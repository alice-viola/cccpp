#pragma once

#ifndef _WIN32

#include "core/PtyProcess.h"

class QSocketNotifier;

class UnixPty : public PtyProcess {
    Q_OBJECT
public:
    explicit UnixPty(QObject *parent = nullptr);
    ~UnixPty() override;

    bool start(const QString &program, const QStringList &args,
               const QString &workingDir,
               const QStringList &env = {}) override;
    void write(const QByteArray &data) override;
    void resize(int rows, int cols) override;
    void terminate() override;
    bool isRunning() const override;

private slots:
    void onReadyRead();

private:
    int m_masterFd = -1;
    pid_t m_childPid = -1;
    QSocketNotifier *m_notifier = nullptr;
    bool m_running = false;
};

#endif // !_WIN32
