#include "core/ClaudeProcess.h"
#include "core/StreamParser.h"
#include "util/Config.h"
#include <nlohmann/json.hpp>
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

void ClaudeProcess::setSystemPrompt(const QString &prompt)
{
    m_systemPrompt = prompt;
}

void ClaudeProcess::setProfileIds(const QStringList &ids)
{
    m_profileIds = ids;
}

void ClaudeProcess::setAgentName(const QString &name) { m_agentName = name; }
void ClaudeProcess::setTeamId(const QString &teamId) { m_teamId = teamId; }

QProcessEnvironment ClaudeProcess::buildProcessEnvironment() const
{
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

    // Peer messaging: inject agent identity (inherited by MCP server subprocess)
    if (!m_agentName.isEmpty())
        env.insert("CCCPP_AGENT_NAME", m_agentName);
    if (!m_teamId.isEmpty())
        env.insert("CCCPP_TEAM_ID", m_teamId);

    return env;
}

QString ClaudeProcess::resolveClaudeBinary() const
{
    QString claudeBin = Config::instance().claudeBinary();
    if (claudeBin == "claude" || claudeBin.isEmpty()) {
        QStringList searchDirs = {
            QDir::homePath() + "/.local/bin",
            "/usr/local/bin",
            "/opt/homebrew/bin",
        };
        for (const QString &dir : searchDirs) {
            QString candidate = dir + "/claude";
            if (QFile::exists(candidate))
                return candidate;
        }
    }
    return claudeBin;
}

void ClaudeProcess::sendMessage(const QString &message,
                                const QList<QPair<QByteArray, QString>> &images)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        emit errorOccurred("Process already running");
        return;
    }

    m_parser->reset();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();

    m_process = new QProcess(this);
    if (!m_workingDir.isEmpty())
        m_process->setWorkingDirectory(m_workingDir);

    m_process->setProcessEnvironment(buildProcessEnvironment());

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
    connect(m_process, &QProcess::started, this, [this] {
        QString info = QStringLiteral("[cccpp] Claude process started | model=%1")
            .arg(m_model.isEmpty() ? "default" : m_model);
        if (!m_profileIds.isEmpty())
            info += QStringLiteral(" | profiles=[%1]").arg(m_profileIds.join(", "));
        qDebug().noquote() << info;
    });

    QString claudeBin = resolveClaudeBinary();
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
        return;
    }

    nlohmann::json contentArray = nlohmann::json::array();
    for (const auto &img : images) {
        contentArray.push_back({
            {"type", "image"},
            {"source", {
                {"type", "base64"},
                {"media_type", img.second.toStdString()},
                {"data", img.first.toBase64().toStdString()}
            }}
        });
    }
    contentArray.push_back({
        {"type", "text"},
        {"text", message.toUtf8().toStdString()}
    });

    nlohmann::json envelope = {
        {"type", "user"},
        {"message", {
            {"role", "user"},
            {"content", contentArray}
        }}
    };
    QByteArray jsonMsg = QByteArray::fromStdString(envelope.dump()) + "\n";
    m_process->write(jsonMsg);
    m_process->closeWriteChannel();

    emit started();
}

void ClaudeProcess::sendToolResult(const QString &toolUseId, const QString &content)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        emit errorOccurred("Process already running");
        return;
    }

    m_parser->reset();
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();

    m_process = new QProcess(this);
    if (!m_workingDir.isEmpty())
        m_process->setWorkingDirectory(m_workingDir);

    m_process->setProcessEnvironment(buildProcessEnvironment());

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ClaudeProcess::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &ClaudeProcess::onReadyReadStderr);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &ClaudeProcess::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        QString msg;
        switch (err) {
        case QProcess::FailedToStart: msg = "Failed to start 'claude'. Is it installed and in your PATH?"; break;
        case QProcess::Crashed:       msg = "Claude process crashed."; break;
        default:                      msg = "Process error."; break;
        }
        emit errorOccurred(msg);
    });

    QString claudeBin = resolveClaudeBinary();
    // Use an empty placeholder message for buildArguments (only session/model flags matter)
    QStringList args = buildArguments(QString());
    qDebug() << "[cccpp] sendToolResult: starting" << claudeBin;

    QString stdbuf = "/usr/bin/stdbuf";
    if (!QFile::exists(stdbuf))
        stdbuf = "/opt/homebrew/bin/stdbuf";

    if (QFile::exists(stdbuf)) {
        m_process->start(stdbuf, QStringList() << "-oL" << claudeBin << args);
    } else {
        m_process->start(claudeBin, args);
    }

    if (!m_process->waitForStarted(5000)) {
        emit errorOccurred(QStringLiteral("Process failed to start: %1").arg(claudeBin));
        return;
    }

    nlohmann::json envelope = {
        {"type", "user"},
        {"message", {
            {"role", "user"},
            {"content", nlohmann::json::array({
                {
                    {"type", "tool_result"},
                    {"tool_use_id", toolUseId.toStdString()},
                    {"content", content.toUtf8().toStdString()}
                }
            })}
        }}
    };
    QByteArray jsonMsg = QByteArray::fromStdString(envelope.dump()) + "\n";
    m_process->write(jsonMsg);
    m_process->closeWriteChannel();

    emit started();
}

