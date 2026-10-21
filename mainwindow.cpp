#include "mainwindow.h"

#include <QCoreApplication>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QSqlQuery>
#include <QSqlError>
#include <QItemSelectionModel>
#include <QSizePolicy>
#include <QSettings>
#include <QDir>
#include <QThread>
#include <QDebug>
#include <QStandardPaths>
#include <QIcon>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QSysInfo>
#include <QRegularExpression>
#include <QClipboard>
#include <QProcess>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace {
QString escapeSql(const QString &s) {
    QString copy = s;
    copy.replace('\'', "''");
    return copy;
}

const char *kSettingsSymbolsDir = "symbolsDir";
const char *kSettingsLanguage   = "language";
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_dbManager(new DatabaseManager(this))
{
    setWindowIcon(QIcon(":/icon.ico"));

    setupUi();
    setupMenuBar();
    setupDatabase();
}

MainWindow::~MainWindow() {
    if (m_syncThread && m_syncThread->isRunning()) {
        m_syncThread->quit();
        m_syncThread->wait(3000);
    }
}

void MainWindow::setupUi() {
    setWindowTitle(QString("%1 %2")
                       .arg(QCoreApplication::applicationName(), APP_VERSION));
    resize(1100, 700);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    m_pathEdit = new QLineEdit;
    m_pathEdit->setReadOnly(true);
    m_browseButton  = new QPushButton(tr("Browse..."));
    m_refreshButton = new QPushButton(tr("Scan"));
    m_resetButton   = new QPushButton(tr("Reset search"));
    m_statusLabel   = new QLabel(tr("Ready"));
    m_countsLabel   = new QLabel;
    m_countsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto *topLayout = new QHBoxLayout;
    topLayout->addWidget(new QLabel(tr("KiCad symbols directory:")));
    topLayout->addWidget(m_pathEdit, 1);
    topLayout->addWidget(m_browseButton);
    topLayout->addWidget(m_refreshButton);
    topLayout->addWidget(m_resetButton);

    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 100);
    m_progressBar->setFormat("%v / %m");
    m_progressBar->setVisible(false);

    auto *progressLayout = new QHBoxLayout;
    progressLayout->addWidget(new QLabel(tr("Progress:")));
    progressLayout->addWidget(m_progressBar, 1);

    m_libSearchEdit = new QLineEdit;
    m_libSearchEdit->setPlaceholderText(tr("Search library..."));
    m_libListView = new QListView;
    m_libListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_libModel = new QSqlQueryModel(this);
    m_libListView->setModel(m_libModel);

    auto *leftLayout = new QVBoxLayout;
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->addWidget(new QLabel(tr("Libraries")));
    leftLayout->addWidget(m_libSearchEdit);
    leftLayout->addWidget(m_libListView, 1);

    m_compSearchEdit = new QLineEdit;
    m_compSearchEdit->setPlaceholderText(tr("Search component across all libraries..."));
    m_compListView = new QListView;
    m_compListView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_compModel = new QSqlQueryModel(this);
    m_compListView->setModel(m_compModel);

    auto *rightLayout = new QVBoxLayout;
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->addWidget(new QLabel(tr("Components")));
    rightLayout->addWidget(m_compSearchEdit);
    rightLayout->addWidget(m_compListView, 1);

    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *leftWidget = new QWidget;
    leftWidget->setLayout(leftLayout);
    auto *rightWidget = new QWidget;
    rightWidget->setLayout(rightLayout);
    splitter->addWidget(leftWidget);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto *bottomLayout = new QHBoxLayout;
    bottomLayout->addWidget(m_statusLabel, 1);
    bottomLayout->addWidget(m_countsLabel, 0);

    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->addLayout(topLayout);
    mainLayout->addLayout(progressLayout);
    mainLayout->addWidget(splitter, 1);
    mainLayout->addLayout(bottomLayout);

    connect(m_libSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onLibrarySearchChanged);
    connect(m_compSearchEdit, &QLineEdit::textChanged, this, &MainWindow::onComponentSearchChanged);
    connect(m_libListView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onLibrarySelectionChanged);
    connect(m_browseButton, &QPushButton::clicked, this, &MainWindow::onBrowseDirectory);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::onRefresh);
    connect(m_resetButton, &QPushButton::clicked, this, &MainWindow::onResetSearch);
}

