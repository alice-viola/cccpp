#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QLabel>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onBrowse();
    void onAccept();
    void onDetect();
    void onTestTelegram();

private:
    QLineEdit *m_claudePath;

    // Telegram
    QCheckBox *m_telegramEnabled;
    QLineEdit *m_telegramToken;
    QLineEdit *m_telegramUsers;
    QLabel *m_telegramStatus;
};
