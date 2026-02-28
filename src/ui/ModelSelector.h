#pragma once

#include <QWidget>
#include <QComboBox>

class ModelSelector : public QWidget {
    Q_OBJECT
public:
    explicit ModelSelector(QWidget *parent = nullptr);

    QString currentModelId() const;
    QString currentModelLabel() const;
    void setModel(const QString &modelId);

signals:
    void modelChanged(const QString &modelId);

private:
    QComboBox *m_combo;
};
