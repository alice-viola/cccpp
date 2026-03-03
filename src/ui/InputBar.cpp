#include "ui/InputBar.h"
#include "ui/ContextPopup.h"
#include "ui/SlashCommandPopup.h"
#include "ui/ThemeManager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QBuffer>
#include <QImage>
#include <QPixmap>
#include <QFileInfo>
#include <QTextCursor>
#include <QTimer>
#include <QDragEnterEvent>
#include <QDropEvent>

InputBar::InputBar(QWidget *parent)
    : QWidget(parent)
{
    setAcceptDrops(true);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 4, 16, 6);
    outerLayout->setSpacing(2);

    m_contextIndicator = new QLabel(this);
    m_contextIndicator->setVisible(false);
    m_contextIndicator->setWordWrap(true);
    outerLayout->addWidget(m_contextIndicator);

    m_contextPillBar = new QWidget(this);
    m_contextPillBar->setVisible(false);
    auto *pillLayout = new QHBoxLayout(m_contextPillBar);
    pillLayout->setContentsMargins(0, 2, 0, 0);
    pillLayout->setSpacing(4);
    pillLayout->addStretch();
    outerLayout->addWidget(m_contextPillBar);

    m_imageBar = new QWidget(this);
    m_imageBar->setVisible(false);
    auto *imgLayout = new QHBoxLayout(m_imageBar);
    imgLayout->setContentsMargins(0, 2, 0, 0);
    imgLayout->setSpacing(4);
    imgLayout->addStretch();
    outerLayout->addWidget(m_imageBar);

    // Unified input container (rounded card with border)
    m_inputContainer = new QWidget(this);
    m_inputContainer->setObjectName("inputContainer");
    auto *containerLayout = new QVBoxLayout(m_inputContainer);
    containerLayout->setContentsMargins(2, 0, 2, 0);
    containerLayout->setSpacing(0);

    m_input = new QTextEdit(m_inputContainer);
    m_input->setObjectName("chatInput");
    m_input->setPlaceholderText("Ask Claude anything... (@ to mention files, / for commands)");
    m_input->setFixedHeight(40);
    m_input->document()->setDocumentMargin(4);
    m_input->setAcceptRichText(false);
    m_input->setAcceptDrops(false);
    m_input->installEventFilter(this);
    containerLayout->addWidget(m_input);

    connect(m_input, &QTextEdit::textChanged, this, [this] {
        int docHeight = static_cast<int>(m_input->document()->size().height()) + 8;
        m_input->setFixedHeight(qBound(40, docHeight, 150));
    });

    auto *bottomBar = new QWidget(m_inputContainer);
    bottomBar->setStyleSheet("QWidget { background: transparent; }");
    m_bottomBarLayout = new QHBoxLayout(bottomBar);
    m_bottomBarLayout->setContentsMargins(10, 2, 8, 4);
    m_bottomBarLayout->setSpacing(4);

    // Footer widgets (mode/model selectors) inserted by addFooterWidget()

    m_sendBtn = new QPushButton("\xe2\x86\x91", m_inputContainer);
    m_sendBtn->setFixedSize(32, 32);
    m_bottomBarLayout->addWidget(m_sendBtn);

    containerLayout->addWidget(bottomBar);
    outerLayout->addWidget(m_inputContainer);

    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        if (m_processing) {
            emit stopRequested();
            return;
        }
        QString t = text().trimmed();
        if (!t.isEmpty()) {
            if (t.startsWith("/")) {
                int spacePos = t.indexOf(' ');
                QString cmd = (spacePos > 0) ? t.left(spacePos) : t;
                QString args = (spacePos > 0) ? t.mid(spacePos + 1).trimmed() : QString();
                emit slashCommand(cmd, args);
                clear();
                return;
            }
            emit sendRequested(t);
            clear();
        }
    });

    m_focusAnim = new QVariantAnimation(this);
    m_focusAnim->setDuration(180);
    m_focusAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_focusAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        applyBorderColor(v.value<QColor>());
    });

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this, &InputBar::applyThemeColors);
}

