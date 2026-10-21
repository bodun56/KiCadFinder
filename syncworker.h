#ifndef SYNCWORKER_H
#define SYNCWORKER_H

#include <QObject>
#include <QString>

class SyncWorker : public QObject
{
    Q_OBJECT
public:
    SyncWorker(const QString &dbPath, const QString &symbolsDir, QObject *parent = nullptr);
    ~SyncWorker() = default;

public slots:
    void process();

signals:
    void progress(int current, int total, const QString &message);
    void done(bool success, const QString &error);

private:
    QString m_dbPath;
    QString m_symbolsDir;
};

#endif // SYNCWORKER_H
