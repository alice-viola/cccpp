#pragma once

#include <QDialog>
#include <QLineEdit>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onBrowse();
    void onAccept();
    void onDetect();

private:
    QLineEdit *m_claudePath;
};