void InputBar::applyThemeColors()
{
    auto &p = ThemeManager::instance().palette();

    m_input->setStyleSheet(QStringLiteral(
        "QTextEdit#chatInput { background: transparent; color: %1; "
        "border: none; padding: 4px 10px; font-size: 13px; }")
        .arg(p.text_primary.name()));

    applyBorderColor(p.border_standard);

    m_sendBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 16px; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; color: %5; }")
        .arg(p.blue.name(), p.on_accent.name(), p.lavender.name(),
             p.bg_raised.name(), p.text_faint.name()));

    m_contextIndicator->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; padding: 2px 4px; }")
        .arg(p.text_muted.name()));

    setStyleSheet(QStringLiteral("InputBar { border-top: 1px solid %1; }")
        .arg(p.border_subtle.name()));
}

void InputBar::applyBorderColor(const QColor &c)
{
    auto &p = ThemeManager::instance().palette();
    m_inputContainer->setStyleSheet(
        QStringLiteral("#inputContainer { background: %1; "
                       "border: 1px solid %2; border-radius: 12px; }")
        .arg(p.bg_raised.name(), c.name()));
}

void InputBar::addFooterWidget(QWidget *w)
{
    // Insert before the send button (last widget)
    m_bottomBarLayout->insertWidget(m_bottomBarLayout->count() - 1, w);
}

QString InputBar::text() const
{
    return m_input->toPlainText();
}

void InputBar::setText(const QString &text)
{
    m_input->setPlainText(text);
    QTextCursor c = m_input->textCursor();
    c.movePosition(QTextCursor::End);
    m_input->setTextCursor(c);
}

void InputBar::focusInput()
{
    m_input->setFocus();
}

void InputBar::clear()
{
    m_input->clear();
    clearAttachments();
}

void InputBar::clearAttachments()
{
    m_attachedContexts.clear();
    m_attachedImages.clear();
    updateContextPills();

    // Clear image bar
    auto *layout = m_imageBar->layout();
    if (layout) {
        QLayoutItem *child;
        while ((child = layout->takeAt(0)) != nullptr) {
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }
        static_cast<QHBoxLayout *>(layout)->addStretch();
    }
    m_imageBar->setVisible(false);
}

void InputBar::setEnabled(bool enabled)
{
    m_input->setEnabled(enabled);
    if (!m_processing)
        m_sendBtn->setEnabled(enabled);
}

void InputBar::setProcessing(bool processing)
{
    m_processing = processing;
    auto &p = ThemeManager::instance().palette();

    if (processing) {
        m_input->setEnabled(false);
        m_sendBtn->setEnabled(true);
        m_sendBtn->setText("\xe2\x96\xa0");
        m_sendBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; "
            "border-radius: 16px; font-size: 12px; font-weight: bold; }"
            "QPushButton:hover { background: %3; }")
            .arg(p.mauve.name(), p.bg_base.name(), p.lavender.name()));
    } else {
        m_input->setEnabled(true);
        m_sendBtn->setEnabled(true);
        m_sendBtn->setText("\xe2\x86\x91");
        applyThemeColors();
    }
}

void InputBar::setPlaceholder(const QString &text)
{
    m_input->setPlaceholderText(text);
}

void InputBar::setWorkspacePath(const QString &path)
{
    m_workspacePath = path;
    while (m_workspacePath.endsWith('/') && m_workspacePath.length() > 1)
        m_workspacePath.chop(1);
}

void InputBar::setOpenFiles(const QStringList &files)
{
    m_openFiles = files;
}

void InputBar::setRecentFiles(const QStringList &files)
{
    m_recentFiles = files;
}

void InputBar::setContextIndicator(const QString &text)
{
    if (text.isEmpty()) {
        m_contextIndicator->setVisible(false);
    } else {
        m_contextIndicator->setText(text);
        m_contextIndicator->setVisible(true);
    }
}

