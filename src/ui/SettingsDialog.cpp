#include "ui/SettingsDialog.h"
#include "util/Config.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QDir>
#include <QFile>
#include <QGroupBox>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumWidth(480);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);

    // --- Claude Binary group ---
    auto *claudeGroup = new QGroupBox("Claude CLI Binary");
    auto *groupLayout = new QVBoxLayout(claudeGroup);

    auto *helpLabel = new QLabel(
        "Set the path to the <b>claude</b> executable. "
        "Leave empty or set to <code>claude</code> to auto-detect.");
    helpLabel->setWordWrap(true);
    groupLayout->addWidget(helpLabel);

    auto *pathRow = new QHBoxLayout;
    m_claudePath = new QLineEdit;
    m_claudePath->setPlaceholderText("claude  (auto-detect)");
    m_claudePath->setText(Config::instance().claudeBinary());
    if (m_claudePath->text() == "claude")
        m_claudePath->clear();
    pathRow->addWidget(m_claudePath, 1);

    auto *browseBtn = new QPushButton("Browse...");
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowse);
    pathRow->addWidget(browseBtn);

    auto *detectBtn = new QPushButton("Detect");
    connect(detectBtn, &QPushButton::clicked, this, &SettingsDialog::onDetect);
    pathRow->addWidget(detectBtn);

    groupLayout->addLayout(pathRow);
    mainLayout->addWidget(claudeGroup);

    // --- Buttons ---
    mainLayout->addStretch();
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();

    auto *cancelBtn = new QPushButton("Cancel");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    buttonRow->addWidget(cancelBtn);

    auto *okBtn = new QPushButton("Save");
    okBtn->setDefault(true);
    connect(okBtn, &QPushButton::clicked, this, &SettingsDialog::onAccept);
    buttonRow->addWidget(okBtn);

    mainLayout->addLayout(buttonRow);
}

void SettingsDialog::onBrowse()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select Claude Binary", "/usr/local/bin");
    if (!path.isEmpty())
        m_claudePath->setText(path);
}

void SettingsDialog::onDetect()
{
    QStringList searchDirs = {
        QDir::homePath() + "/.local/bin",
        "/usr/local/bin",
        "/opt/homebrew/bin",
        QDir::homePath() + "/.npm-global/bin",
        QDir::homePath() + "/.yarn/bin",
        QDir::homePath() + "/.cargo/bin",
        "/usr/bin",
        "/snap/bin",
    };

    // Also search NVM node versions
    QDir nvmDir(QDir::homePath() + "/.nvm/versions/node");
    if (nvmDir.exists()) {
        QStringList versions = nvmDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (!versions.isEmpty())
            searchDirs.prepend(nvmDir.absoluteFilePath(versions.last()) + "/bin");
    }

    for (const QString &dir : searchDirs) {
        QString candidate = dir + "/claude";
        if (QFile::exists(candidate)) {
            m_claudePath->setText(candidate);
            return;
        }
    }

    m_claudePath->setPlaceholderText("Not found — enter path manually");
}

void SettingsDialog::onAccept()
{
    QString path = m_claudePath->text().trimmed();
    if (path.isEmpty())
        path = "claude";
    Config::instance().setClaudeBinary(path);
    accept();
}