void MainWindow::setupMenuBar() {
    QMenuBar *bar = menuBar();

    // --- Language ---
    QMenu *langMenu = bar->addMenu(tr("&Language"));
    auto *langGroup = new QActionGroup(this);
    langGroup->setExclusive(true);

    QAction *systemAct = langMenu->addAction(tr("System (auto)"));
    systemAct->setCheckable(true);
    systemAct->setData(QStringLiteral("system"));

    QAction *enAct = langMenu->addAction(QStringLiteral("English"));
    enAct->setCheckable(true);
    enAct->setData(QStringLiteral("en"));

    // Имя языка пишем на нём самом — так принято и не зависит от текущего языка.
    QAction *ruAct = langMenu->addAction(QString::fromUtf8("Русский"));
    ruAct->setCheckable(true);
    ruAct->setData(QStringLiteral("ru"));

    langGroup->addAction(systemAct);
    langGroup->addAction(enAct);
    langGroup->addAction(ruAct);

    QSettings s;
    const QString cur = s.value(kSettingsLanguage, "system").toString();
    if (cur == "en")      enAct->setChecked(true);
    else if (cur == "ru") ruAct->setChecked(true);
    else                  systemAct->setChecked(true);

    connect(systemAct, &QAction::triggered, this, &MainWindow::onLanguageSelected);
    connect(enAct,     &QAction::triggered, this, &MainWindow::onLanguageSelected);
    connect(ruAct,     &QAction::triggered, this, &MainWindow::onLanguageSelected);

    // --- Help ---
    QMenu *helpMenu = bar->addMenu(tr("&Help"));

    QAction *aboutAct = helpMenu->addAction(tr("&About..."));
    aboutAct->setMenuRole(QAction::AboutRole);
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAbout);

    QAction *aboutQtAct = helpMenu->addAction(tr("About &Qt..."));
    aboutQtAct->setMenuRole(QAction::AboutQtRole);
    connect(aboutQtAct, &QAction::triggered, this, &MainWindow::onAboutQt);
}

