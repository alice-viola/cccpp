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
        // Dedup is handled in parseEvent/handleInnerEvent — just emit here
        emit toolUseStarted(event.toolName, event.toolInput);
        break;
    case StreamEvent::ToolResult:
        emit toolResultReceived(event.toolResultContent);
        break;
    case StreamEvent::Result:
        emit resultReady(event.sessionId, event.raw);
        break;
    case StreamEvent::Error:
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
                // Accumulate partial tool input JSON
                int idx = jint(ev, "index");
                if (m_pendingTools.contains(idx)) {
                    std::string partial;
                    if (ev["delta"].contains("partial_json") && ev["delta"]["partial_json"].is_string())
                        partial = ev["delta"]["partial_json"].get<std::string>();
                    m_pendingTools[idx].accumulatedJson += partial;
                }
                return;
            }
            if (dt == "thinking_delta" || dt == "signature_delta") {
                return; // Skip thinking/signature blocks
            }
        }
        return;
    }

    // --- Tool use block start (input is empty, will be built up by input_json_delta) ---
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
            }
        }
        return;
    }

    // --- Block stop: finalize pending tool_use ---
    if (evType == "content_block_stop") {
        int idx = jint(ev, "index");
        if (m_pendingTools.contains(idx)) {
            PendingToolUse &pending = m_pendingTools[idx];

            // Only emit from here if the assistant snapshot hasn't already handled it
            // AND we have accumulated JSON input (not empty)
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
                emit toolUseStarted(pending.name, toolInput);
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
        return event;
    }

    // ---- "result" event (final) ----
    if (type == "result") {
        event.type = StreamEvent::Result;
        event.sessionId = jstr(j, "session_id");
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
