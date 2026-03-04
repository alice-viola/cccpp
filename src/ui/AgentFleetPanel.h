#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QMap>
#include <QPropertyAnimation>
#include <QStringList>

struct AgentSummary {
    QString sessionId;
    QString title;
    QString activity;
    bool processing = false;
    bool hasPendingQuestion = false;
    bool unread = false;
    bool favorite = false;
    int editCount = 0;
    int turnCount = 0;
    double costUsd = 0.0;
    qint64 updatedAt = 0;
    QStringList profileIds;

    // Hierarchy
    QString parentSessionId;
    int depth = 0;
    bool isDelegatedChild = false;
    QString delegationTask;
    QString pipelineId;
};

class AgentCard : public QWidget {
    Q_OBJECT
    Q_PROPERTY(float pulsePhase READ pulsePhase WRITE setPulsePhase)
public:
    explicit AgentCard(const QString &sessionId, QWidget *parent = nullptr);

    QString sessionId() const { return m_sessionId; }
    void update(const AgentSummary &summary);
    void setSelected(bool selected);
    void setCollapsed(bool collapsed);

    float pulsePhase() const { return m_pulsePhase; }
    void setPulsePhase(float phase);

signals:
    void clicked(const QString &sessionId);
    void deleteRequested(const QString &sessionId);
    void doubleClicked(const QString &sessionId);
    void renameRequested(const QString &sessionId);
    void favoriteToggled(const QString &sessionId, bool favorite);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

private:
    void updatePulseAnimation();

    QString m_sessionId;
    QString m_title = "New Agent";
    QString m_activity;
    bool m_processing = false;
    bool m_blocked = false;
    bool m_unread = false;
    bool m_favorite = false;
    bool m_selected = false;
    bool m_collapsed = false;
    bool m_hovered = false;
    int m_editCount = 0;
    int m_turnCount = 0;
    int m_depth = 0;
    double m_costUsd = 0.0;
    qint64 m_updatedAt = 0;
    QStringList m_profileIds;
    QRect m_deleteRect;

    float m_pulsePhase = 1.0f;
    QPropertyAnimation *m_pulseAnim = nullptr;

    // Cached fonts (avoid recreation in paintEvent)
    QFont m_titleFont;
    QFont m_smallFont;
};

class AgentFleetPanel : public QWidget {
    Q_OBJECT
public:
    explicit AgentFleetPanel(QWidget *parent = nullptr);

    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

    void rebuild(const QList<AgentSummary> &agents, const QString &selectedId);
    void updateAgent(const AgentSummary &summary);
    void setSelectedAgent(const QString &sessionId);

signals:
    void agentSelected(const QString &sessionId);
    void newAgentRequested();
    void deleteRequested(const QString &sessionId);
    void exportAndDeleteRequested(const QString &sessionId);
    void deleteAllRequested();
    void deleteOlderThanDayRequested();
    void deleteAllExceptTodayRequested();
    void renameRequested(const QString &sessionId);
    void favoriteToggled(const QString &sessionId, bool favorite);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void applyThemeColors();
    void clearCards();

    QVBoxLayout *m_mainLayout = nullptr;
    QWidget *m_header = nullptr;
    QLabel *m_headerLabel = nullptr;
    QPushButton *m_newAgentBtn = nullptr;
    QPushButton *m_menuBtn = nullptr;
    QMenu *m_actionsMenu = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_scrollContent = nullptr;
    QVBoxLayout *m_agentLayout = nullptr;

    QMap<QString, AgentCard *> m_cards;
    QList<QWidget *> m_dividers;
    QString m_selectedId;
    bool m_collapsed = false;

    QList<AgentSummary> buildHierarchicalOrder(const QList<AgentSummary> &flat) const;
};