void InputBar::showContextPopup()
{
    if (!m_contextPopup) {
        m_contextPopup = new ContextPopup(this);
        connect(m_contextPopup, &ContextPopup::itemSelected,
                this, &InputBar::onContextItemSelected);
        connect(m_contextPopup, &ContextPopup::dismissed, this, &InputBar::hideContextPopup);
    }

    m_contextPopup->setWorkspacePath(m_workspacePath);
    m_contextPopup->setOpenFiles(m_openFiles);
    m_contextPopup->setRecentFiles(m_recentFiles);
    m_contextPopup->updateFilter("");

    QPoint pos = m_input->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - m_contextPopup->height() - 4);
    m_contextPopup->move(pos);
    m_contextPopup->show();
    m_popupActive = true;
}

void InputBar::hideContextPopup()
{
    if (m_contextPopup) {
        m_contextPopup->hide();
    }
    m_popupActive = false;
    m_atTriggerPos = -1;
}

void InputBar::showSlashPopup()
{
    if (!m_slashPopup) {
        m_slashPopup = new SlashCommandPopup(this);
        connect(m_slashPopup, &SlashCommandPopup::commandSelected, this, [this](const QString &cmd) {
            m_input->clear();
            m_input->setPlainText(cmd + " ");
            QTextCursor cursor = m_input->textCursor();
            cursor.movePosition(QTextCursor::End);
            m_input->setTextCursor(cursor);
            hideSlashPopup();
        });
        connect(m_slashPopup, &SlashCommandPopup::dismissed, this, &InputBar::hideSlashPopup);
    }

    m_slashPopup->updateFilter("");
    QPoint pos = m_input->mapToGlobal(QPoint(0, 0));
    pos.setY(pos.y() - m_slashPopup->height() - 4);
    m_slashPopup->move(pos);
    m_slashPopup->show();
    m_slashPopupActive = true;
}

void InputBar::hideSlashPopup()
{
    if (m_slashPopup)
        m_slashPopup->hide();
    m_slashPopupActive = false;
}

void InputBar::onContextItemSelected(const QString &displayName, const QString &fullPath)
{
    AttachedContext ctx;
    ctx.displayName = displayName;
    ctx.fullPath = fullPath;
    m_attachedContexts.append(ctx);

    // Remove the @filter text from input
    if (m_atTriggerPos >= 0) {
        QTextCursor cursor = m_input->textCursor();
        int curPos = cursor.position();
        cursor.setPosition(m_atTriggerPos);
        cursor.setPosition(curPos, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }

    hideContextPopup();
    updateContextPills();
    m_input->setFocus();
}

void InputBar::updateContextPills()
{
    auto *layout = static_cast<QHBoxLayout *>(m_contextPillBar->layout());

    // Remove existing pills (keep stretch)
    QLayoutItem *child;
    while (layout->count() > 1) {
        child = layout->takeAt(0);
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    auto &p = ThemeManager::instance().palette();
    int insertPos = 0;
    for (int i = 0; i < m_attachedContexts.size(); ++i) {
        auto *pill = new QPushButton(
            QStringLiteral("@ %1  \xc3\x97").arg(m_attachedContexts[i].displayName), m_contextPillBar);
        pill->setFixedHeight(20);
        pill->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; border-radius: 12px; "
            "font-size: 11px; padding: 0 8px; }"
            "QPushButton:hover { background: %3; }")
            .arg(p.bg_raised.name(), p.blue.name(), p.hover_raised.name()));
        connect(pill, &QPushButton::clicked, this, [this, i] {
            if (i < m_attachedContexts.size()) {
                m_attachedContexts.removeAt(i);
                updateContextPills();
            }
        });
        layout->insertWidget(insertPos++, pill);
    }

    m_contextPillBar->setVisible(!m_attachedContexts.isEmpty());
}

