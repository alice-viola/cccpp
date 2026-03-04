#!/usr/bin/env python3
"""
MCP server for C3P2 orchestrator — zero dependencies (stdlib only).

Exposes 4 tools over JSON-RPC 2.0 / stdio:
  delegate  — Delegate work to a specialist agent
  validate  — Run a shell command to check results
  done      — Report goal achieved
  fail      — Report unrecoverable failure

The C++ app intercepts tool calls from the stream-json output.
This server just acknowledges them so the Claude CLI round-trip completes.
"""
import json
import sys

TOOLS = [
    {
        "name": "delegate",
        "description": (
            "Delegate a task to a specialist agent. "
            "The agent will execute autonomously and return results."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "role": {
                    "type": "string",
                    "enum": ["architect", "implementer", "reviewer", "tester"],
                    "description": (
                        "Specialist role: architect (designs/plans), "
                        "implementer (writes code), reviewer (reviews code), "
                        "tester (writes/runs tests)"
                    ),
                },
                "task": {
                    "type": "string",
                    "description": (
                        "Detailed task description for the specialist. "
                        "Be specific about what to do, which files to touch, "
                        "and what the expected output should be."
                    ),
                },
            },
            "required": ["role", "task"],
            "additionalProperties": False,
        },
    },
    {
        "name": "validate",
        "description": (
            "Run a shell command to validate results (build, test, lint, etc). "
            "Returns the command output and exit code."
        ),
        "inputSchema": {
            "type": "object",
            "properties": {
                "command": {
                    "type": "string",
                    "description": "Shell command to execute (e.g. 'cmake --build build', 'npm test')",
                },
                "description": {
                    "type": "string",
                    "description": "Human-readable description of what this validates",
                },
            },
            "required": ["command"],
            "additionalProperties": False,
        },
    },
    {
        "name": "done",
        "description": "Report that the goal has been fully achieved. Only call this after implementation, validation, and review are all complete.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "summary": {
                    "type": "string",
                    "description": "Summary of what was accomplished",
                },
            },
            "required": ["summary"],
            "additionalProperties": False,
        },
    },
    {
        "name": "fail",
        "description": "Report that the goal cannot be achieved. Only use for truly unrecoverable failures.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "reason": {
                    "type": "string",
                    "description": "Why the goal cannot be completed",
                },
            },
            "required": ["reason"],
            "additionalProperties": False,
        },
    },
]

# Tool call acknowledgments — Claude sees these as tool results
TOOL_ACKS = {
    "delegate": "Command received. Delegation will be executed and results provided in your next message.",
    "validate": "Command received. Validation will be executed and results provided in your next message.",
    "done": "Orchestration marked as complete.",
    "fail": "Orchestration marked as failed.",
}


def send(msg):
    """Write a JSON-RPC message to stdout."""
    sys.stdout.write(json.dumps(msg, separators=(",", ":")) + "\n")
    sys.stdout.flush()


def reply(req_id, result):
    """Send a JSON-RPC success response."""
    send({"jsonrpc": "2.0", "id": req_id, "result": result})


def reply_error(req_id, code, message):
    """Send a JSON-RPC error response."""
    send({"jsonrpc": "2.0", "id": req_id, "error": {"code": code, "message": message}})


def main():
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            msg = json.loads(line)
        except json.JSONDecodeError:
            continue

        method = msg.get("method", "")
        req_id = msg.get("id")  # None for notifications

        # ─── Lifecycle ────────────────────────────────────────────
        if method == "initialize":
            params = msg.get("params", {})
            reply(req_id, {
                "protocolVersion": params.get("protocolVersion", "2025-11-25"),
                "capabilities": {"tools": {}},
                "serverInfo": {"name": "c3p2-orchestrator", "version": "0.1.0"},
            })

        elif method in ("notifications/initialized", "notifications/cancelled"):
            pass  # notifications — no response

        # ─── Tool Discovery ───────────────────────────────────────
        elif method == "tools/list":
            reply(req_id, {"tools": TOOLS})

        # ─── Tool Execution ───────────────────────────────────────
        elif method == "tools/call":
            params = msg.get("params", {})
            name = params.get("name", "")
            ack = TOOL_ACKS.get(name, f"Acknowledged: {name}")
            reply(req_id, {
                "content": [{"type": "text", "text": ack}],
            })

        # ─── Other Standard Methods ───────────────────────────────
        elif method == "ping":
            reply(req_id, {})

        elif method in ("resources/list", "resources/templates/list"):
            reply(req_id, {"resources": []})

        elif method == "prompts/list":
            reply(req_id, {"prompts": []})

        # ─── Unknown ──────────────────────────────────────────────
        elif req_id is not None:
            reply_error(req_id, -32601, f"Method not found: {method}")
        # else: unknown notification — silently ignore


if __name__ == "__main__":
    main()
