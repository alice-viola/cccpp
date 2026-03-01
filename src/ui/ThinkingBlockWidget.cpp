#include "ui/ThinkingBlockWidget.h"
#include "ui/ThemeManager.h"
#include <QTimer>
#include <QtMath>

static constexpr float TWO_PI = 6.28318530718f;

ThinkingBlockWidget::ThinkingBlockWidget(QWidget *parent)
    : QFrame(parent)
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(12, 6, 12, 6);
    m_layout->setSpacing(2);

    auto *header = new QWidget(this);
    m_headerLayout = new QHBoxLayout(header);
    m_headerLayout->setContentsMargins(0, 0, 0, 0);
    m_headerLayout->setSpacing(6);

    m_titleLabel = new QLabel("Thinking", this);
    m_headerLayout->addWidget(m_titleLabel);

    m_dotsLabel = new QLabel(this);
    m_dotsLabel->setFixedWidth(30);
    m_headerLayout->addWidget(m_dotsLabel);

    m_headerLayout->addStretch();
    m_layout->addWidget(header);

    m_contentBrowser = new QTextBrowser(this);
    m_contentBrowser->setFrameShape(QFrame::NoFrame);
    m_contentBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setVisible(true);
    m_contentBrowser->document()->setDocumentMargin(4);
    m_contentBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_contentBrowser->setMinimumHeight(0);
    m_contentBrowser->setMaximumHeight(0);
    m_layout->addWidget(m_contentBrowser);

    m_dotAnim = new QPropertyAnimation(this, "dotPhase", this);
    m_dotAnim->setStartValue(0.0f);
    m_dotAnim->setEndValue(TWO_PI);
    m_dotAnim->setDuration(1400);
    m_dotAnim->setEasingCurve(QEasingCurve::Linear);
    m_dotAnim->setLoopCount(-1);
    m_dotAnim->start();

    m_timer.start();

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ThinkingBlockWidget::applyThemeColors);
}

void ThinkingBlockWidget::appendContent(const QString &text)
{
    m_rawContent += text;
    QString escaped = m_rawContent.toHtmlEscaped();
    escaped.replace('\n', "<br>");

    auto &tm = ThemeManager::instance();
    m_contentBrowser->setHtml(QStringLiteral(
        "<div style='font-family:\"JetBrains Mono\";font-size:11px;"
        "color:%1;line-height:1.5;'>%2</div>")
        .arg(tm.hex("text_muted"), escaped));
    resizeBrowser();
}

void ThinkingBlockWidget::finalize()
{
    m_finalized = true;
    m_dotAnim->stop();
    m_dotsLabel->clear();

    qint64 elapsed = m_timer.elapsed();
    int secs = static_cast<int>(elapsed / 1000);
    QString duration;
    if (secs < 1)
        duration = "< 1s";
    else if (secs < 60)
        duration = QStringLiteral("%1s").arg(secs);
    else
        duration = QStringLiteral("%1m %2s").arg(secs / 60).arg(secs % 60);

    m_titleLabel->setText(QStringLiteral("Thought for %1").arg(duration));
}

void ThinkingBlockWidget::updateDotsText()
{
    if (m_finalized) return;
    int count = 1 + static_cast<int>(3.0f * (0.5f + 0.5f * sinf(m_dotPhase)));
    if (count > 3) count = 3;
    m_dotsLabel->setText(QString(".").repeated(count));
}

void ThinkingBlockWidget::resizeBrowser()
{
    if (!m_contentBrowser) return;
    int vpWidth = m_contentBrowser->viewport()->width();
    if (vpWidth <= 0)
        vpWidth = m_contentBrowser->width() - 4;
    if (vpWidth <= 0) {
        QTimer::singleShot(50, this, [this] { resizeBrowser(); });
        return;
    }
    m_contentBrowser->document()->setTextWidth(vpWidth);
    int h = qCeil(m_contentBrowser->document()->size().height()) + 2;
    if (h < 2) h = 2;
    m_contentBrowser->setMinimumHeight(h);
    m_contentBrowser->setMaximumHeight(h);
}

void ThinkingBlockWidget::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    resizeBrowser();
}

void ThinkingBlockWidget::applyThemeColors()
{
    auto &tm = ThemeManager::instance();
    auto &p = tm.palette();

    setStyleSheet(QStringLiteral(
        "ThinkingBlockWidget { background: transparent; }"));

    m_titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-style: italic; }")
        .arg(p.text_muted.name()));

    m_dotsLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 12px; font-weight: bold; }")
        .arg(p.mauve.name()));

    m_contentBrowser->setStyleSheet(QStringLiteral(
        "QTextBrowser { background: transparent; border: none; }"));

    if (!m_rawContent.isEmpty())
        appendContent("");
}
