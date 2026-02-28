#include "ui/ModelSelector.h"
#include <QHBoxLayout>
#include <QLabel>

struct ModelInfo {
    const char *id;
    const char *label;
};

static const ModelInfo MODELS[] = {
    {"claude-sonnet-4-6",           "Sonnet 4.6"},
    {"claude-opus-4-6",             "Opus 4.6"},
    {"claude-opus-4-5-20251101",    "Opus 4.5"},
    {"claude-haiku-4-5-20251001",   "Haiku 4.5"},
    {"claude-sonnet-4-5-20250929",  "Sonnet 4.5"},
};

ModelSelector::ModelSelector(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 2, 8, 2);
    layout->setSpacing(6);

    auto *label = new QLabel("Model", this);
    label->setStyleSheet("color: #6c7086; font-size: 11px;");
    layout->addWidget(label);

    m_combo = new QComboBox(this);
    m_combo->setFixedHeight(24);
    m_combo->setMinimumWidth(130);
    // Styled via QSS — only override the dropdown list selection color
    m_combo->setStyleSheet(
        "QComboBox QAbstractItemView { selection-background-color: #252525; }");

    for (const auto &m : MODELS)
        m_combo->addItem(m.label, QString(m.id));

    connect(m_combo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int idx) {
        emit modelChanged(m_combo->itemData(idx).toString());
    });

    layout->addWidget(m_combo);
    layout->addStretch();
}

QString ModelSelector::currentModelId() const
{
    return m_combo->currentData().toString();
}

QString ModelSelector::currentModelLabel() const
{
    return m_combo->currentText();
}

void ModelSelector::setModel(const QString &modelId)
{
    for (int i = 0; i < m_combo->count(); ++i) {
        if (m_combo->itemData(i).toString() == modelId) {
            m_combo->setCurrentIndex(i);
            return;
        }
    }
}
