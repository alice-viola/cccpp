# CCCPP — Claude Code C++ UI

<p align="center">
  <img src="assets/logo.png" alt="CCCPP Logo" width="200">
</p>

A native C++ desktop application that provides a Cursor-like three-column UI for [Claude Code](https://docs.claude.com/en/docs/claude-code/cli-reference). Built with Qt6 and QScintilla, it wraps the `claude` CLI to give you a visual interface for agentic coding sessions.

## Layout

```
┌──────────────┬─────────────────────┬──────────────────────┐
│  Workspace   │    Code Viewer      │    Agent Chat(s)     │
│  Tree        │                     │   ┌────┬────┬────┐   │
│              │  - Syntax highlight │   │Tab1│Tab2│Tab3│   │
│  - Browse    │  - Diff overlay     │   ├────┴────┴────┤   │
│    files     │  - Line numbers     │   │ Chat messages │   │
│              │  - Read-only        │   │ (streaming)   │   │
│              │                     │   ├──────────────┤   │
│              │                     │   │ [Agent|Ask|Plan] │
│              │                     │   │ [input] [Send]   │
└──────────────┴─────────────────────┴──────────────────────┘
```

## Features

### Chat & AI

- **Real-time streaming**: parses Claude Code's `stream-json` output for live, token-by-token response display
- **Multiple chat tabs**: run parallel conversations in separate tabs, each with its own session
- **Agent / Ask / Plan modes**: maps directly to Claude Code CLI flags — full tool access, read-only, or permission-controlled
- **Model selection**: switch between Claude models on the fly from the toolbar
- **Chat persistence**: SQLite database at `~/.cccpp/history.db` stores sessions and messages
- **Session resume**: uses `--resume SESSION_ID` to continue conversations across restarts
- **Chat history**: browse and reopen past sessions from the history menu
- **Thinking indicator**: animated bouncing-dot indicator while Claude is processing
- **Markdown rendering**: code blocks, bold, italic, headers, lists, links, blockquotes, and tables
- **Tool call display**: collapsible tool-call groups per turn with inline diffs for Edit/Write operations
- **Interactive questions**: answer Claude's follow-up questions directly in the UI with radio/checkbox options
- **Snapshot & revert**: takes git stash snapshots before each agent turn; revert button restores files

### Code Viewer

- **Syntax highlighting**: QScintilla-powered highlighting for 40+ languages — C/C++, Python, JavaScript/TypeScript, Rust, Go, Java, Ruby, Swift, Kotlin, HTML, CSS/SCSS, SQL, Bash, YAML, JSON, Markdown, and more
- **Multi-tab editor**: open multiple files with dirty-state tracking, close buttons, and tab reordering
- **Split diff view**: side-by-side old/new comparison with synchronized scrolling and hunk navigation
- **Diff markers**: parses Edit/Write tool events to show green/red line markers in the gutter
- **Markdown preview**: rendered HTML preview for `.md` files with theme-aware styling
- **External change detection**: watches for file modifications outside the app and prompts to reload
- **Standard editing**: undo/redo, cut/copy/paste, save single file or all files at once
- **QScintilla fallback**: gracefully falls back to QPlainTextEdit if QScintilla is not installed

### Integrated Terminal

- **Full terminal emulation**: libvterm-backed terminal with PTY support (macOS & Windows)
- **Multiple terminals**: open and manage several terminal tabs
- **Scrollback buffer**: up to 10,000 lines of history
- **Input method support**: IME integration for international input
- **Shell integration**: inherits your environment, PATH, and NVM setup

### Git Integration

- **Git panel**: stage, unstage, and discard changes on individual files or in bulk
- **Commit**: write commit messages and commit directly from the panel
- **File status**: color-coded indicators for modified, added, deleted, and untracked files
- **Branch display**: current branch shown in the status bar, auto-updates on branch changes
- **Diff viewing**: click any changed file to open a side-by-side split diff against HEAD
- **Workspace tree markers**: changed files show visual status dots in the file tree
- **Toast feedback**: success/failure notifications after commits

### Workspace & File Management

- **File tree**: hierarchical workspace browser with file/directory navigation
- **File status overlay**: git status colors and change-type markers on tree items
- **Click-to-open**: single-click loads files in the code viewer; click from chat navigates to the relevant line

### Themes

- **Four built-in themes**: Catppuccin Mocha, Macchiato, Frappé (dark), and Latte (light)
- **Runtime theme switching**: change themes from the menu without restarting
- **Unified design tokens**: consistent color palette, radii, spacing, and typography across all components

### UI & Layout

- **Three-column layout**: workspace tree + git panel | code viewer + terminal | chat panel
- **Panel toggling**: show/hide any panel from the toolbar
- **Resizable splitters**: drag to adjust column widths
- **Status bar**: displays current file, active model, git branch, and processing state
- **Toast notifications**: slide-in popups for success, error, info, and warning events
- **Animated focus ring**: input bar border animates on focus
- **Empty states**: custom illustrations when no files or chats are open

### Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `Ctrl+O` | Open Workspace |
| `Ctrl+N` | New Chat |
| `Ctrl+S` | Save File |
| `Ctrl+Shift+S` | Save All Files |
| `Ctrl+Z` / `Ctrl+Y` | Undo / Redo |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | Cut / Copy / Paste |
| `` Ctrl+` `` | Toggle Terminal |
| `` Ctrl+Shift+` `` | New Terminal |
| `Ctrl+Shift+G` | Refresh Git Status |
| `Ctrl+Q` | Quit |

## Requirements

- **Claude Code CLI** (`claude`) installed and authenticated
- **Qt6** (Core, Widgets, Sql)
- **QScintilla** (optional — falls back to QPlainTextEdit without it)
- **CMake** >= 3.20
- **C++17** compiler

## Build

```bash
# macOS with Homebrew
brew install qt@6 qscintilla2

mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH="$(brew --prefix qt);$(brew --prefix qscintilla2)"
cmake --build .

# Run
./cccpp /path/to/your/project
```

## Usage

1. **Open a workspace**: `File > Open Workspace` or pass a path as CLI argument
2. **Chat with Claude**: type in the input bar, press Enter to send
3. **Switch modes**: click Agent / Ask / Plan in the mode selector
4. **Browse files**: click files in the workspace tree to view them
5. **See diffs**: changed files show green dots in the tree; diff markers appear in the code viewer
6. **Revert changes**: click "Revert" on any agent turn to undo its file modifications
7. **Multiple sessions**: `File > New Chat` (Ctrl+N) for parallel conversations

## Configuration

Settings stored in `~/.cccpp/config.json`:

```json
{
  "claude_binary": "claude",
  "theme": "dark",
  "last_workspace": "/path/to/project"
}
```

## Architecture

The app wraps Claude Code CLI (`claude -p`) rather than calling the Anthropic API directly:

- Each user message spawns `claude -p "message" --resume SESSION_ID --output-format stream-json`
- `StreamParser` reads stdout line-by-line, parsing newline-delimited JSON events
- Text deltas stream into the chat panel; tool events feed the diff engine and snapshot manager
- `SnapshotManager` runs `git stash create` before each turn for atomic rollback

## Project Structure

```
src/
├── core/           # Process management, parsing, diff engine, DB, snapshots
├── ui/             # Qt widgets: MainWindow, WorkspaceTree, CodeViewer, ChatPanel
└── util/           # Markdown renderer, JSON helpers, config
```
