#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLineEdit>
#include <QListView>
#include <QSplitter>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QSqlQueryModel>
#include <QPointer>

#include "databasemanager.h"
#include "syncworker.h"

class QThread;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onLibrarySearchChanged(const QString &text);
    void onComponentSearchChanged(const QString &text);
    void onLibrarySelectionChanged();
    void onBrowseDirectory();
    void onRefresh();
    void onResetSearch();

    void onSyncProgress(int current, int total, const QString &message);
    void onSyncDone(bool ok, const QString &error);

    void onAbout();
    void onAboutQt();
    void onLanguageSelected();

private:
    void setupUi();
    void setupMenuBar();
    void setupDatabase();
    void startSync();
    void updateLibraryModel();
    void updateComponentModel();
    void updateCounts();

    QString buildInfoHtml() const;

    DatabaseManager *m_dbManager;
    QString m_dbPath;
    QString m_symbolsDir;

    QLineEdit *m_libSearchEdit;
    QListView *m_libListView;
    QSqlQueryModel *m_libModel;

    QLineEdit *m_compSearchEdit;
    QListView *m_compListView;
    QSqlQueryModel *m_compModel;

    QLineEdit *m_pathEdit;
    QPushButton *m_browseButton;
    QPushButton *m_refreshButton;
    QPushButton *m_resetButton;
    QLabel *m_statusLabel;
    QLabel *m_countsLabel;
    QProgressBar *m_progressBar;

    bool m_suppressLibrarySignal = false;

    QPointer<QThread> m_syncThread;
    QPointer<SyncWorker> m_syncWorker;
};

#endif // MAINWINDOW_H
