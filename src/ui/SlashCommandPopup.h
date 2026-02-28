#pragma once

#include <QFrame>
#include <QListWidget>
#include <QVBoxLayout>

struct SlashCommand {
    QString command;
    QString description;
    QString icon;
};

class SlashCommandPopup : public QFrame {
    Q_OBJECT
public:
    explicit SlashCommandPopup(QWidget *parent = nullptr);

    void updateFilter(const QString &filter);
    void selectNext();
    void selectPrevious();
    QString acceptSelection();
    bool hasSelection() const;
    int itemCount() const;

signals:
    void commandSelected(const QString &command);
    void dismissed();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyThemeColors();

    QVBoxLayout *m_layout;
    QListWidget *m_list;
    QList<SlashCommand> m_allCommands;
    QList<SlashCommand> m_filtered;
};
