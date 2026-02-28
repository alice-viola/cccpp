#pragma once

#include <QWidget>
#include <QFont>
#include <QColor>
#include <QVector>
#include <QTimer>

extern "C" {
#include <vterm.h>
}

class PtyProcess;

struct ScrollbackLine {
    int cols;
    QVector<VTermScreenCell> cells;
};

class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget *parent = nullptr);
    ~TerminalWidget() override;

    void startShell(const QString &workingDir);
    void writeToPty(const QByteArray &data);
    bool isRunning() const;

    QSize sizeHint() const override;

signals:
    void titleChanged(const QString &title);
    void shellFinished(int exitCode);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void initVterm(int rows, int cols);
    void destroyVterm();
    void recalculateGridSize();
    QColor vtermColorToQColor(VTermColor c) const;

    // libvterm callbacks
    static int onDamage(VTermRect rect, void *user);
    static int onMoveCursor(VTermPos pos, VTermPos oldpos, int visible, void *user);
    static int onSetTermProp(VTermProp prop, VTermValue *val, void *user);
    static int onBell(void *user);
    static int onSbPushLine(int cols, const VTermScreenCell *cells, void *user);
    static int onSbPopLine(int cols, VTermScreenCell *cells, void *user);
    static void onOutput(const char *bytes, size_t len, void *user);

    PtyProcess *m_pty = nullptr;
    VTerm *m_vterm = nullptr;
    VTermScreen *m_vtermScreen = nullptr;

    QFont m_font;
    int m_cellWidth = 0;
    int m_cellHeight = 0;
    int m_rows = 0;
    int m_cols = 0;

    VTermPos m_cursorPos = {0, 0};
    bool m_cursorVisible = true;
    bool m_cursorBlink = true;
    QTimer m_cursorTimer;
    bool m_cursorBlinkState = true;

    QVector<ScrollbackLine> m_scrollback;
    int m_scrollOffset = 0; // 0 = at bottom; >0 = scrolled up
    static constexpr int MAX_SCROLLBACK = 10000;

    QString m_windowTitle;
};
