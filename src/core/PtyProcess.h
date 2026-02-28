#pragma once

#include <QObject>
#include <QByteArray>
#include <QString>
#include <QStringList>

class PtyProcess : public QObject {
    Q_OBJECT
public:
    explicit PtyProcess(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~PtyProcess() = default;

    virtual bool start(const QString &program, const QStringList &args,
                       const QString &workingDir,
                       const QStringList &env = {}) = 0;
    virtual void write(const QByteArray &data) = 0;
    virtual void resize(int rows, int cols) = 0;
    virtual void terminate() = 0;
    virtual bool isRunning() const = 0;

    static PtyProcess *create(QObject *parent = nullptr);

signals:
    void dataReceived(const QByteArray &data);
    void finished(int exitCode);
};