void MainWindow::onLanguageSelected() {
    QAction *act = qobject_cast<QAction *>(sender());
    if (!act) return;

    const QString lang = act->data().toString();
    QSettings().setValue(kSettingsLanguage, lang);

    QMessageBox box(this);
    box.setIcon(QMessageBox::Information);
    box.setWindowTitle(tr("Language changed"));
    box.setText(tr("The interface language has been changed. "
                   "Restart the application to apply changes."));

    QPushButton *restartBtn = box.addButton(tr("Restart now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.exec();

    if (box.clickedButton() == restartBtn) {
        const QString exe = QCoreApplication::applicationFilePath();
        const QStringList args = QCoreApplication::arguments().mid(1);
        qApp->quit();
        QProcess::startDetached(exe, args);
    }
}

QString MainWindow::buildInfoHtml() const {
    const QString qtBuild   = QString::fromLatin1(QT_VERSION_STR);
    const QString qtRuntime = QString::fromLatin1(qVersion());

    QString compiler = tr("unknown");
#if defined(__clang__)
    compiler = QStringLiteral("Clang %1.%2.%3")
                   .arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    compiler = QStringLiteral("GCC %1.%2.%3")
                   .arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#  ifdef __VERSION__
    compiler += QStringLiteral(" (__VERSION__=\"%1\")").arg(QString::fromLatin1(__VERSION__));
#  endif
#elif defined(_MSC_VER)
    compiler = QStringLiteral("MSVC %1").arg(_MSC_VER);
#endif

    QString mingw = QString::fromUtf8("—");
#if defined(__MINGW64_VERSION_MAJOR)
    mingw = QStringLiteral("MinGW-w64 %1.%2")
                .arg(__MINGW64_VERSION_MAJOR).arg(__MINGW64_VERSION_MINOR);
#elif defined(__MINGW32_VERSION_MAJOR)
    mingw = QStringLiteral("MinGW32 %1.%2")
                .arg(__MINGW32_VERSION_MAJOR).arg(__MINGW32_VERSION_MINOR);
#endif

    const QString buildArch = QSysInfo::buildCpuArchitecture();
    const QString currArch  = QSysInfo::currentCpuArchitecture();
    const QString buildAbi  = QSysInfo::buildAbi();
    const QString prettyOs  = QSysInfo::prettyProductName();
    const QString kernel    = QSysInfo::kernelVersion();

    QString qtDiff;
    if (qtBuild != qtRuntime) {
        qtDiff = QStringLiteral("<br><span style='color:#c0392b;'><b>%1</b></span>")
        .arg(tr("Attention: Qt runtime version differs from build version!"));
    }

    QString html;
    html += QString(
                "<h3>%1 %2</h3>"
                "<p>%3</p>"
                "<table cellpadding='4' style='border-collapse:collapse;'>"
                "<tr><td><b>%4</b></td><td>%1 %2</td></tr>"
                "<tr><td><b>%5</b></td><td>%6</td></tr>"
                "<tr><td><b>%7</b></td><td>%8%9</td></tr>"
                "<tr><td><b>%10</b></td><td>%11</td></tr>"
                "<tr><td><b>%12</b></td><td>%13</td></tr>"
                "<tr><td><b>%14</b></td><td>%15</td></tr>"
                "<tr><td><b>%16</b></td><td>%17 → %18</td></tr>"
                "<tr><td><b>%19</b></td><td>%20</td></tr>"
                "<tr><td><b>%21</b></td><td>%22</td></tr>"
                "</table>"
                "<p style='color:#666;font-size:small;'>%23 %24</p>"
                ).arg(
                    QCoreApplication::applicationName().toHtmlEscaped(),
                    QStringLiteral(APP_VERSION),
                    tr("Search components in KiCad symbol libraries."),
                    tr("Application:"),
                    tr("Qt (build):"),
                    qtBuild.toHtmlEscaped(),
                    tr("Qt (runtime):"),
                    qtRuntime.toHtmlEscaped(),
                    qtDiff,
                    tr("Compiler:"),
                    compiler.toHtmlEscaped(),
                    tr("MinGW:"),
                    mingw.toHtmlEscaped(),
                    tr("Build ABI:"),
                    buildAbi.toHtmlEscaped(),
                    tr("Architecture (build → runtime):"),
                    buildArch.toHtmlEscaped(),
                    currArch.toHtmlEscaped(),
                    tr("OS:"),
                    prettyOs.toHtmlEscaped(),
                    tr("Kernel:"),
                    kernel.toHtmlEscaped(),
                    tr("Database:"),
                    QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toHtmlEscaped()
                    );

    return html;
}

void MainWindow::onAbout() {
    QMessageBox box(this);
    box.setWindowTitle(tr("About"));
    box.setIconPixmap(QIcon(":/icon.png").pixmap(64, 64));
    box.setTextFormat(Qt::RichText);
    box.setText(buildInfoHtml());

    QPushButton *copyBtn = box.addButton(tr("Copy to clipboard"), QMessageBox::ActionRole);
    box.addButton(QMessageBox::Close);
    box.exec();

    if (box.clickedButton() == copyBtn) {
        QString plain = buildInfoHtml();
        plain.remove(QRegularExpression("<[^>]*>"));
        plain.replace("&nbsp;", " ");
        QApplication::clipboard()->setText(plain);
    }
}

void MainWindow::onAboutQt() {
    QMessageBox::aboutQt(this, tr("About Qt"));
}

void MainWindow::setupDatabase() {
    const QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir);
    m_dbPath = appDataDir + "/kicad_libs.db";

    if (!m_dbManager->openDatabase(m_dbPath)) {
        QMessageBox::critical(this, tr("Error"), tr("Failed to open the database."));
        return;
    }

    QSettings settings;
    const QString savedDir = settings.value(kSettingsSymbolsDir).toString();

    if (!savedDir.isEmpty() && QDir(savedDir).exists()) {
        m_symbolsDir = QDir::toNativeSeparators(savedDir);
    } else {
        m_symbolsDir = DatabaseManager::detectKiCadSymbolsPath();
        if (!m_symbolsDir.isEmpty())
            settings.setValue(kSettingsSymbolsDir, m_symbolsDir);
    }

    if (m_symbolsDir.isEmpty())
        m_pathEdit->setText(tr("(not found, choose manually)"));
    else
        m_pathEdit->setText(m_symbolsDir);

    updateLibraryModel();

    if (m_libModel->rowCount() == 0)
        m_statusLabel->setText(tr("Database is empty. Click \"Scan\" to populate."));
    else
        m_statusLabel->setText(tr("Ready. Click \"Scan\" to update the database."));
}

void MainWindow::startSync() {
    if (m_syncThread) return;
    if (m_symbolsDir.isEmpty()) {
        QMessageBox::warning(this, tr("Synchronization"),
                             tr("Symbols directory is not specified."));
        return;
    }

    m_refreshButton->setEnabled(false);
    m_browseButton->setEnabled(false);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, 0);
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Starting synchronization..."));

    m_syncThread = new QThread(this);
    m_syncWorker = new SyncWorker(m_dbPath, m_symbolsDir);
    m_syncWorker->moveToThread(m_syncThread);

    connect(m_syncThread, &QThread::started, m_syncWorker, &SyncWorker::process);
    connect(m_syncWorker, &SyncWorker::progress, this, &MainWindow::onSyncProgress);
    connect(m_syncWorker, &SyncWorker::done, this, &MainWindow::onSyncDone);
    connect(m_syncWorker, &SyncWorker::done, m_syncThread, &QThread::quit);
    connect(m_syncThread, &QThread::finished, m_syncWorker, &QObject::deleteLater);
    connect(m_syncThread, &QThread::finished, m_syncThread, &QObject::deleteLater);

    m_syncThread->start();
}

