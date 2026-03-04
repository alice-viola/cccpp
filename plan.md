# Plan: Chat Rename & Favorite Feature

## Overview
Add two features to agent cards:
1. **Rename** — custom chat name, persisted to DB
2. **Favorite** — toggle a "favorite" flag, shown as a star icon on the card

Both triggered via right-click context menu on agent cards.

---

## 1. Data Model Changes

### `SessionInfo` (SessionManager.h)
- Add `bool favorite = false;` field

### `AgentSummary` (AgentFleetPanel.h)
- Add `bool favorite = false;` field

### `Database` schema (Database.cpp)
- Add `favorite` column to `sessions` table: `ALTER TABLE sessions ADD COLUMN favorite INTEGER DEFAULT 0`
- Add migration in `createTables()` — use `ALTER TABLE ... ADD COLUMN` with `IF NOT EXISTS` workaround (try ALTER, ignore error if column exists)
- Update `saveSession()`, `loadSession()`, `loadSessions()` to read/write `favorite`

### `SessionManager`
- Add `setSessionFavorite(sessionId, bool)` method — updates in-memory cache + emits `sessionUpdated`

---

## 2. AgentCard UI Changes (AgentFleetPanel.h/.cpp)

### New member
- `bool m_favorite = false;` on `AgentCard`

### Update `AgentSummary` → `AgentCard::update()`
- Read `summary.favorite` → `m_favorite`

### `paintEvent()` — star icon
- When `m_favorite` is true, draw a small filled star (yellow `#f9e2af`) to the left of the title text, after the status dot
- Shift title text right by ~14px when star is present

### Right-click context menu
- Override `contextMenuEvent(QContextMenuEvent*)` on `AgentCard`
- Menu items:
  - **"Rename…"** → emits new signal `renameRequested(sessionId)`
  - **"Favorite" / "Unfavorite"** → emits new signal `favoriteToggled(sessionId, bool)`
  - Separator
  - **"Delete"** → emits existing `deleteRequested(sessionId)`

### New signals on `AgentCard`
- `renameRequested(const QString &sessionId)`
- `favoriteToggled(const QString &sessionId, bool favorite)`

---

## 3. AgentFleetPanel Changes

### New signals
- `renameRequested(const QString &sessionId)`
- `favoriteToggled(const QString &sessionId, bool favorite)`

### Wire card signals in `rebuild()`
- Connect `card->renameRequested` → `this->renameRequested`
- Connect `card->favoriteToggled` → `this->favoriteToggled`

---

## 4. ChatPanel Changes

### `agentSummaries()`
- Populate `s.favorite` from `SessionInfo` (for closed sessions from DB) and from a new `ChatTab::favorite` field (for open tabs)

### `ChatTab` struct
- Add `bool favorite = false;`

---

## 5. MainWindow Wiring

### Rename flow
- Connect `AgentFleetPanel::renameRequested` → lambda that:
  1. Shows `QInputDialog::getText()` with current title
  2. If accepted, calls `SessionManager::setSessionTitle(sessionId, newTitle)`
  3. Calls `Database::saveSession(updatedInfo)`
  4. Updates the open `ChatTab` title if the session is open
  5. Calls `rebuildFleetPanel()` to refresh the card

### Favorite flow
- Connect `AgentFleetPanel::favoriteToggled` → lambda that:
  1. Calls `SessionManager::setSessionFavorite(sessionId, newState)`
  2. Calls `Database::saveSession(updatedInfo)`
  3. Updates the open `ChatTab::favorite` if session is open
  4. Calls `rebuildFleetPanel()` to refresh the card

---

## Files Modified
1. `src/core/SessionManager.h` — add `favorite` to `SessionInfo`, add `setSessionFavorite()`
2. `src/core/SessionManager.cpp` — implement `setSessionFavorite()`
3. `src/core/Database.cpp` — schema migration, update save/load queries
4. `src/ui/AgentFleetPanel.h` — add `favorite` to `AgentSummary`, new signals/members on `AgentCard` and `AgentFleetPanel`
5. `src/ui/AgentFleetPanel.cpp` — context menu, star painting, signal wiring
6. `src/ui/ChatPanel.h` — add `favorite` to `ChatTab`
7. `src/ui/ChatPanel.cpp` — populate `favorite` in `agentSummaries()`
8. `src/ui/MainWindow.cpp` — wire rename/favorite signals, show dialog, persist
