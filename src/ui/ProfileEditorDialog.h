#pragma once

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QTabWidget>

class ProfileEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfileEditorDialog(const QString &workspace, QWidget *parent = nullptr);

private slots:
    void onProfileSelected(int row);
    void onAddProfile();
    void onDeleteProfile();
    void onAccept();

private:
    void loadProfiles();
    void loadWorkspaceSpec();
    void saveCurrentProfileEdits();

    // Profiles tab
    QListWidget *m_profileList;
    QLineEdit *m_nameEdit;
    QTextEdit *m_promptEdit;
    QPushButton *m_colorBtn;
    QPushButton *m_deleteBtn;
    QColor m_currentColor;
    int m_lastSelectedRow = -1;

    // Workspace tab
    QLineEdit *m_wsNameEdit;
    QTextEdit *m_wsSpecEdit;

    QString m_workspace;
};
