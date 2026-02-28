#include "ui/TerminalWidget.h"
#include "ui/ThemeManager.h"
#include "core/PtyProcess.h"
#include <QPainter>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <QFontMetrics>
#include <QApplication>
#include <QClipboard>
#include <QScrollBar>
#include <QDebug>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TerminalWidget::TerminalWidget(QWidget *parent)
    : QWidget(parent)
    , m_font("Menlo", 13)
{
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_InputMethodEnabled, true);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    QFontMetrics fm(m_font);
    m_cellWidth = fm.horizontalAdvance('M');
    m_cellHeight = fm.height();

    m_cursorTimer.setInterval(530);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this] {
        m_cursorBlinkState = !m_cursorBlinkState;
        update();
    });
}

TerminalWidget::~TerminalWidget()
{
    if (m_pty) {
        m_pty->terminate();
        delete m_pty;
    }
    destroyVterm();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TerminalWidget::startShell(const QString &workingDir)
{
    recalculateGridSize();
    if (m_rows < 1) m_rows = 24;
    if (m_cols < 1) m_cols = 80;

    initVterm(m_rows, m_cols);

    m_pty = PtyProcess::create(this);
    connect(m_pty, &PtyProcess::dataReceived, this, [this](const QByteArray &data) {
        if (m_vterm) {
            vterm_input_write(m_vterm, data.constData(), data.size());
            // If scrolled up and new output arrives, snap to bottom
            if (m_scrollOffset > 0)
                m_scrollOffset = 0;
            update();
        }
    });
    connect(m_pty, &PtyProcess::finished, this, [this](int code) {
        emit shellFinished(code);
    });

    // Detect default shell
    QString shell;
#ifdef _WIN32
    shell = "powershell.exe";
#else
    shell = qEnvironmentVariable("SHELL", "/bin/bash");
#endif

    QStringList args;
#ifndef _WIN32
    args << "-l"; // login shell
#endif

    QStringList env;
    env << "TERM=xterm-256color";
    env << QStringLiteral("COLORTERM=truecolor");
    env << QStringLiteral("LANG=%1").arg(qEnvironmentVariable("LANG", "en_US.UTF-8"));

    m_pty->resize(m_rows, m_cols);
    m_pty->start(shell, args, workingDir, env);
    m_cursorTimer.start();
    setFocus();
}

void TerminalWidget::writeToPty(const QByteArray &data)
{
    if (m_pty)
        m_pty->write(data);
}

bool TerminalWidget::isRunning() const
{
    return m_pty && m_pty->isRunning();
}

QSize TerminalWidget::sizeHint() const
{
    return QSize(80 * m_cellWidth, 24 * m_cellHeight);
}

// ---------------------------------------------------------------------------
// libvterm initialization
// ---------------------------------------------------------------------------

void TerminalWidget::initVterm(int rows, int cols)
{
    destroyVterm();

    m_vterm = vterm_new(rows, cols);
    vterm_set_utf8(m_vterm, 1);

    // Output callback — libvterm sends data to the PTY
    vterm_output_set_callback(m_vterm, &TerminalWidget::onOutput, this);

    m_vtermScreen = vterm_obtain_screen(m_vterm);

    static VTermScreenCallbacks screenCbs = {};
    screenCbs.damage       = &TerminalWidget::onDamage;
    screenCbs.movecursor   = &TerminalWidget::onMoveCursor;
    screenCbs.settermprop  = &TerminalWidget::onSetTermProp;
    screenCbs.bell         = &TerminalWidget::onBell;
    screenCbs.sb_pushline  = &TerminalWidget::onSbPushLine;
    screenCbs.sb_popline   = &TerminalWidget::onSbPopLine;

    vterm_screen_set_callbacks(m_vtermScreen, &screenCbs, this);
    vterm_screen_reset(m_vtermScreen, 1);

    // Set default colors to match the app's theme
    const auto &pal = ThemeManager::instance().palette();
    VTermColor fg, bg;
    vterm_color_rgb(&fg, pal.text_primary.red(), pal.text_primary.green(), pal.text_primary.blue());
    vterm_color_rgb(&bg, pal.bg_base.red(), pal.bg_base.green(), pal.bg_base.blue());
    vterm_state_set_default_colors(vterm_obtain_state(m_vterm), &fg, &bg);
}

void TerminalWidget::destroyVterm()
{
    if (m_vterm) {
        vterm_free(m_vterm);
        m_vterm = nullptr;
        m_vtermScreen = nullptr;
    }
}

void TerminalWidget::recalculateGridSize()
{
    if (m_cellWidth <= 0 || m_cellHeight <= 0)
        return;
    m_cols = qMax(1, width() / m_cellWidth);
    m_rows = qMax(1, height() / m_cellHeight);
}

// ---------------------------------------------------------------------------
// Color conversion
// ---------------------------------------------------------------------------

QColor TerminalWidget::vtermColorToQColor(VTermColor c) const
{
    if (VTERM_COLOR_IS_DEFAULT_FG(&c))
        return ThemeManager::instance().palette().text_primary;
    if (VTERM_COLOR_IS_DEFAULT_BG(&c))
        return ThemeManager::instance().palette().bg_base;

    if (VTERM_COLOR_IS_INDEXED(&c)) {
        VTermState *state = vterm_obtain_state(m_vterm);
        vterm_state_convert_color_to_rgb(state, &c);
    }
    return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

bool TerminalWidget::event(QEvent *evt)
{
    // Intercept Tab/Backtab before Qt uses them for focus navigation
    if (evt->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(evt);
        if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
            keyPressEvent(ke);
            return true;
        }
    }
    return QWidget::event(evt);
}

void TerminalWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setFont(m_font);

    const auto &pal = ThemeManager::instance().palette();
    QColor defaultBg = pal.bg_base;
    p.fillRect(rect(), defaultBg);

    if (!m_vtermScreen)
        return;

    // Draw scrollback lines (if scrolled up)
    int scrollbackVisible = qMin(m_scrollOffset, static_cast<int>(m_scrollback.size()));
    int scrollbackStart = static_cast<int>(m_scrollback.size()) - m_scrollOffset;

    for (int visualRow = 0; visualRow < m_rows; ++visualRow) {
        int y = visualRow * m_cellHeight;
        int lineIndex = visualRow - scrollbackVisible; // negative = scrollback, >= 0 = screen

        if (visualRow < scrollbackVisible) {
            // Drawing from scrollback buffer
            int sbIdx = scrollbackStart + visualRow;
            if (sbIdx < 0 || sbIdx >= m_scrollback.size())
                continue;
            const auto &line = m_scrollback[sbIdx];
            for (int col = 0; col < qMin(line.cols, m_cols); ++col) {
                int x = col * m_cellWidth;
                const VTermScreenCell &cell = line.cells[col];

                QColor bg = vtermColorToQColor(cell.bg);
                QColor fg = vtermColorToQColor(cell.fg);
                if (cell.attrs.reverse)
                    std::swap(fg, bg);

                if (bg != defaultBg)
                    p.fillRect(x, y, m_cellWidth, m_cellHeight, bg);

                if (cell.chars[0] != 0 && cell.chars[0] != ' ') {
                    QFont drawFont = m_font;
                    drawFont.setBold(cell.attrs.bold);
                    drawFont.setItalic(cell.attrs.italic);
                    drawFont.setUnderline(cell.attrs.underline != 0);
                    drawFont.setStrikeOut(cell.attrs.strike);
                    p.setFont(drawFont);
                    p.setPen(fg);
                    p.drawText(x, y + m_cellHeight - p.fontMetrics().descent(),
                               QString::fromUcs4(&cell.chars[0], 1));
                }
            }
        } else {
            // Drawing from vterm screen
            int screenRow = lineIndex;
            if (screenRow < 0 || screenRow >= m_rows)
                continue;

            for (int col = 0; col < m_cols; ++col) {
                int x = col * m_cellWidth;
                VTermPos pos = {screenRow, col};
                VTermScreenCell cell;
                vterm_screen_get_cell(m_vtermScreen, pos, &cell);

                QColor bg = vtermColorToQColor(cell.bg);
                QColor fg = vtermColorToQColor(cell.fg);
                if (cell.attrs.reverse)
                    std::swap(fg, bg);

                if (bg != defaultBg)
                    p.fillRect(x, y, m_cellWidth * cell.width, m_cellHeight, bg);

                if (cell.chars[0] != 0 && cell.chars[0] != ' ') {
                    QFont drawFont = m_font;
                    drawFont.setBold(cell.attrs.bold);
                    drawFont.setItalic(cell.attrs.italic);
                    drawFont.setUnderline(cell.attrs.underline != 0);
                    drawFont.setStrikeOut(cell.attrs.strike);
                    p.setFont(drawFont);
                    p.setPen(fg);
                    p.drawText(x, y + m_cellHeight - p.fontMetrics().descent(),
                               QString::fromUcs4(&cell.chars[0], 1));
                }

                // Wide characters occupy two columns
                if (cell.width > 1)
                    col += cell.width - 1;
            }
        }
    }

    // Always draw cursor (regardless of focus) so it's never invisible
    if (m_scrollOffset == 0) {
        int cx = m_cursorPos.col * m_cellWidth;
        int cy = m_cursorPos.row * m_cellHeight;
        QColor cursorColor = pal.mauve;

        if (hasFocus()) {
            if (m_cursorBlinkState || !m_cursorBlink) {
                // Focused: bright solid block cursor
                p.fillRect(cx, cy, m_cellWidth, m_cellHeight, cursorColor);

                // Draw character under cursor inverted
                VTermPos pos = {m_cursorPos.row, m_cursorPos.col};
                VTermScreenCell cell;
                vterm_screen_get_cell(m_vtermScreen, pos, &cell);
                if (cell.chars[0] != 0 && cell.chars[0] != ' ') {
                    p.setPen(pal.bg_base);
                    p.setFont(m_font);
                    p.drawText(cx, cy + m_cellHeight - p.fontMetrics().descent(),
                               QString::fromUcs4(&cell.chars[0], 1));
                }
            } else {
                // Blink-off: thin I-beam so position stays visible
                p.fillRect(cx, cy, 2, m_cellHeight, cursorColor);
            }
        } else {
            // Unfocused: bright solid outline — always visible
            p.setPen(QPen(cursorColor, 2.0));
            p.setBrush(Qt::NoBrush);
            p.drawRect(cx + 1, cy + 1, m_cellWidth - 2, m_cellHeight - 2);
        }
    }
}

