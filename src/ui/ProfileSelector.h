#pragma once

#include <QWidget>
#include <QPushButton>
#include <QStringList>

class ProfileSelector : public QWidget {
    Q_OBJECT
public:
    explicit ProfileSelector(QWidget *parent = nullptr);

    QStringList selectedIds() const { return m_selectedIds; }
    void setSelectedIds(const QStringList &ids);

signals:
    void selectionChanged(const QStringList &profileIds);
    void manageProfilesRequested();

private:
    void updateLabel();
    void showProfileMenu();

    QPushButton *m_button;
    QStringList m_selectedIds;
};
