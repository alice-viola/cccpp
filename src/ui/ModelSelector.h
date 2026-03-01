#pragma once

#include <QWidget>
#include <QPushButton>
#include <QVector>

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
    void updateLabel();
    void showModelMenu();

    QPushButton *m_button;
    struct ModelInfo { QString id; QString label; };
    QVector<ModelInfo> m_models;
    int m_currentIndex = 0;
};