void ClaudeProcess::cancel()
{
    QProcess *proc = m_process;
    if (!proc || proc->state() == QProcess::NotRunning) {
        qDebug() << "[cccpp] cancel() called but process not running";
        return;
    }

    qDebug() << "[cccpp] cancel() terminating process pid=" << proc->processId();

    // Disconnect all signals from proc to prevent onProcessFinished from
    // running inside waitForFinished's nested event loop, which would
    // deleteLater the QProcess while it's still on the call stack.
    proc->disconnect(this);
    m_process = nullptr;

    proc->terminate();
    if (!proc->waitForFinished(2000)) {
        qDebug() << "[cccpp] cancel() terminate timeout, killing";
        proc->kill();
        proc->waitForFinished(1000);
    }

    int code = proc->exitCode();
    proc->deleteLater();
    qDebug() << "[cccpp] cancel() done, emitting finished(" << code << ")";
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

    proc->setProcessEnvironment(buildProcessEnvironment());

    QString claudeBin = resolveClaudeBinary();
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

    if (!m_systemPrompt.isEmpty())
        args << "--append-system-prompt" << m_systemPrompt;

    if (m_mode == "orchestrator") {
        // Orchestrator: ONLY MCP orchestrator tools, max 1 agentic turn.
        // Claude calls delegate/validate/done/fail as structured tool calls.
        // The MCP server acknowledges; our C++ code intercepts via toolUseStarted.
        args << "--permission-mode" << "bypassPermissions"
             << "--allowedTools" << "mcp__c3p2-orchestrator__delegate,"
                                    "mcp__c3p2-orchestrator__validate,"
                                    "mcp__c3p2-orchestrator__done,"
                                    "mcp__c3p2-orchestrator__fail"
             << "--max-turns" << "1";
    } else if (m_mode == "ask") {
        args << "--permission-mode" << "bypassPermissions"
             << "--tools" << "Read,Glob,Grep";
    } else if (m_mode == "plan") {
        args << "--permission-mode" << "plan";
    } else {
        QString allowedTools = "Bash,Read,Edit,Write,Glob,Grep,Task,AskUserQuestion";
        // Add peer messaging tools when agent is part of a team
        if (!m_teamId.isEmpty())
            allowedTools += ",mcp__c3p2-orchestrator__send_message"
                            ",mcp__c3p2-orchestrator__check_inbox";
        args << "--permission-mode" << "bypassPermissions"
             << "--allowedTools" << allowedTools;
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
    m_stderrBuffer.append(m_process->readAllStandardError());
}

void ClaudeProcess::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    qDebug() << "[cccpp] onProcessFinished  exit=" << exitCode
             << (status == QProcess::NormalExit ? "Normal" : "Crash")
             << "bufferRemaining=" << m_stdoutBuffer.size() << "bytes";

    if (!m_stdoutBuffer.trimmed().isEmpty())
        m_parser->feed(m_stdoutBuffer);
    m_stdoutBuffer.clear();

    if (exitCode != 0 && !m_stderrBuffer.trimmed().isEmpty()) {
        qWarning() << "[cccpp] stderr:" << m_stderrBuffer.trimmed();
        emit errorOccurred(QString::fromUtf8(m_stderrBuffer.trimmed()));
    }
    m_stderrBuffer.clear();

    m_process->deleteLater();
    m_process = nullptr;
    qDebug() << "[cccpp] emitting finished(" << exitCode << ")";
    emit finished(exitCode);
}