// ---------------------------------------------------------------------------
// Resize
// ---------------------------------------------------------------------------

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    recalculateGridSize();
    if (m_vterm && m_rows > 0 && m_cols > 0) {
        vterm_set_size(m_vterm, m_rows, m_cols);
        if (m_pty)
            m_pty->resize(m_rows, m_cols);
    }
}

// ---------------------------------------------------------------------------
// Keyboard input
// ---------------------------------------------------------------------------

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (!m_vterm)
        return;

    // Snap to bottom on any keypress
    m_scrollOffset = 0;

    VTermModifier mod = VTERM_MOD_NONE;
    if (event->modifiers() & Qt::ShiftModifier)
        mod = static_cast<VTermModifier>(mod | VTERM_MOD_SHIFT);
    if (event->modifiers() & Qt::AltModifier)
        mod = static_cast<VTermModifier>(mod | VTERM_MOD_ALT);
    if (event->modifiers() & Qt::ControlModifier)
        mod = static_cast<VTermModifier>(mod | VTERM_MOD_CTRL);

    // Handle Ctrl+Shift+C/V for copy/paste
    if ((event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)) ==
        (Qt::ControlModifier | Qt::ShiftModifier)) {
        if (event->key() == Qt::Key_C) {
            // Copy — TODO: implement selection
            return;
        }
        if (event->key() == Qt::Key_V) {
            QByteArray clip = QApplication::clipboard()->text().toUtf8();
            if (m_pty && !clip.isEmpty())
                m_pty->write(clip);
            return;
        }
    }

    VTermKey vtKey = VTERM_KEY_NONE;
    switch (event->key()) {
    case Qt::Key_Return:    case Qt::Key_Enter:     vtKey = VTERM_KEY_ENTER; break;
    case Qt::Key_Tab:                               vtKey = VTERM_KEY_TAB; break;
    case Qt::Key_Backspace:                         vtKey = VTERM_KEY_BACKSPACE; break;
    case Qt::Key_Escape:                            vtKey = VTERM_KEY_ESCAPE; break;
    case Qt::Key_Up:                                vtKey = VTERM_KEY_UP; break;
    case Qt::Key_Down:                              vtKey = VTERM_KEY_DOWN; break;
    case Qt::Key_Left:                              vtKey = VTERM_KEY_LEFT; break;
    case Qt::Key_Right:                             vtKey = VTERM_KEY_RIGHT; break;
    case Qt::Key_Insert:                            vtKey = VTERM_KEY_INS; break;
    case Qt::Key_Delete:                            vtKey = VTERM_KEY_DEL; break;
    case Qt::Key_Home:                              vtKey = VTERM_KEY_HOME; break;
    case Qt::Key_End:                               vtKey = VTERM_KEY_END; break;
    case Qt::Key_PageUp:                            vtKey = VTERM_KEY_PAGEUP; break;
    case Qt::Key_PageDown:                          vtKey = VTERM_KEY_PAGEDOWN; break;
    case Qt::Key_F1:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 1); break;
    case Qt::Key_F2:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 2); break;
    case Qt::Key_F3:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 3); break;
    case Qt::Key_F4:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 4); break;
    case Qt::Key_F5:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 5); break;
    case Qt::Key_F6:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 6); break;
    case Qt::Key_F7:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 7); break;
    case Qt::Key_F8:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 8); break;
    case Qt::Key_F9:  vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 9); break;
    case Qt::Key_F10: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 10); break;
    case Qt::Key_F11: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 11); break;
    case Qt::Key_F12: vtKey = static_cast<VTermKey>(VTERM_KEY_FUNCTION_0 + 12); break;
    default: break;
    }

    if (vtKey != VTERM_KEY_NONE) {
        vterm_keyboard_key(m_vterm, vtKey, mod);
        return;
    }

    // Unicode character input
    QString text = event->text();
    if (!text.isEmpty()) {
        uint32_t codepoint = text.at(0).unicode();
        vterm_keyboard_unichar(m_vterm, codepoint, mod);
    }
}