void MainWindow::onSyncProgress(int current, int total, const QString &message) {
    if (total > 0) {
        if (m_progressBar->maximum() != total)
            m_progressBar->setRange(0, total);
        m_progressBar->setValue(current);
    } else {
        m_progressBar->setRange(0, 0);
    }
    if (!message.isEmpty())
        m_statusLabel->setText(message);
}

void MainWindow::onSyncDone(bool ok, const QString &error) {
    m_progressBar->setVisible(false);
    m_refreshButton->setEnabled(true);
    m_browseButton->setEnabled(true);

    if (ok) {
        m_statusLabel->setText(tr("Synchronization complete."));
        if (!m_symbolsDir.isEmpty())
            QSettings().setValue(kSettingsSymbolsDir, m_symbolsDir);
    } else {
        m_statusLabel->setText(tr("Synchronization error: %1").arg(error));
        QMessageBox::warning(this, tr("Synchronization error"), error);
    }

    updateLibraryModel();
}

void MainWindow::updateLibraryModel() {
    const QString libSearch  = m_libSearchEdit->text().trimmed();
    const QString compSearch = m_compSearchEdit->text().trimmed();

    QString prevSelection;
    if (m_libListView->currentIndex().isValid())
        prevSelection = m_libListView->currentIndex().data(Qt::DisplayRole).toString();

    QString queryStr;
    if (!compSearch.isEmpty()) {
        queryStr = "SELECT DISTINCT l.name FROM libraries l "
                   "JOIN components c ON c.library_id = l.id "
                   "WHERE c.name LIKE '%" + escapeSql(compSearch) + "%'";
        if (!libSearch.isEmpty())
            queryStr += " AND l.name LIKE '%" + escapeSql(libSearch) + "%'";
        queryStr += " ORDER BY l.name";
    } else {
        queryStr = "SELECT name FROM libraries";
        if (!libSearch.isEmpty())
            queryStr += " WHERE name LIKE '%" + escapeSql(libSearch) + "%'";
        queryStr += " ORDER BY name";
    }

    m_suppressLibrarySignal = true;
    m_libModel->setQuery(queryStr, m_dbManager->database());
    m_libModel->setHeaderData(0, Qt::Horizontal, tr("Library"));

    int restoreRow = -1;
    if (!prevSelection.isEmpty()) {
        for (int i = 0; i < m_libModel->rowCount(); ++i) {
            if (m_libModel->index(i, 0).data(Qt::DisplayRole).toString() == prevSelection) {
                restoreRow = i;
                break;
            }
        }
    }

    if (restoreRow < 0 && compSearch.isEmpty() && m_libModel->rowCount() > 0)
        restoreRow = 0;

    if (restoreRow >= 0) {
        m_libListView->setCurrentIndex(m_libModel->index(restoreRow, 0));
    } else {
        m_libListView->clearSelection();
        m_libListView->setCurrentIndex(QModelIndex());
    }

    m_suppressLibrarySignal = false;

    updateComponentModel();
}