void InputBar::addImageThumbnail(const QByteArray &data, const QString &format, const QString &name)
{
    AttachedImage img;
    img.data = data;
    img.format = format;
    img.displayName = name;
    m_attachedImages.append(img);

    auto *layout = static_cast<QHBoxLayout *>(m_imageBar->layout());
    auto &p = ThemeManager::instance().palette();

    auto *container = new QWidget(m_imageBar);
    auto *cLayout = new QHBoxLayout(container);
    cLayout->setContentsMargins(4, 2, 4, 2);
    cLayout->setSpacing(4);

    QPixmap pixmap;
    pixmap.loadFromData(data);
    auto *thumb = new QLabel(container);
    thumb->setPixmap(pixmap.scaled(40, 40, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    thumb->setFixedSize(40, 40);
    cLayout->addWidget(thumb);

    auto *nameLabel = new QLabel(name, container);
    nameLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }").arg(p.text_muted.name()));
    cLayout->addWidget(nameLabel);

    int idx = m_attachedImages.size() - 1;
    auto *removeBtn = new QPushButton("\xc3\x97", container);
    removeBtn->setFixedSize(16, 16);
    removeBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; border-radius: 8px; font-size: 10px; }"
        "QPushButton:hover { background: %3; }")
        .arg(p.bg_raised.name(), p.text_muted.name(), p.hover_raised.name()));
    connect(removeBtn, &QPushButton::clicked, this, [this, idx, container] {
        if (idx < m_attachedImages.size()) {
            m_attachedImages.removeAt(idx);
            container->deleteLater();
            m_imageBar->setVisible(!m_attachedImages.isEmpty());
        }
    });
    cLayout->addWidget(removeBtn);

    container->setStyleSheet(QStringLiteral(
        "QWidget { background: %1; border: 1px solid %2; border-radius: 8px; }")
        .arg(p.bg_raised.name(), p.border_standard.name()));

    // Insert before the stretch
    layout->insertWidget(layout->count() - 1, container);
    m_imageBar->setVisible(true);
}

