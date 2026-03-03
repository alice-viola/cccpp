#include "core/StreamParser.h"
#include <QDebug>

StreamParser::StreamParser(QObject *parent)
    : QObject(parent)
{
}

void StreamParser::reset()
{
    m_accumulatedText.clear();
    m_pendingTools.clear();
    m_emittedToolIds.clear();
    m_activeThinkingBlockIdx = -1;
}

void StreamParser::feed(const QByteArray &line)
{
    if (line.trimmed().isEmpty())
        return;

    json j;
    try {
        j = json::parse(line.begin(), line.end());
    } catch (const json::parse_error &) {
        return;
    }

    // (debug logging removed)

    StreamEvent event = parseEvent(j);

    switch (event.type) {
    case StreamEvent::TextDelta:
        m_accumulatedText += event.text;
        emit textDelta(event.text);
        break;
    case StreamEvent::ToolUse:
        qDebug() << "[stream] toolUse:" << event.toolName << "id=" << event.toolId;
        emit toolUseStarted(event.toolName, event.toolId, event.toolInput);
        break;
    case StreamEvent::ToolResult:
        qDebug() << "[stream] toolResult len=" << event.toolResultContent.size();
        emit toolResultReceived(event.toolResultContent);
        break;
    case StreamEvent::Result:
        qDebug() << "[stream] result  session=" << event.sessionId;
        emit resultReady(event.sessionId, event.raw);
        break;
    case StreamEvent::Error:
        qWarning() << "[stream] error:" << event.text;
        emit errorOccurred(event.text);
        break;
    default:
        break;
    }
}

static QString jstr(const json &j, const std::string &key)
{
    if (j.contains(key) && j[key].is_string())
        return QString::fromStdString(j[key].get<std::string>());
    return {};
}

static std::string jtype(const json &j)
{
    if (j.contains("type") && j["type"].is_string())
        return j["type"].get<std::string>();
    return {};
}

static int jint(const json &j, const std::string &key, int def = -1)
{
    if (j.contains(key) && j[key].is_number())
        return j[key].get<int>();
    return def;
}

void StreamParser::handleInnerEvent(const json &ev)
{
    std::string evType = jtype(ev);

    // --- Text streaming ---
    if (evType == "content_block_delta") {
        if (ev.contains("delta")) {
            std::string dt = jtype(ev["delta"]);
            if (dt == "text_delta") {
                StreamEvent event;
                event.type = StreamEvent::TextDelta;
                event.text = jstr(ev["delta"], "text");
                m_accumulatedText += event.text;
                emit textDelta(event.text);
                emit eventParsed(event);
                return;
            }
            if (dt == "input_json_delta") {
                int idx = jint(ev, "index");
                if (m_pendingTools.contains(idx)) {
                    std::string partial;
                    if (ev["delta"].contains("partial_json") && ev["delta"]["partial_json"].is_string())
                        partial = ev["delta"]["partial_json"].get<std::string>();
                    m_pendingTools[idx].accumulatedJson += partial;
                    processPartialToolJson(idx, partial);
                }
                return;
            }
            if (dt == "thinking_delta") {
                QString text = jstr(ev["delta"], "thinking");
                if (!text.isEmpty())
                    emit thinkingDelta(text);
                return;
            }
            if (dt == "signature_delta") {
                return;
            }
        }
        return;
    }

    if (evType == "content_block_start") {
        if (ev.contains("content_block")) {
            std::string bt = jtype(ev["content_block"]);
            int idx = jint(ev, "index");
            if (bt == "tool_use" && idx >= 0) {
                PendingToolUse pending;
                pending.name = jstr(ev["content_block"], "name");
                pending.id = jstr(ev["content_block"], "id");
                pending.blockIndex = idx;
                m_pendingTools[idx] = pending;
                qDebug() << "[stream] block_start tool_use:" << pending.name << "idx=" << idx;
            } else if (bt == "thinking") {
                m_activeThinkingBlockIdx = idx;
                qDebug() << "[stream] block_start thinking idx=" << idx;
                emit thinkingStarted();
            }
        }
        return;
    }

    if (evType == "content_block_stop") {
        int idx = jint(ev, "index");
        qDebug() << "[stream] block_stop idx=" << idx;

        if (idx == m_activeThinkingBlockIdx) {
            m_activeThinkingBlockIdx = -1;
            emit thinkingStopped();
            return;
        }

        if (m_pendingTools.contains(idx)) {
            PendingToolUse &pending = m_pendingTools[idx];

            bool wasStreaming = pending.inNewString || pending.pathEmitted;
            if (wasStreaming)
                emit editStreamFinished(idx);

            if (!pending.id.isEmpty() && !m_emittedToolIds.contains(pending.id)
                && !pending.accumulatedJson.empty()) {
                json toolInput;
                try {
                    toolInput = json::parse(pending.accumulatedJson);
                } catch (...) {
                    toolInput = json::object();
                }

                m_emittedToolIds.insert(pending.id);
                StreamEvent event;
                event.type = StreamEvent::ToolUse;
                event.toolName = pending.name;
                event.toolId = pending.id;
                event.toolInput = toolInput;
                emit toolUseStarted(pending.name, pending.id, toolInput);
                emit eventParsed(event);
            }

            m_pendingTools.remove(idx);
        }
        return;
    }

    // message_start, message_delta, message_stop — no action needed
}

