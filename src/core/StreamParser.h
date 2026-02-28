#pragma once

#include <QObject>
#include <QString>
#include <QMap>
#include <QSet>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct StreamEvent {
    enum Type {
        TextDelta,
        ToolUse,
        ToolResult,
        Result,
        Error,
        Unknown
    };

    Type type = Unknown;
    QString text;
    QString toolName;
    QString toolId;
    json toolInput;
    QString toolResultContent;
    QString sessionId;
    json raw;
};

struct PendingToolUse {
    QString name;
    QString id;
    int blockIndex = -1;
    std::string accumulatedJson;
};

class StreamParser : public QObject {
    Q_OBJECT
public:
    explicit StreamParser(QObject *parent = nullptr);

    void feed(const QByteArray &line);
    void reset();

signals:
    void textDelta(const QString &text);
    void toolUseStarted(const QString &toolName, const json &input);
    void toolResultReceived(const QString &content);
    void resultReady(const QString &sessionId, const json &result);
    void errorOccurred(const QString &message);
    void eventParsed(const StreamEvent &event);

private:
    StreamEvent parseEvent(const json &j);
    void handleInnerEvent(const json &ev);

    QString m_accumulatedText;
    QMap<int, PendingToolUse> m_pendingTools; // block index -> pending tool
    QSet<QString> m_emittedToolIds;
};
