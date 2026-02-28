#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QPushButton>
#include <QStringList>

class SuggestionChips : public QFrame {
    Q_OBJECT
public:
    explicit SuggestionChips(QWidget *parent = nullptr);

    void setSuggestions(const QStringList &suggestions);
    void clear();

signals:
    void suggestionClicked(const QString &text);

private:
    void applyThemeColors();
    QHBoxLayout *m_layout;
};
