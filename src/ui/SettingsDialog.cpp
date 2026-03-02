#include "ui/SettingsDialog.h"
#include "util/Config.h"
#include "core/TelegramApi.h"

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

    // --- Telegram group ---
    auto &cfg = Config::instance();

    auto *telegramGroup = new QGroupBox("Telegram Bot");
    auto *tgLayout = new QVBoxLayout(telegramGroup);

    m_telegramEnabled = new QCheckBox("Enable Telegram bot");
    m_telegramEnabled->setChecked(cfg.telegramEnabled());
    tgLayout->addWidget(m_telegramEnabled);

    auto *tokenLabel = new QLabel("Bot token (from @BotFather):");
    tgLayout->addWidget(tokenLabel);

    m_telegramToken = new QLineEdit;
    m_telegramToken->setEchoMode(QLineEdit::Password);
    m_telegramToken->setPlaceholderText("123456:ABC-DEF...");
    m_telegramToken->setText(cfg.telegramBotToken());
    tgLayout->addWidget(m_telegramToken);

    auto *usersLabel = new QLabel("Allowed user IDs (comma-separated, empty = allow all):");
    tgLayout->addWidget(usersLabel);

    m_telegramUsers = new QLineEdit;
    m_telegramUsers->setPlaceholderText("123456789, 987654321");
    QStringList userStrs;
    for (qint64 id : cfg.telegramAllowedUsers())
        userStrs.append(QString::number(id));
    m_telegramUsers->setText(userStrs.join(", "));
    tgLayout->addWidget(m_telegramUsers);

    auto *testRow = new QHBoxLayout;
    auto *testBtn = new QPushButton("Test Connection");
    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::onTestTelegram);
    testRow->addWidget(testBtn);

    m_telegramStatus = new QLabel;
    testRow->addWidget(m_telegramStatus, 1);
    tgLayout->addLayout(testRow);

    mainLayout->addWidget(telegramGroup);

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
    auto &cfg = Config::instance();
    cfg.setSuppressAutoSave(true);

    QString path = m_claudePath->text().trimmed();
    if (path.isEmpty())
        path = "claude";
    cfg.setClaudeBinary(path);

    cfg.setTelegramEnabled(m_telegramEnabled->isChecked());
    cfg.setTelegramBotToken(m_telegramToken->text().trimmed());

    QList<qint64> userIds;
    QString usersText = m_telegramUsers->text().trimmed();
    if (!usersText.isEmpty()) {
        for (const QString &s : usersText.split(',')) {
            bool ok;
            qint64 id = s.trimmed().toLongLong(&ok);
            if (ok) userIds.append(id);
        }
    }
    cfg.setTelegramAllowedUsers(userIds);

    cfg.setSuppressAutoSave(false);
    cfg.save();

    accept();
}

void SettingsDialog::onTestTelegram()
{
    QString token = m_telegramToken->text().trimmed();
    if (token.isEmpty()) {
        m_telegramStatus->setText("Enter a token first.");
        return;
    }

    m_telegramStatus->setText("Testing...");

    auto *api = new TelegramApi(this);
    api->setToken(token);
    api->getMe([this, api](bool ok, const QString &username) {
        if (ok)
            m_telegramStatus->setText("Connected: @" + username);
        else
            m_telegramStatus->setText("Failed to connect. Check token.");
        api->deleteLater();
    });
}
