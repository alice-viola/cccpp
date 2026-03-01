#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "util/Config.h"
#include <QProcessEnvironment>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUuid>
#include <QDebug>

ClaudeProcess::ClaudeProcess(QObject *parent)
    : QObject(parent)
    , m_parser(new StreamParser(this))
{
}

ClaudeProcess::~ClaudeProcess()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->terminate();
        m_process->waitForFinished(2000);
    }
}

void ClaudeProcess::setWorkingDirectory(const QString &dir)
{
    m_workingDir = dir;
}

void ClaudeProcess::setSessionId(const QString &id)
{
    m_sessionId = id;
}

void ClaudeProcess::setMode(const QString &mode)
{
    m_mode = mode;
}

void ClaudeProcess::setModel(const QString &model)
{
    m_model = model;
}

void ClaudeProcess::sendMessage(const QString &message)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        emit errorOccurred("Process already running");
        return;
    }

    m_parser->reset();
    m_stdoutBuffer.clear();

    m_process = new QProcess(this);
    if (!m_workingDir.isEmpty())
        m_process->setWorkingDirectory(m_workingDir);

    // macOS GUI apps don't inherit shell PATH
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString path = env.value("PATH");
    QStringList extraPaths = {
        QDir::homePath() + "/.local/bin",
        "/usr/local/bin",
        "/opt/homebrew/bin",
        "/opt/homebrew/sbin",
    };
    QDir nvmDir(QDir::homePath() + "/.nvm/versions/node");
    if (nvmDir.exists()) {
        QStringList versions = nvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!versions.isEmpty())
            extraPaths.prepend(nvmDir.absoluteFilePath(versions.last()) + "/bin");
    }
    for (const QString &p : extraPaths) {
        if (!path.contains(p) && QDir(p).exists())
            path = p + ":" + path;
    }
    env.insert("PATH", path);
    if (env.value("HOME").isEmpty())
        env.insert("HOME", QDir::homePath());
    env.insert("CLAUDE_CODE_ENABLE_SDK_FILE_CHECKPOINTING", "1");
    m_process->setProcessEnvironment(env);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ClaudeProcess::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &ClaudeProcess::onReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &ClaudeProcess::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        QString msg;
        switch (err) {
        case QProcess::FailedToStart:
            msg = "Failed to start 'claude'. Is it installed and in your PATH?"; break;
        case QProcess::Crashed:
            msg = "Claude process crashed."; break;
        case QProcess::Timedout:
            msg = "Claude process timed out."; break;
        case QProcess::WriteError:
            msg = "Write error communicating with claude."; break;
        case QProcess::ReadError:
            msg = "Read error communicating with claude."; break;
        default:
            msg = "Unknown process error."; break;
        }
        qWarning() << "QProcess error:" << msg;
        emit errorOccurred(msg);
    });
    connect(m_process, &QProcess::started, this, [] {
        qDebug() << "[cccpp] Claude process started";
    });

    // Resolve the full path to the claude binary.
    // If the user configured a path in settings, use it directly.
    QString claudeBin = Config::instance().claudeBinary();
    if (claudeBin == "claude" || claudeBin.isEmpty()) {
        QStringList searchDirs = {
            QDir::homePath() + "/.local/bin",
            "/usr/local/bin",
            "/opt/homebrew/bin",
        };
        for (const QString &dir : searchDirs) {
            QString candidate = dir + "/claude";
            if (QFile::exists(candidate)) {
                claudeBin = candidate;
                break;
            }
        }
    }

    QStringList args = buildArguments(message);
    qDebug() << "[cccpp] Starting:" << claudeBin;
    qDebug() << "[cccpp] Working dir:" << m_workingDir;

    // Force line-buffered stdout via stdbuf if available, otherwise run directly
    QString stdbuf = "/usr/bin/stdbuf";
    if (!QFile::exists(stdbuf))
        stdbuf = "/opt/homebrew/bin/stdbuf";

    if (QFile::exists(stdbuf)) {
        QStringList wrappedArgs;
        wrappedArgs << "-oL" << claudeBin << args;
        qDebug() << ">>> Using stdbuf wrapper";
        m_process->start(stdbuf, wrappedArgs);
    } else {
        m_process->start(claudeBin, args);
    }

    if (!m_process->waitForStarted(5000)) {
        qWarning() << "[cccpp] Process failed to start within 5 seconds";
        emit errorOccurred(QStringLiteral("Process failed to start: %1").arg(claudeBin));
    }

    // Send the user message as stream-json on stdin, then close.
    // Format matches the Anthropic API message structure that the CLI expects.
    QByteArray escaped = message.toUtf8();
    escaped.replace('\\', "\\\\");
    escaped.replace('"', "\\\"");
    escaped.replace('\n', "\\n");
    escaped.replace('\r', "\\r");
    escaped.replace('\t', "\\t");
    QByteArray jsonMsg = "{\"type\":\"user\",\"message\":{\"role\":\"user\","
        "\"content\":[{\"type\":\"text\",\"text\":\"" + escaped + "\"}]}}\n";
    m_process->write(jsonMsg);
    m_process->closeWriteChannel();

    emit started();
}

