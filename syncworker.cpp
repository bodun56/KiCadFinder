#include "syncworker.h"
#include "databasemanager.h"

SyncWorker::SyncWorker(const QString &dbPath, const QString &symbolsDir, QObject *parent)
    : QObject(parent)
    , m_dbPath(dbPath)
    , m_symbolsDir(symbolsDir)
{}

void SyncWorker::process() {
    DatabaseManager dbManager;

    connect(&dbManager, &DatabaseManager::syncProgress,
            this, &SyncWorker::progress, Qt::DirectConnection);

    if (!dbManager.openDatabase(m_dbPath)) {
        emit done(false, tr("Failed to open the database."));
        return;
    }

    QString error;
    const bool ok = dbManager.syncLibraries(m_symbolsDir, &error);
    dbManager.closeDatabase();

    emit done(ok, error);
}
