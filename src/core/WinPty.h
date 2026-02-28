#pragma once

#ifdef _WIN32

#include "core/PtyProcess.h"
#include <windows.h>

class QThread;

class WinPty : public PtyProcess {
    Q_OBJECT
public:
    explicit WinPty(QObject *parent = nullptr);
    ~WinPty() override;

    bool start(const QString &program, const QStringList &args,
               const QString &workingDir,
               const QStringList &env = {}) override;
    void write(const QByteArray &data) override;
    void resize(int rows, int cols) override;
    void terminate() override;
    bool isRunning() const override;

private:
    void readerThreadFunc();

    HPCON m_hPC = INVALID_HANDLE_VALUE;
    HANDLE m_pipeIn = INVALID_HANDLE_VALUE;   // write end (our input to the console)
    HANDLE m_pipeOut = INVALID_HANDLE_VALUE;  // read end (console output to us)
    HANDLE m_hProcess = INVALID_HANDLE_VALUE;
    QThread *m_readerThread = nullptr;
    bool m_running = false;
};

#endif // _WIN32