bool InputBar::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_input) {
        if (event->type() == QEvent::KeyPress) {
            auto *keyEvent = static_cast<QKeyEvent *>(event);

            // Handle popup navigation
            if (m_popupActive && m_contextPopup) {
                if (keyEvent->key() == Qt::Key_Down) {
                    m_contextPopup->selectNext();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Up) {
                    m_contextPopup->selectPrevious();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Tab) {
                    m_contextPopup->acceptSelection();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Escape) {
                    hideContextPopup();
                    return true;
                }
            }

            if (m_slashPopupActive && m_slashPopup) {
                if (keyEvent->key() == Qt::Key_Down) {
                    m_slashPopup->selectNext();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Up) {
                    m_slashPopup->selectPrevious();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Tab) {
                    m_slashPopup->acceptSelection();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Escape) {
                    hideSlashPopup();
                    return true;
                }
            }

            // Enter to send
            if (keyEvent->key() == Qt::Key_Return && !(keyEvent->modifiers() & Qt::ShiftModifier)) {
                if (!m_popupActive && !m_slashPopupActive) {
                    m_sendBtn->click();
                    return true;
                }
            }

            // Detect @ trigger
            if (keyEvent->text() == "@") {
                m_atTriggerPos = m_input->textCursor().position();
                QTimer::singleShot(0, this, [this] { showContextPopup(); });
            }

            // Detect / trigger at start of input
            if (keyEvent->text() == "/" && m_input->toPlainText().trimmed().isEmpty()) {
                QTimer::singleShot(0, this, [this] { showSlashPopup(); });
            }

            // Image paste (Ctrl+V / Cmd+V)
            if (keyEvent->matches(QKeySequence::Paste)) {
                const QMimeData *mime = QApplication::clipboard()->mimeData();
                if (mime && mime->hasImage()) {
                    QImage image = qvariant_cast<QImage>(mime->imageData());
                    if (!image.isNull()) {
                        QByteArray data;
                        QBuffer buffer(&data);
                        buffer.open(QIODevice::WriteOnly);
                        image.save(&buffer, "PNG");
                        addImageThumbnail(data, "png", "pasted-image.png");
                        return true;
                    }
                }
            }

            // Update popup filter as user types after @
            if (m_popupActive && m_contextPopup && m_atTriggerPos >= 0) {
                // Space/Enter while popup is NOT focused closes popup
                // (already handled above for Enter/Tab)
                QTimer::singleShot(0, this, [this] {
                    if (!m_popupActive) return;
                    QString fullText = m_input->toPlainText();
                    int curPos = m_input->textCursor().position();

                    // Dismiss if cursor moved before the @ trigger
                    if (curPos <= m_atTriggerPos) {
                        hideContextPopup();
                        return;
                    }

                    // Extract text after @ to use as filter
                    QString filter = fullText.mid(m_atTriggerPos + 1, curPos - m_atTriggerPos - 1);

                    // Dismiss if user typed a space (end of token)
                    if (filter.contains(' ')) {
                        hideContextPopup();
                        return;
                    }

                    m_contextPopup->updateFilter(filter);

                    // Reposition above input
                    QPoint pos = m_input->mapToGlobal(QPoint(0, 0));
                    pos.setY(pos.y() - m_contextPopup->height() - 4);
                    m_contextPopup->move(pos);
                });
            }

            // Update slash popup filter
            if (m_slashPopupActive && m_slashPopup) {
                QTimer::singleShot(0, this, [this] {
                    if (!m_slashPopupActive) return;
                    QString fullText = m_input->toPlainText();
                    if (fullText.startsWith("/")) {
                        m_slashPopup->updateFilter(fullText);
                        QPoint pos = m_input->mapToGlobal(QPoint(0, 0));
                        pos.setY(pos.y() - m_slashPopup->height() - 4);
                        m_slashPopup->move(pos);
                    } else {
                        hideSlashPopup();
                    }
                });
            }

        } else if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            // Dismiss popups when input loses focus (delay to allow click on popup)
            QTimer::singleShot(200, this, [this] {
                if (!m_input->hasFocus()) {
                    hideContextPopup();
                    hideSlashPopup();
                }
            });
        }
    }
    return QWidget::eventFilter(obj, event);
}

static bool isSupportedImageFile(const QString &path)
{
    static const QStringList exts = {"png", "jpg", "jpeg", "gif", "webp"};
    return exts.contains(QFileInfo(path).suffix().toLower());
}

void InputBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (!event->mimeData()) return;

    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() && isSupportedImageFile(url.toLocalFile())) {
                event->acceptProposedAction();
                auto &p = ThemeManager::instance().palette();
                applyBorderColor(p.blue);
                return;
            }
        }
    }

    if (event->mimeData()->hasImage()) {
        event->acceptProposedAction();
        auto &p = ThemeManager::instance().palette();
        applyBorderColor(p.blue);
        return;
    }
}

void InputBar::dragLeaveEvent(QDragLeaveEvent *)
{
    auto &p = ThemeManager::instance().palette();
    applyBorderColor(p.border_standard);
}

void InputBar::dropEvent(QDropEvent *event)
{
    auto &p = ThemeManager::instance().palette();
    applyBorderColor(p.border_standard);

    if (!event->mimeData()) return;

    if (event->mimeData()->hasUrls()) {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (!url.isLocalFile()) continue;
            QString path = url.toLocalFile();
            if (!isSupportedImageFile(path)) continue;

            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) continue;

            QByteArray data = file.readAll();
            QString format = QFileInfo(path).suffix().toLower();
            if (format == "jpg") format = "jpeg";
            addImageThumbnail(data, format, QFileInfo(path).fileName());
        }
        event->acceptProposedAction();
        return;
    }

    if (event->mimeData()->hasImage()) {
        QImage image = qvariant_cast<QImage>(event->mimeData()->imageData());
        if (!image.isNull()) {
            QByteArray data;
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "PNG");
            addImageThumbnail(data, "png", "dropped-image.png");
            event->acceptProposedAction();
        }
    }
}
