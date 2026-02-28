#pragma once

#include <QFrame>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QButtonGroup>
#include <QRadioButton>
#include <QCheckBox>
#include <nlohmann/json.hpp>

class QuestionWidget : public QFrame {
    Q_OBJECT
public:
    explicit QuestionWidget(const nlohmann::json &input, QWidget *parent = nullptr);

signals:
    void answered(const QString &responseText);

private:
    void buildUI(const nlohmann::json &input);
    void submitAnswer();

    struct QuestionData {
        QString header;
        bool multiSelect = false;
        QList<QPair<QString, QString>> options; // id, description
        QWidget *optionGroup = nullptr;
    };

    QVBoxLayout *m_layout;
    QList<QuestionData> m_questions;
    QPushButton *m_submitBtn;
};
