#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager();

    bool openDatabase(const QString &dbPath);
    void closeDatabase();

    QSqlDatabase database() const { return m_db; }

    bool syncLibraries(const QString &symbolsDir, QString *errorMsg = nullptr);

    static QString detectKiCadSymbolsPath();
    static QStringList extractSymbolNames(const QString &filePath);

signals:
    void syncProgress(int current, int total, const QString &message);

private:
    QSqlDatabase m_db;
    QString m_connectionName;

    bool createTables(QString *errorMsg);
};

#endif // DATABASEMANAGER_H
