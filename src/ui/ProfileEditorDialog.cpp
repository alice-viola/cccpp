#include "ui/ProfileEditorDialog.h"
#include "ui/ThemeManager.h"
#include "core/PersonalityProfile.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLabel>
#include <QColorDialog>
#include <QMessageBox>
#include <QPainter>

ProfileEditorDialog::ProfileEditorDialog(const QString &workspace, QWidget *parent)
    : QDialog(parent), m_workspace(workspace)
{
    setWindowTitle("Profiles & Workspace");
    setMinimumSize(640, 480);
    resize(720, 520);

    const auto &p = ThemeManager::instance().palette();

    setStyleSheet(QStringLiteral(
        "QDialog { background: %1; }"
        "QLabel { color: %2; font-size: 12px; }"
        "QLineEdit { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; "
        "  padding: 6px 8px; font-size: 12px; }"
        "QLineEdit:focus { border-color: %5; }"
        "QTextEdit { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; "
        "  padding: 6px 8px; font-size: 12px; font-family: monospace; }"
        "QTextEdit:focus { border-color: %5; }"
        "QListWidget { background: %3; color: %2; border: 1px solid %4; border-radius: 6px; "
        "  font-size: 12px; outline: none; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: %6; color: %7; }"
        "QPushButton { background: %6; color: %2; border: none; border-radius: 6px; "
        "  padding: 6px 14px; font-size: 12px; }"
        "QPushButton:hover { background: %8; }"
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { background: transparent; color: %9; border: none; padding: 8px 16px; font-size: 12px; }"
        "QTabBar::tab:selected { color: %2; border-bottom: 2px solid %5; }"
        "QTabBar::tab:hover:!selected { color: %10; }")
        .arg(p.bg_window.name(), p.text_primary.name(), p.bg_surface.name(),
             p.border_subtle.name(), p.blue.name(), p.bg_raised.name(),
             p.text_primary.name(), p.hover_raised.name(),
             p.text_muted.name(), p.text_secondary.name()));

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 12, 16, 12);

    auto *tabs = new QTabWidget(this);
    mainLayout->addWidget(tabs, 1);

    // ─── Profiles Tab ───
    auto *profilesPage = new QWidget;
    auto *profilesLayout = new QHBoxLayout(profilesPage);
    profilesLayout->setContentsMargins(8, 8, 8, 8);
    profilesLayout->setSpacing(12);

    // Left: profile list
    auto *leftPanel = new QWidget;
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(6);

    m_profileList = new QListWidget;
    m_profileList->setFixedWidth(180);
    leftLayout->addWidget(m_profileList, 1);

    auto *listBtnRow = new QHBoxLayout;
    listBtnRow->setSpacing(6);
    auto *addBtn = new QPushButton("+ Add");
    m_deleteBtn = new QPushButton("Delete");
    listBtnRow->addWidget(addBtn);
    listBtnRow->addWidget(m_deleteBtn);
    leftLayout->addLayout(listBtnRow);
    profilesLayout->addWidget(leftPanel);

    // Right: edit form
    auto *rightPanel = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    rightLayout->addWidget(new QLabel("Name:"));
    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("Profile name");
    rightLayout->addWidget(m_nameEdit);

    rightLayout->addWidget(new QLabel("Prompt:"));
    m_promptEdit = new QTextEdit;
    m_promptEdit->setPlaceholderText("System prompt instructions for this personality...");
    rightLayout->addWidget(m_promptEdit, 1);

    auto *colorRow = new QHBoxLayout;
    colorRow->addWidget(new QLabel("Color:"));
    m_colorBtn = new QPushButton;
    m_colorBtn->setFixedSize(28, 28);
    m_colorBtn->setCursor(Qt::PointingHandCursor);
    m_currentColor = QColor("#cdd6f4");
    m_colorBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 14px; }"
        "QPushButton:hover { border-color: %3; }")
        .arg(m_currentColor.name(), p.border_subtle.name(), p.blue.name()));
    colorRow->addWidget(m_colorBtn);
    colorRow->addStretch();
    rightLayout->addLayout(colorRow);
    profilesLayout->addWidget(rightPanel, 1);

    tabs->addTab(profilesPage, "Profiles");

    // ─── Workspace Spec Tab ───
    auto *wsPage = new QWidget;
    auto *wsLayout = new QVBoxLayout(wsPage);
    wsLayout->setContentsMargins(8, 8, 8, 8);
    wsLayout->setSpacing(8);

    auto *wsPathLabel = new QLabel(QStringLiteral("Workspace: %1").arg(m_workspace));
    wsPathLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }").arg(p.text_muted.name()));
    wsLayout->addWidget(wsPathLabel);

    wsLayout->addWidget(new QLabel("Name:"));
    m_wsNameEdit = new QLineEdit;
    m_wsNameEdit->setPlaceholderText("Display name for this workspace");
    wsLayout->addWidget(m_wsNameEdit);

    wsLayout->addWidget(new QLabel("Spec:"));
    m_wsSpecEdit = new QTextEdit;
    m_wsSpecEdit->setPlaceholderText(
        "Describe the project: architecture, tech stack, conventions, build commands, "
        "key directories, rules for the agent to follow...");
    wsLayout->addWidget(m_wsSpecEdit, 1);

    tabs->addTab(wsPage, "Workspace Spec");

    // ─── Bottom buttons ───
    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    auto *cancelBtn = new QPushButton("Cancel");
    auto *saveBtn = new QPushButton("Save");
    saveBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.blue.name(), p.bg_base.name(), p.sapphire.name()));
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    mainLayout->addLayout(btnRow);

    // Connections
    connect(m_profileList, &QListWidget::currentRowChanged, this, &ProfileEditorDialog::onProfileSelected);
    connect(addBtn, &QPushButton::clicked, this, &ProfileEditorDialog::onAddProfile);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ProfileEditorDialog::onDeleteProfile);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &ProfileEditorDialog::onAccept);

    connect(m_colorBtn, &QPushButton::clicked, this, [this, p] {
        QColor c = QColorDialog::getColor(m_currentColor, this, "Profile Color");
        if (c.isValid()) {
            m_currentColor = c;
            m_colorBtn->setStyleSheet(QStringLiteral(
                "QPushButton { background: %1; border: 1px solid %2; border-radius: 14px; }"
                "QPushButton:hover { border-color: %3; }")
                .arg(c.name(), p.border_subtle.name(), p.blue.name()));
        }
    });

    loadProfiles();
    loadWorkspaceSpec();

    if (m_profileList->count() > 0)
        m_profileList->setCurrentRow(0);
}