void MainWindow::updateComponentModel() {
    const QString searchText = m_compSearchEdit->text().trimmed();
    const QModelIndex currentIndex = m_libListView->currentIndex();
    const bool hasLib = currentIndex.isValid();

    if (!searchText.isEmpty()) {
        if (hasLib) {
            const QString libName = currentIndex.data(Qt::DisplayRole).toString();
            const QString queryStr = QString(
                                         "SELECT c.name FROM components c "
                                         "JOIN libraries l ON c.library_id = l.id "
                                         "WHERE c.name LIKE '%%1%' AND l.name = '%2' "
                                         "ORDER BY c.name"
                                         ).arg(escapeSql(searchText), escapeSql(libName));
            m_compModel->setQuery(queryStr, m_dbManager->database());
            m_compModel->setHeaderData(0, Qt::Horizontal,
                                       tr("Component (in %1)").arg(libName));
        } else {
            const QString queryStr = QString(
                                         "SELECT c.name || '  (' || l.name || ')' AS display_name "
                                         "FROM components c "
                                         "JOIN libraries l ON c.library_id = l.id "
                                         "WHERE c.name LIKE '%%1%' "
                                         "ORDER BY c.name, l.name"
                                         ).arg(escapeSql(searchText));
            m_compModel->setQuery(queryStr, m_dbManager->database());
            m_compModel->setHeaderData(0, Qt::Horizontal, tr("Component (Library)"));
        }
    } else {
        if (!hasLib) {
            m_compModel->setQuery("SELECT NULL WHERE 0", m_dbManager->database());
        } else {
            const QString libName = currentIndex.data(Qt::DisplayRole).toString();
            const QString queryStr = QString(
                                         "SELECT c.name FROM components c "
                                         "JOIN libraries l ON c.library_id = l.id "
                                         "WHERE l.name = '%1' "
                                         "ORDER BY c.name"
                                         ).arg(escapeSql(libName));
            m_compModel->setQuery(queryStr, m_dbManager->database());
            m_compModel->setHeaderData(0, Qt::Horizontal, tr("Component"));
        }
    }

    updateCounts();
}

void MainWindow::updateCounts() {
    const int libsShown  = m_libModel ? m_libModel->rowCount() : 0;
    const int compsShown = m_compModel ? m_compModel->rowCount() : 0;
    const QString searchText = m_compSearchEdit->text().trimmed();

    QString parts;
    parts += tr("Libraries: %1").arg(libsShown);
    parts += tr("   |   Components: %1").arg(compsShown);

    if (!searchText.isEmpty()) {
        QSqlQuery q(m_dbManager->database());
        q.prepare("SELECT COUNT(*) FROM components WHERE name LIKE :pat");
        q.bindValue(":pat", QString("%%1%").arg(searchText));
        int totalMatches = 0;
        if (q.exec() && q.next())
            totalMatches = q.value(0).toInt();
        parts += tr("   |   Matches (all libraries): %1").arg(totalMatches);
    }

    m_countsLabel->setText(parts);
}

void MainWindow::onLibrarySearchChanged(const QString &) {
    updateLibraryModel();
}

void MainWindow::onComponentSearchChanged(const QString &) {
    updateLibraryModel();
}

void MainWindow::onLibrarySelectionChanged() {
    if (m_suppressLibrarySignal) return;
    updateComponentModel();
}

void MainWindow::onResetSearch() {
    {
        const bool b1 = m_libSearchEdit->blockSignals(true);
        const bool b2 = m_compSearchEdit->blockSignals(true);
        m_libSearchEdit->clear();
        m_compSearchEdit->clear();
        m_libSearchEdit->blockSignals(b1);
        m_compSearchEdit->blockSignals(b2);
    }

    m_suppressLibrarySignal = true;

    m_libModel->setQuery("SELECT name FROM libraries ORDER BY name",
                         m_dbManager->database());
    m_libModel->setHeaderData(0, Qt::Horizontal, tr("Library"));

    m_libListView->clearSelection();
    m_libListView->setCurrentIndex(QModelIndex());

    m_compModel->setQuery(
        "SELECT c.name || '  (' || l.name || ')' AS display_name "
        "FROM components c "
        "JOIN libraries l ON c.library_id = l.id "
        "ORDER BY c.name, l.name",
        m_dbManager->database());
    m_compModel->setHeaderData(0, Qt::Horizontal, tr("Component (Library)"));

    m_suppressLibrarySignal = false;

    updateCounts();
    m_statusLabel->setText(tr("Search reset"));
}

void MainWindow::onBrowseDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, tr("Choose folder with *.kicad_sym"), m_symbolsDir);
    if (dir.isEmpty() || dir == m_symbolsDir) return;

    m_symbolsDir = dir;
    m_pathEdit->setText(m_symbolsDir);
    QSettings().setValue(kSettingsSymbolsDir, m_symbolsDir);
    startSync();
}

void MainWindow::onRefresh() {
    startSync();
}
