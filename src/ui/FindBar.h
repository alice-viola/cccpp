#pragma once

#include <functional>
#include <QWidget>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QKeyEvent>
#include "ui/ThemeManager.h"

#ifndef NO_QSCINTILLA
#include <Qsci/qsciscintilla.h>
#endif

class FindBar : public QWidget {
public:
    explicit FindBar(QWidget *parent = nullptr);

    void focusInput();
    void prefill(const QString &text);
    void setCloseCallback(std::function<void()> cb) { m_closeCb = std::move(cb); }

#ifndef NO_QSCINTILLA
    void setEditor(QsciScintilla *ed);
#endif

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void doFind(const QString &text);
    void findNext();
    void findPrev();

    QLineEdit *m_input = nullptr;
    QLabel *m_countLabel = nullptr;
    std::function<void()> m_closeCb;
#ifndef NO_QSCINTILLA
    QsciScintilla *m_editor = nullptr;
#endif
};
