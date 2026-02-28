#include "ui/BreadcrumbBar.h"
#include "ui/ThemeManager.h"
#include "ui/FileIconProvider.h"
#include <QDir>
#include <QFileInfo>
#include <QMouseEvent>

class ClickableLabel : public QLabel {
public:
    ClickableLabel(const QString &text, const QString &path, QWidget *parent)
        : QLabel(text, parent), m_path(path) {
        setCursor(Qt::PointingHandCursor);
    }
    QString path() const { return m_path; }
protected:
    void mousePressEvent(QMouseEvent *e) override {
        if (e->button() == Qt::LeftButton) {
            auto *bar = qobject_cast<BreadcrumbBar *>(parentWidget());
            if (bar) emit bar->segmentClicked(m_path);
        }
        QLabel::mousePressEvent(e);
    }
private:
    QString m_path;
};

BreadcrumbBar::BreadcrumbBar(QWidget *parent)
    : QWidget(parent)
{
    setFixedHeight(24);
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(10, 0, 10, 0);
    m_layout->setSpacing(0);
    m_layout->addStretch();

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &BreadcrumbBar::applyThemeColors);

    hide();
}

void BreadcrumbBar::setFilePath(const QString &filePath, const QString &rootPath)
{
    m_filePath = filePath;
    m_rootPath = rootPath;
    rebuild();
    show();
}

void BreadcrumbBar::clear()
{
    for (auto *w : m_segments) {
        m_layout->removeWidget(w);
        w->deleteLater();
    }
    m_segments.clear();
    m_filePath.clear();
    hide();
}

void BreadcrumbBar::rebuild()
{
    for (auto *w : m_segments) {
        m_layout->removeWidget(w);
        w->deleteLater();
    }
    m_segments.clear();

    if (m_filePath.isEmpty()) return;

    const auto &pal = ThemeManager::instance().palette();
    QString relPath = m_filePath;
    if (!m_rootPath.isEmpty())
        relPath = QDir(m_rootPath).relativeFilePath(m_filePath);

    QStringList parts = relPath.split('/');
    if (parts.isEmpty()) return;

    QString accumulated = m_rootPath;
    int insertIdx = 0;

    for (int i = 0; i < parts.size(); ++i) {
        bool isLast = (i == parts.size() - 1);
        QString part = parts[i];

        if (i > 0) {
            auto *sep = new QLabel(QString::fromUtf8(" \xe2\x80\xba "), this);
            sep->setStyleSheet(
                QStringLiteral("QLabel { color: %1; font-size: 11px; padding: 0; }")
                .arg(pal.text_faint.name()));
            m_layout->insertWidget(insertIdx++, sep);
            m_segments.append(sep);
        }

        accumulated += (i > 0 ? "/" : "") + part;

        if (isLast) {
            QIcon icon = FileIconProvider::iconForFile(part);
            auto *fileLabel = new QLabel(this);
            fileLabel->setPixmap(icon.pixmap(14, 14));
            fileLabel->setFixedSize(16, 16);
            fileLabel->setStyleSheet("QLabel { padding: 0; margin: 0 2px 0 0; }");
            m_layout->insertWidget(insertIdx++, fileLabel);
            m_segments.append(fileLabel);

            auto *nameLabel = new QLabel(part, this);
            nameLabel->setStyleSheet(
                QStringLiteral("QLabel { color: %1; font-size: 12px; font-weight: 500; padding: 0; }")
                .arg(pal.text_primary.name()));
            m_layout->insertWidget(insertIdx++, nameLabel);
            m_segments.append(nameLabel);
        } else {
            auto *label = new ClickableLabel(part, accumulated, this);
            label->setStyleSheet(
                QStringLiteral("QLabel { color: %1; font-size: 11px; padding: 0; }"
                               "QLabel:hover { color: %2; }")
                .arg(pal.text_muted.name(), pal.text_primary.name()));
            m_layout->insertWidget(insertIdx++, label);
            m_segments.append(label);
        }
    }
}

void BreadcrumbBar::applyThemeColors()
{
    const auto &pal = ThemeManager::instance().palette();
    setStyleSheet(
        QStringLiteral("BreadcrumbBar { background: %1; border-bottom: 1px solid %2; }")
        .arg(pal.bg_base.name(), pal.border_subtle.name()));
    if (!m_filePath.isEmpty())
        rebuild();
}
