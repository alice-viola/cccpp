#include "ui/QuestionWidget.h"

static QString jstr(const nlohmann::json &j, const std::string &key) {
    if (j.contains(key) && j[key].is_string())
        return QString::fromStdString(j[key].get<std::string>());
    return {};
}

QuestionWidget::QuestionWidget(const nlohmann::json &input, QWidget *parent)
    : QFrame(parent)
{
    setStyleSheet(
        "QuestionWidget { background: #1c1c1c; border: 1px solid #333; "
        "border-radius: 6px; }");

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(12, 8, 12, 8);
    m_layout->setSpacing(6);

    auto *title = new QLabel("Claude has a question:", this);
    title->setStyleSheet("QLabel { color: #89b4fa; font-weight: bold; font-size: 12px; }");
    m_layout->addWidget(title);

    buildUI(input);

    m_submitBtn = new QPushButton("Submit Answer", this);
    m_submitBtn->setFixedHeight(28);
    m_submitBtn->setStyleSheet(
        "QPushButton { background: #a6e3a1; color: #0e0e0e; border: none; "
        "border-radius: 4px; font-weight: bold; font-size: 12px; }"
        "QPushButton:hover { background: #c6f0c2; }");
    connect(m_submitBtn, &QPushButton::clicked, this, &QuestionWidget::submitAnswer);
    m_layout->addWidget(m_submitBtn);
}

void QuestionWidget::buildUI(const nlohmann::json &input)
{
    if (!input.contains("questions") || !input["questions"].is_array())
        return;

    for (const auto &q : input["questions"]) {
        QuestionData qd;
        qd.header = jstr(q, "header");
        qd.multiSelect = q.contains("multiSelect") && q["multiSelect"].is_boolean()
                          && q["multiSelect"].get<bool>();

        if (!qd.header.isEmpty()) {
            auto *headerLabel = new QLabel(qd.header, this);
            headerLabel->setStyleSheet(
                "QLabel { color: #cdd6f4; font-weight: bold; font-size: 13px; padding-top: 4px; }");
            headerLabel->setWordWrap(true);
            m_layout->addWidget(headerLabel);
        }

        auto *optionContainer = new QWidget(this);
        auto *optionLayout = new QVBoxLayout(optionContainer);
        optionLayout->setContentsMargins(8, 0, 0, 0);
        optionLayout->setSpacing(2);
        qd.optionGroup = optionContainer;

        QButtonGroup *group = nullptr;
        if (!qd.multiSelect) {
            group = new QButtonGroup(optionContainer);
        }

        if (q.contains("options") && q["options"].is_array()) {
            for (const auto &opt : q["options"]) {
                // Claude Code options can have: label, value, description (any combo)
                QString label = jstr(opt, "label");
                QString value = jstr(opt, "value");
                QString desc = jstr(opt, "description");

                // Build display text from whatever fields exist
                QString displayText;
                if (!label.isEmpty()) {
                    displayText = label;
                    if (!desc.isEmpty()) displayText += " — " + desc;
                } else if (!value.isEmpty()) {
                    displayText = value;
                    if (!desc.isEmpty()) displayText += " — " + desc;
                } else {
                    displayText = desc;
                }

                // Value for the answer — prefer value, then label, then description
                if (value.isEmpty()) value = label;
                if (value.isEmpty()) value = desc;
                label = displayText;

                qd.options.append({value, label});

                if (qd.multiSelect) {
                    auto *cb = new QCheckBox(label, optionContainer);
                    cb->setStyleSheet("QCheckBox { color: #bac2de; font-size: 12px; }");
                    cb->setProperty("optValue", value);
                    optionLayout->addWidget(cb);
                } else {
                    auto *rb = new QRadioButton(label, optionContainer);
                    rb->setStyleSheet("QRadioButton { color: #bac2de; font-size: 12px; }");
                    rb->setProperty("optValue", value);
                    if (group) group->addButton(rb);
                    optionLayout->addWidget(rb);
                }
            }
        }

        m_layout->addWidget(optionContainer);
        m_questions.append(qd);
    }
}

void QuestionWidget::submitAnswer()
{
    QStringList answers;

    for (const auto &qd : m_questions) {
        if (!qd.optionGroup) continue;

        if (qd.multiSelect) {
            QStringList selected;
            for (auto *cb : qd.optionGroup->findChildren<QCheckBox *>()) {
                if (cb->isChecked())
                    selected << cb->property("optValue").toString();
            }
            if (!selected.isEmpty())
                answers << selected.join(", ");
        } else {
            for (auto *rb : qd.optionGroup->findChildren<QRadioButton *>()) {
                if (rb->isChecked()) {
                    answers << rb->property("optValue").toString();
                    break;
                }
            }
        }
    }

    QString response = answers.isEmpty() ? "Continue with defaults" : answers.join("; ");

    m_submitBtn->setEnabled(false);
    m_submitBtn->setText("Submitted");
    m_submitBtn->setStyleSheet(
        "QPushButton { background: #333; color: #666; border: none; "
        "border-radius: 4px; font-size: 12px; }");

    // Disable all options
    for (auto *rb : findChildren<QRadioButton *>()) rb->setEnabled(false);
    for (auto *cb : findChildren<QCheckBox *>()) cb->setEnabled(false);

    emit answered(response);
}