StreamEvent StreamParser::parseEvent(const json &j)
{
    StreamEvent event;
    event.raw = j;

    std::string type = jtype(j);

    // ---- "system" init event ----
    if (type == "system") {
        event.type = StreamEvent::Result;
        event.sessionId = jstr(j, "session_id");
        qDebug() << "[stream] event type=system session=" << event.sessionId;
        return event;
    }

    // ---- "result" event (final) ----
    if (type == "result") {
        event.type = StreamEvent::Result;
        event.sessionId = jstr(j, "session_id");
        qDebug() << "[stream] event type=result (final) session=" << event.sessionId;
        return event;
    }

    // ---- "error" ----
    if (type == "error") {
        event.type = StreamEvent::Error;
        if (j.contains("error"))
            event.text = jstr(j["error"], "message");
        if (event.text.isEmpty())
            event.text = QString::fromStdString(j.dump());
        return event;
    }

    // ---- "stream_event" wrapper ----
    if (type == "stream_event" && j.contains("event")) {
        handleInnerEvent(j["event"]);
        event.type = StreamEvent::Unknown; // Already handled
        return event;
    }

    // ---- "user" message with checkpoint UUID (from --replay-user-messages) ----
    // Only the replayed user prompt (isReplay=true) carries the checkpoint UUID.
    // Tool result events also arrive as type "user" but have no file checkpoint.
    if (type == "user") {
        bool isReplay = j.contains("isReplay") && j["isReplay"].is_boolean()
                        && j["isReplay"].get<bool>();
        if (isReplay) {
            QString uuid = jstr(j, "uuid");
            if (!uuid.isEmpty())
                emit checkpointReceived(uuid);
        }
        event.type = StreamEvent::Unknown;
        return event;
    }

    // ---- "assistant" snapshot message ----
    // Contains accumulated content. We use it ONLY for tool_use blocks we haven't seen.
    // Text blocks are already handled by content_block_delta streaming.
    if (type == "assistant") {
        json content;
        if (j.contains("message") && j["message"].contains("content"))
            content = j["message"]["content"];

        if (content.is_array()) {
            for (const auto &block : content) {
                std::string btype = jtype(block);
                if (btype == "tool_use") {
                    QString toolId = jstr(block, "id");
                    if (!toolId.isEmpty() && !m_emittedToolIds.contains(toolId)) {
                        m_emittedToolIds.insert(toolId);
                        event.type = StreamEvent::ToolUse;
                        event.toolName = jstr(block, "name");
                        event.toolId = toolId;
                        if (block.contains("input"))
                            event.toolInput = block["input"];
                        return event;
                    }
                }
            }
        }

        // Capture session_id
        if (!jstr(j, "session_id").isEmpty()) {
            event.type = StreamEvent::Result;
            event.sessionId = jstr(j, "session_id");
            return event;
        }

        event.type = StreamEvent::Unknown;
        return event;
    }

    // ---- Tool result ----
    if (type == "tool_result") {
        event.type = StreamEvent::ToolResult;
        if (j.contains("content"))
            event.toolResultContent = QString::fromStdString(j["content"].dump());
        return event;
    }

    event.type = StreamEvent::Unknown;
    return event;
}

static std::string unescapeJsonString(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            char c = s[i + 1];
            if (c == '"')       { out += '"'; ++i; }
            else if (c == '\\') { out += '\\'; ++i; }
            else if (c == 'n')  { out += '\n'; ++i; }
            else if (c == 't')  { out += '\t'; ++i; }
            else if (c == 'r')  { out += '\r'; ++i; }
            else if (c == '/')  { out += '/'; ++i; }
            else                { out += s[i]; }
        } else {
            out += s[i];
        }
    }
    return out;
}

static size_t findJsonFieldStart(const std::string &json, const std::string &field, size_t from)
{
    std::string needle = "\"" + field + "\":\"";
    size_t pos = json.find(needle, from);
    if (pos != std::string::npos)
        return pos + needle.size();

    needle = "\"" + field + "\": \"";
    pos = json.find(needle, from);
    if (pos != std::string::npos)
        return pos + needle.size();

    return std::string::npos;
}

static std::string extractFieldValue(const std::string &json, size_t fieldStart)
{
    std::string value;
    for (size_t i = fieldStart; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            value += json[i];
            value += json[i + 1];
            ++i;
        } else if (json[i] == '"') {
            break;
        } else {
            value += json[i];
        }
    }
    return value;
}

void StreamParser::processPartialToolJson(int idx, const std::string &)
{
    PendingToolUse &p = m_pendingTools[idx];
    bool isEdit = (p.name == "Edit" || p.name == "StrReplace" || p.name == "Write");
    if (!isEdit)
        return;

    const std::string &json = p.accumulatedJson;

    if (!p.pathEmitted) {
        size_t pathStart = findJsonFieldStart(json, "path", 0);
        if (pathStart == std::string::npos)
            pathStart = findJsonFieldStart(json, "file_path", 0);
        if (pathStart != std::string::npos) {
            std::string rawVal = extractFieldValue(json, pathStart);
            if (json.find('"', pathStart + rawVal.size()) != std::string::npos) {
                p.extractedPath = unescapeJsonString(rawVal);
                p.pathEmitted = true;
                emit editStreamStarted(p.name, QString::fromStdString(p.extractedPath));
            }
        }
    }

    if (p.pathEmitted && !p.inNewString) {
        std::string fieldName = (p.name == "Write") ? "content" : "new_string";
        size_t start = findJsonFieldStart(json, fieldName, 0);
        if (start == std::string::npos && p.name == "Write")
            start = findJsonFieldStart(json, "contents", 0);
        if (start != std::string::npos) {
            p.inNewString = true;
            p.lastScanPos = start;
        }
    }

    if (p.inNewString) {
        std::string rawVal = extractFieldValue(json, p.lastScanPos);
        std::string decoded = unescapeJsonString(rawVal);
        if (decoded.size() > p.newStringBuffer.size()) {
            std::string delta = decoded.substr(p.newStringBuffer.size());
            p.newStringBuffer = decoded;
            emit editContentDelta(idx, QString::fromStdString(delta));
        }
    }
}
