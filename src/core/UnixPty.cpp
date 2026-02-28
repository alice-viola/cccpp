#ifndef _WIN32

#include "core/UnixPty.h"
#include <QSocketNotifier>
#include <QCoreApplication>

#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <errno.h>

#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

UnixPty::UnixPty(QObject *parent)
    : PtyProcess(parent)
{
}

UnixPty::~UnixPty()
{
    terminate();
}

bool UnixPty::start(const QString &program, const QStringList &args,
                    const QString &workingDir, const QStringList &env)
{
    struct winsize ws = {};
    ws.ws_row = 24;
    ws.ws_col = 80;

    pid_t pid = forkpty(&m_masterFd, nullptr, nullptr, &ws);
    if (pid < 0)
        return false;

    if (pid == 0) {
        // Child process
        if (!workingDir.isEmpty())
            chdir(workingDir.toLocal8Bit().constData());

        for (const QString &e : env) {
            putenv(strdup(e.toLocal8Bit().constData()));
        }

        QByteArray prog = program.toLocal8Bit();
        QList<QByteArray> argBytes;
        QVector<char *> argv;
        argv.append(prog.data());
        for (const QString &a : args) {
            argBytes.append(a.toLocal8Bit());
            argv.append(argBytes.last().data());
        }
        argv.append(nullptr);

        execvp(prog.constData(), argv.data());
        _exit(127);
    }

    // Parent process
    m_childPid = pid;
    m_running = true;

    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &UnixPty::onReadyRead);

    return true;
}

void UnixPty::write(const QByteArray &data)
{
    if (m_masterFd >= 0)
        ::write(m_masterFd, data.constData(), data.size());
}

void UnixPty::resize(int rows, int cols)
{
    if (m_masterFd < 0)
        return;

    struct winsize ws = {};
    ws.ws_row = static_cast<unsigned short>(rows);
    ws.ws_col = static_cast<unsigned short>(cols);
    ioctl(m_masterFd, TIOCSWINSZ, &ws);
}

void UnixPty::terminate()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        delete m_notifier;
        m_notifier = nullptr;
    }

    if (m_childPid > 0) {
        kill(m_childPid, SIGHUP);
        int status = 0;
        waitpid(m_childPid, &status, WNOHANG);
        m_childPid = -1;
    }

    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }

    m_running = false;
}

bool UnixPty::isRunning() const
{
    return m_running;
}

void UnixPty::onReadyRead()
{
    char buf[4096];
    ssize_t n = ::read(m_masterFd, buf, sizeof(buf));
    if (n > 0) {
        emit dataReceived(QByteArray(buf, static_cast<int>(n)));
    } else if (n <= 0) {
        m_running = false;
        if (m_notifier)
            m_notifier->setEnabled(false);

        int status = 0;
        int exitCode = 0;
        if (m_childPid > 0 && waitpid(m_childPid, &status, WNOHANG) > 0) {
            if (WIFEXITED(status))
                exitCode = WEXITSTATUS(status);
        }
        m_childPid = -1;
        emit finished(exitCode);
    }
}

#endif // !_WIN32