void ProfileEditorDialog::loadProfiles()
{
    m_profileList->clear();
    auto profiles = ProfileManager::instance().allProfiles();
    for (const auto &prof : profiles) {
        auto *item = new QListWidgetItem(prof.name);
        item->setData(Qt::UserRole, prof.id);

        // Color dot icon
        QPixmap dot(10, 10);
        dot.fill(Qt::transparent);
        QPainter painter(&dot);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(prof.color);
        painter.drawEllipse(1, 1, 8, 8);
        item->setIcon(QIcon(dot));

        m_profileList->addItem(item);
    }
}

void ProfileEditorDialog::loadWorkspaceSpec()
{
    auto spec = ProfileManager::instance().workspaceSpec(m_workspace);
    m_wsNameEdit->setText(spec.name);
    m_wsSpecEdit->setPlainText(spec.specText);
}

void ProfileEditorDialog::onProfileSelected(int row)
{
    // Save edits from previous selection
    saveCurrentProfileEdits();

    if (row < 0 || row >= m_profileList->count()) {
        m_nameEdit->clear();
        m_promptEdit->clear();
        m_deleteBtn->setEnabled(false);
        m_lastSelectedRow = -1;
        return;
    }

    m_lastSelectedRow = row;
    QString id = m_profileList->item(row)->data(Qt::UserRole).toString();
    auto prof = ProfileManager::instance().profile(id);

    m_nameEdit->setText(prof.name);
    m_promptEdit->setPlainText(prof.promptText);
    m_currentColor = prof.color;

    const auto &p = ThemeManager::instance().palette();
    m_colorBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; border: 1px solid %2; border-radius: 14px; }"
        "QPushButton:hover { border-color: %3; }")
        .arg(m_currentColor.name(), p.border_subtle.name(), p.blue.name()));

    // Can't delete built-in profiles
    m_deleteBtn->setEnabled(!prof.builtIn);
    m_nameEdit->setReadOnly(false);
}

void ProfileEditorDialog::saveCurrentProfileEdits()
{
    if (m_lastSelectedRow < 0 || m_lastSelectedRow >= m_profileList->count())
        return;

    QString id = m_profileList->item(m_lastSelectedRow)->data(Qt::UserRole).toString();
    auto prof = ProfileManager::instance().profile(id);
    if (prof.id.isEmpty()) return;

    prof.name = m_nameEdit->text().trimmed();
    prof.promptText = m_promptEdit->toPlainText();
    prof.color = m_currentColor;

    ProfileManager::instance().updateProfile(prof);
    m_profileList->item(m_lastSelectedRow)->setText(prof.name);
}

void ProfileEditorDialog::onAddProfile()
{
    // Generate unique id
    QString baseName = "custom-profile";
    int num = 1;
    auto profiles = ProfileManager::instance().allProfiles();
    QSet<QString> ids;
    for (const auto &p : profiles)
        ids.insert(p.id);

    QString id = baseName;
    while (ids.contains(id))
        id = QStringLiteral("%1-%2").arg(baseName).arg(num++);

    PersonalityProfile p;
    p.id = id;
    p.name = "New Profile";
    p.promptText = "";
    p.color = QColor("#cdd6f4");
    p.builtIn = false;

    ProfileManager::instance().addProfile(p);
    loadProfiles();

    // Select the new item (last)
    m_profileList->setCurrentRow(m_profileList->count() - 1);
    m_nameEdit->setFocus();
    m_nameEdit->selectAll();
}

void ProfileEditorDialog::onDeleteProfile()
{
    int row = m_profileList->currentRow();
    if (row < 0) return;

    QString id = m_profileList->item(row)->data(Qt::UserRole).toString();
    auto prof = ProfileManager::instance().profile(id);
    if (prof.builtIn) return;

    auto answer = QMessageBox::question(this, "Delete Profile",
        QStringLiteral("Delete \"%1\"?").arg(prof.name),
        QMessageBox::Yes | QMessageBox::No);
    if (answer != QMessageBox::Yes) return;

    m_lastSelectedRow = -1;  // prevent saving edits for deleted item
    ProfileManager::instance().removeProfile(id);
    loadProfiles();

    if (m_profileList->count() > 0)
        m_profileList->setCurrentRow(qMin(row, m_profileList->count() - 1));
}

void ProfileEditorDialog::onAccept()
{
    // Save current profile edits
    saveCurrentProfileEdits();

    // Save workspace spec
    WorkspaceSpec spec;
    spec.workspace = m_workspace;
    spec.name = m_wsNameEdit->text().trimmed();
    spec.specText = m_wsSpecEdit->toPlainText();

    if (!spec.specText.trimmed().isEmpty() || !spec.name.trimmed().isEmpty()) {
        ProfileManager::instance().setWorkspaceSpec(spec);
    } else {
        ProfileManager::instance().removeWorkspaceSpec(m_workspace);
    }

    accept();
}