void TerminalWidget::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event->commitString().isEmpty() && m_pty) {
        m_pty->write(event->commitString().toUtf8());
    }
    event->accept();
}

QVariant TerminalWidget::inputMethodQuery(Qt::InputMethodQuery query) const
{
    if (query == Qt::ImCursorRectangle) {
        return QRect(m_cursorPos.col * m_cellWidth,
                     m_cursorPos.row * m_cellHeight,
                     m_cellWidth, m_cellHeight);
    }
    return QWidget::inputMethodQuery(query);
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    m_cursorBlinkState = true;
    m_cursorTimer.start();
    update();
    QWidget::focusInEvent(event);
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    m_cursorTimer.stop();
    m_cursorBlinkState = true;
    update();
    QWidget::focusOutEvent(event);
}

// ---------------------------------------------------------------------------
// Mouse wheel (scrollback)
// ---------------------------------------------------------------------------

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y();
    int lines = delta / 40;

    m_scrollOffset = qBound(0, m_scrollOffset - lines,
                            static_cast<int>(m_scrollback.size()));
    update();
    event->accept();
}

// ---------------------------------------------------------------------------
// libvterm screen callbacks
// ---------------------------------------------------------------------------

int TerminalWidget::onDamage(VTermRect, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    self->update();
    return 0;
}

