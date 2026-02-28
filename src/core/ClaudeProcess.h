#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class StreamParser;

class ClaudeProcess : public QObject {
    Q_OBJECT
public:
    explicit ClaudeProcess(QObject *parent = nullptr);
    ~ClaudeProcess();

    void setWorkingDirectory(const QString &dir);
    void setSessionId(const QString &id);
    void setMode(const QString &mode); // "agent", "ask", "plan"

    void sendMessage(const QString &message);
    void cancel();
    bool isRunning() const;

    StreamParser *streamParser() const { return m_parser; }
    QString sessionId() const { return m_sessionId; }

signals:
    void started();
    void finished(int exitCode);
    void errorOccurred(const QString &error);

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QStringList buildArguments(const QString &message) const;

    QProcess *m_process = nullptr;
    StreamParser *m_parser = nullptr;
    QString m_workingDir;
    QString m_sessionId;
    QString m_mode = "agent";
    QByteArray m_stdoutBuffer;
};