void ClaudeProcess::cancel()
{
    QProcess *proc = m_process;
    if (!proc || proc->state() == QProcess::NotRunning)
        return;

    // Disconnect all signals from proc to prevent onProcessFinished from
    // running inside waitForFinished's nested event loop, which would
    // deleteLater the QProcess while it's still on the call stack.
    proc->disconnect(this);
    m_process = nullptr;

    proc->terminate();
    if (!proc->waitForFinished(2000)) {
        proc->kill();
        proc->waitForFinished(1000);
    }

    int code = proc->exitCode();
    proc->deleteLater();
    emit finished(code);
}

bool ClaudeProcess::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void ClaudeProcess::rewindFiles(const QString &checkpointUuid)
{
    if (m_sessionId.isEmpty() || checkpointUuid.isEmpty()) {
        emit rewindCompleted(false);
        return;
    }

    auto *proc = new QProcess(this);
    if (!m_workingDir.isEmpty())
        proc->setWorkingDirectory(m_workingDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString pathVal = env.value("PATH");
    QStringList extraPaths = {
        QDir::homePath() + "/.local/bin",
        "/usr/local/bin",
        "/opt/homebrew/bin",
    };
    QDir nvmDir(QDir::homePath() + "/.nvm/versions/node");
    if (nvmDir.exists()) {
        QStringList versions = nvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!versions.isEmpty())
            extraPaths.prepend(nvmDir.absoluteFilePath(versions.last()) + "/bin");
    }
    for (const QString &p : extraPaths) {
        if (!pathVal.contains(p) && QDir(p).exists())
            pathVal = p + ":" + pathVal;
    }
    env.insert("PATH", pathVal);
    if (env.value("HOME").isEmpty())
        env.insert("HOME", QDir::homePath());
    env.insert("CLAUDE_CODE_ENABLE_SDK_FILE_CHECKPOINTING", "1");
    proc->setProcessEnvironment(env);

    QString claudeBin = Config::instance().claudeBinary();
    if (claudeBin == "claude" || claudeBin.isEmpty()) {
        QStringList searchDirs = {
            QDir::homePath() + "/.local/bin",
            "/usr/local/bin",
            "/opt/homebrew/bin",
        };
        for (const QString &dir : searchDirs) {
            QString candidate = dir + "/claude";
            if (QFile::exists(candidate)) {
                claudeBin = candidate;
                break;
            }
        }
    }

    QStringList args;
    args << "--resume" << m_sessionId
         << "--rewind-files" << checkpointUuid;

    qDebug() << "[cccpp] Rewinding:" << claudeBin << args;

    connect(proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
        QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
        QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        bool ok = (exitCode == 0);
        qDebug() << "[cccpp] Rewind exit:" << exitCode
                 << "stdout:" << out << "stderr:" << err;
        proc->deleteLater();
        emit rewindCompleted(ok);
    });

    proc->start(claudeBin, args);
    proc->closeWriteChannel();
}

QStringList ClaudeProcess::buildArguments(const QString &message) const
{
    QStringList args;
    args << "-p";
    args << "--input-format" << "stream-json";
    args << "--output-format" << "stream-json";
    args << "--verbose";
    args << "--include-partial-messages";
    args << "--replay-user-messages";

    if (!m_model.isEmpty())
        args << "--model" << m_model;

    if (!m_sessionId.isEmpty()) {
        args << "--resume" << m_sessionId;
    } else {
        // Force a fresh session to prevent auto-resuming old sessions from other projects
        args << "--session-id" << QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    if (m_mode == "ask") {
        args << "--permission-mode" << "bypassPermissions"
             << "--tools" << "Read,Glob,Grep";
    } else if (m_mode == "plan") {
        args << "--permission-mode" << "plan";
    } else {
        args << "--permission-mode" << "bypassPermissions"
             << "--allowedTools"
             << "Bash,Read,Edit,Write,Glob,Grep,Task";
    }

    return args;
}

void ClaudeProcess::onReadyReadStdout()
{
    m_stdoutBuffer.append(m_process->readAllStandardOutput());

    while (true) {
        int idx = m_stdoutBuffer.indexOf('\n');
        if (idx < 0)
            break;
        QByteArray line = m_stdoutBuffer.left(idx);
        m_stdoutBuffer.remove(0, idx + 1);
        if (!line.trimmed().isEmpty())
            m_parser->feed(line);
    }
}

void ClaudeProcess::onReadyReadStderr()
{
    QByteArray err = m_process->readAllStandardError();
    if (!err.trimmed().isEmpty())
        emit errorOccurred(QString::fromUtf8(err));
}

void ClaudeProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_stdoutBuffer.trimmed().isEmpty())
        m_parser->feed(m_stdoutBuffer);
    m_stdoutBuffer.clear();

    m_process->deleteLater();
    m_process = nullptr;
    emit finished(exitCode);
}