int TerminalWidget::onMoveCursor(VTermPos pos, VTermPos, int visible, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    self->m_cursorPos = pos;
    self->m_cursorVisible = visible;
    self->m_cursorBlinkState = true;
    self->update();
    return 0;
}

int TerminalWidget::onSetTermProp(VTermProp prop, VTermValue *val, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    switch (prop) {
    case VTERM_PROP_TITLE:
        self->m_windowTitle = QString::fromUtf8(val->string.str,
                                                 static_cast<int>(val->string.len));
        emit self->titleChanged(self->m_windowTitle);
        break;
    case VTERM_PROP_CURSORVISIBLE:
        self->m_cursorVisible = val->boolean;
        break;
    case VTERM_PROP_CURSORBLINK:
        self->m_cursorBlink = val->boolean;
        if (self->m_cursorBlink)
            self->m_cursorTimer.start();
        else
            self->m_cursorTimer.stop();
        break;
    default:
        break;
    }
    return 0;
}

int TerminalWidget::onBell(void *user)
{
    QApplication::beep();
    return 0;
}

int TerminalWidget::onSbPushLine(int cols, const VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    ScrollbackLine line;
    line.cols = cols;
    line.cells.resize(cols);
    memcpy(line.cells.data(), cells, cols * sizeof(VTermScreenCell));
    self->m_scrollback.append(std::move(line));

    if (self->m_scrollback.size() > MAX_SCROLLBACK)
        self->m_scrollback.removeFirst();

    return 0;
}

int TerminalWidget::onSbPopLine(int cols, VTermScreenCell *cells, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    if (self->m_scrollback.isEmpty())
        return 0;

    const auto &line = self->m_scrollback.last();
    int n = qMin(cols, line.cols);
    memcpy(cells, line.cells.constData(), n * sizeof(VTermScreenCell));
    for (int i = n; i < cols; ++i) {
        memset(&cells[i], 0, sizeof(VTermScreenCell));
        cells[i].width = 1;
    }
    self->m_scrollback.removeLast();
    return 1;
}

void TerminalWidget::onOutput(const char *bytes, size_t len, void *user)
{
    auto *self = static_cast<TerminalWidget *>(user);
    if (self->m_pty)
        self->m_pty->write(QByteArray(bytes, static_cast<int>(len)));
}
