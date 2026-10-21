#include "databasemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QAtomicInt>
#include <QDebug>

namespace {
QAtomicInt g_connCounter(0);
}

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {}

DatabaseManager::~DatabaseManager() {
    closeDatabase();
}

bool DatabaseManager::openDatabase(const QString &dbPath) {
    closeDatabase();

    const int id = g_connCounter.fetchAndAddOrdered(1);
    m_connectionName = QString("kicad_conn_%1").arg(id);

    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qWarning() << "Failed to open database:" << m_db.lastError().text();
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
        return false;
    }
    return createTables(nullptr);
}

void DatabaseManager::closeDatabase() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    if (!m_connectionName.isEmpty() && QSqlDatabase::contains(m_connectionName)) {
        QSqlDatabase::removeDatabase(m_connectionName);
    }
    m_connectionName.clear();
}

bool DatabaseManager::createTables(QString *errorMsg) {
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA foreign_keys = ON;")) {
        if (errorMsg) *errorMsg = query.lastError().text();
        return false;
    }

    const QString sqlLibraries = R"(
        CREATE TABLE IF NOT EXISTS libraries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            path TEXT NOT NULL
        );
    )";
    const QString sqlComponents = R"(
        CREATE TABLE IF NOT EXISTS components (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            library_id INTEGER NOT NULL,
            name TEXT NOT NULL,
            FOREIGN KEY(library_id) REFERENCES libraries(id) ON DELETE CASCADE,
            UNIQUE(library_id, name)
        );
    )";
    const QString sqlIndex = R"(
        CREATE INDEX IF NOT EXISTS idx_components_name ON components(name);
    )";

    if (!query.exec(sqlLibraries)) {
        if (errorMsg) *errorMsg = query.lastError().text();
        return false;
    }
    if (!query.exec(sqlComponents)) {
        if (errorMsg) *errorMsg = query.lastError().text();
        return false;
    }
    if (!query.exec(sqlIndex)) {
        if (errorMsg) *errorMsg = query.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::syncLibraries(const QString &symbolsDir, QString *errorMsg) {
    QDir dir(symbolsDir);
    if (!dir.exists()) {
        if (errorMsg) *errorMsg = QString("Directory does not exist: %1").arg(symbolsDir);
        return false;
    }

    QStringList filters;
    filters << "*.kicad_sym";
    dir.setNameFilters(filters);
    dir.setFilter(QDir::Files | QDir::NoSymLinks);

    const QFileInfoList fileList = dir.entryInfoList();
    const int total = fileList.size();

    emit syncProgress(0, total, tr("Scanning: %1 files").arg(total));

    if (!m_db.transaction()) {
        if (errorMsg) *errorMsg = m_db.lastError().text();
        return false;
    }

    // Существующие библиотеки
    QSqlQuery existingLibsQuery(m_db);
    existingLibsQuery.exec("SELECT name FROM libraries");
    QSet<QString> existingLibNames;
    while (existingLibsQuery.next()) existingLibNames.insert(existingLibsQuery.value(0).toString());

    QSet<QString> processedLibNames;
    int current = 0;

    for (const QFileInfo &fi : fileList) {
        ++current;
        const QString libName = fi.completeBaseName();
        processedLibNames.insert(libName);

        // 1. Библиотека: вставить / обновить
        QSqlQuery libQuery(m_db);
        libQuery.prepare("INSERT INTO libraries (name, path) VALUES (:name, :path) "
                         "ON CONFLICT(name) DO UPDATE SET path=excluded.path");
        libQuery.bindValue(":name", libName);
        libQuery.bindValue(":path", fi.absoluteFilePath());
        if (!libQuery.exec()) {
            if (errorMsg) *errorMsg = libQuery.lastError().text();
            m_db.rollback();
            return false;
        }

        // 2. Получить id
        int libId = -1;
        {
            QSqlQuery idQuery(m_db);
            idQuery.prepare("SELECT id FROM libraries WHERE name = :name");
            idQuery.bindValue(":name", libName);
            if (idQuery.exec() && idQuery.next()) {
                libId = idQuery.value(0).toInt();
            } else {
                if (errorMsg) *errorMsg = "Failed to retrieve library id: " + idQuery.lastError().text();
                m_db.rollback();
                return false;
            }
        }

        // 3. Извлечь имена символов
        const QStringList symbolNames = extractSymbolNames(fi.absoluteFilePath());

        // 4. Получить существующие компоненты
        QSet<QString> existingComps;
        {
            QSqlQuery compQuery(m_db);
            compQuery.prepare("SELECT name FROM components WHERE library_id = :lib_id");
            compQuery.bindValue(":lib_id", libId);
            compQuery.exec();
            while (compQuery.next()) existingComps.insert(compQuery.value(0).toString());
        }

        // 5. Удалить исчезнувшие
        for (const QString &oldName : existingComps) {
            if (!symbolNames.contains(oldName)) {
                QSqlQuery del(m_db);
                del.prepare("DELETE FROM components WHERE library_id = :lib_id AND name = :name");
                del.bindValue(":lib_id", libId);
                del.bindValue(":name", oldName);
                del.exec();
            }
        }

        // 6. Вставить новые
        for (const QString &symName : symbolNames) {
            if (!existingComps.contains(symName)) {
                QSqlQuery ins(m_db);
                ins.prepare("INSERT INTO components (library_id, name) VALUES (:lib_id, :name)");
                ins.bindValue(":lib_id", libId);
                ins.bindValue(":name", symName);
                if (!ins.exec()) {
                    if (errorMsg) *errorMsg = ins.lastError().text();
                    m_db.rollback();
                    return false;
                }
            }
        }

        emit syncProgress(current, total, QStringLiteral("%1 (%2 symbols)").arg(libName).arg(symbolNames.size()));
    }

    // Удалить библиотеки, чьи файлы исчезли
    for (const QString &oldLib : existingLibNames) {
        if (!processedLibNames.contains(oldLib)) {
            QSqlQuery del(m_db);
            del.prepare("DELETE FROM libraries WHERE name = :name");
            del.bindValue(":name", oldLib);
            del.exec();
        }
    }

    if (!m_db.commit()) {
        if (errorMsg) *errorMsg = m_db.lastError().text();
        return false;
    }

    emit syncProgress(total, total, tr("Synchronization complete"));
    return true;
}

// Парсер S-выражений KiCad.
// Символы верхнего уровня находятся на глубине 2:
//   (kicad_symbol_lib ...        <- depth 1
//       (symbol "NAME" ...       <- depth 2  <-- нас интересует
//           (symbol "NAME_1_1"   <- depth 3  <-- игнорируем
QStringList DatabaseManager::extractSymbolNames(const QString &filePath) {
    QStringList names;
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return names;

    QTextStream in(&file);
    in.setEncoding(QStringConverter::Utf8);
    const QString content = in.readAll();
    file.close();

    int depth = 0;
    bool inString = false;
    bool escape = false;
    QString currentToken;
    QString pendingKeyword;

    for (int i = 0; i < content.size(); ++i) {
        const QChar ch = content.at(i);

        if (inString) {
            if (escape) {
                currentToken.append(ch);
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                inString = false;
                if (depth == 2 && pendingKeyword == QLatin1String("symbol")) {
                    if (!currentToken.isEmpty() && !names.contains(currentToken)) {
                        names.append(currentToken);
                    }
                }
                pendingKeyword.clear();
                currentToken.clear();
            } else {
                currentToken.append(ch);
            }
            continue;
        }

        if (ch == '"') {
            inString = true;
            currentToken.clear();
            continue;
        }

        if (ch == '(') {
            ++depth;
            currentToken.clear();
            pendingKeyword.clear();
            continue;
        }
        if (ch == ')') {
            --depth;
            currentToken.clear();
            pendingKeyword.clear();
            continue;
        }
        if (ch.isSpace()) {
            if (depth == 2 && !currentToken.isEmpty()) {
                if (currentToken == QLatin1String("symbol")) {
                    pendingKeyword = currentToken;
                }
                currentToken.clear();
            }
            continue;
        }

        currentToken.append(ch);
    }

    return names;
}

QString DatabaseManager::detectKiCadSymbolsPath() {
    QStringList candidates;
    const QString programFiles = qEnvironmentVariable("ProgramFiles", "C:/Program Files");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)", "C:/Program Files (x86)");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");

    const QString envPath = qEnvironmentVariable("KICAD_SYMBOL_DIR");
    if (!envPath.isEmpty()) candidates.prepend(envPath);

    const QStringList versions = {"10.0", "9.0", "8.0", "7.0", "6.0"};
    for (const QString &ver : versions) {
        candidates << QString("%1/KiCad/%2/share/kicad/symbols").arg(programFiles, ver);
        candidates << QString("%1/KiCad/%2/share/kicad/symbols").arg(programFilesX86, ver);
        if (!localAppData.isEmpty())
            candidates << QString("%1/Programs/KiCad/%2/share/kicad/symbols").arg(localAppData, ver);
    }

    for (const QString &path : candidates) {
        QDir d(path);
        if (d.exists()) return QDir::toNativeSeparators(d.absolutePath());
    }
    return QString();
}
