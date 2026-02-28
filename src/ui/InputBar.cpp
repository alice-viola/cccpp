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

InputBar::InputBar(QWidget *parent)
    : QWidget(parent)
{
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 4, 16, 10);
    outerLayout->setSpacing(2);

    // Context indicator strip (shows auto-attached context)
    m_contextIndicator = new QLabel(this);
    m_contextIndicator->setVisible(false);
    m_contextIndicator->setWordWrap(true);
    outerLayout->addWidget(m_contextIndicator);

    // Context pills bar (shows @-mentioned files)
    m_contextPillBar = new QWidget(this);
    m_contextPillBar->setVisible(false);
    auto *pillLayout = new QHBoxLayout(m_contextPillBar);
    pillLayout->setContentsMargins(0, 2, 0, 0);
    pillLayout->setSpacing(4);
    pillLayout->addStretch();
    outerLayout->addWidget(m_contextPillBar);

    // Image thumbnails bar
    m_imageBar = new QWidget(this);
    m_imageBar->setVisible(false);
    auto *imgLayout = new QHBoxLayout(m_imageBar);
    imgLayout->setContentsMargins(0, 2, 0, 0);
    imgLayout->setSpacing(4);
    imgLayout->addStretch();
    outerLayout->addWidget(m_imageBar);

    // Input row
    auto *inputRow = new QHBoxLayout;
    inputRow->setContentsMargins(0, 2, 0, 0);
    inputRow->setSpacing(6);

    m_input = new QTextEdit(this);
    m_input->setObjectName("chatInput");
    m_input->setPlaceholderText("Ask Claude anything... (@ to mention files, / for commands)");
    m_input->setMaximumHeight(80);
    m_input->setMinimumHeight(32);
    m_input->setAcceptRichText(false);
    m_input->installEventFilter(this);

    m_sendBtn = new QPushButton("\xe2\x86\x91", this); // up arrow
    m_sendBtn->setFixedSize(32, 32);

    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_sendBtn, 0, Qt::AlignBottom);
    outerLayout->addLayout(inputRow);

    connect(m_sendBtn, &QPushButton::clicked, this, [this] {
        QString t = text().trimmed();
        if (!t.isEmpty()) {
            // Check for slash commands
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
    m_sendBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; border: none; "
        "border-radius: 14px; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: %3; }"
        "QPushButton:disabled { background: %4; color: %5; }")
        .arg(p.blue.name(), p.on_accent.name(), p.lavender.name(),
             p.bg_raised.name(), p.text_faint.name()));

    m_contextIndicator->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; padding: 2px 4px; }")
        .arg(p.text_muted.name()));
}

void InputBar::applyBorderColor(const QColor &c)
{
    auto &p = ThemeManager::instance().palette();
    m_input->setStyleSheet(
        QStringLiteral("QTextEdit#chatInput { background: %1; color: %2; "
                       "border: 1px solid %3; border-radius: 12px; padding: 6px 10px; "
                       "font-size: 13px; }").arg(p.bg_surface.name(), p.text_primary.name(), c.name()));
}

QString InputBar::text() const
{
    return m_input->toPlainText();
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
    m_sendBtn->setEnabled(enabled);
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
            "QPushButton { background: %1; color: %2; border: none; border-radius: 10px; "
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
        "QWidget { background: %1; border: 1px solid %2; border-radius: 6px; }")
        .arg(p.bg_surface.name(), p.border_standard.name()));

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

        } else if (event->type() == QEvent::FocusIn) {
            auto &p = ThemeManager::instance().palette();
            m_focusAnim->stop();
            m_focusAnim->setStartValue(p.border_standard);
            m_focusAnim->setEndValue(p.mauve);
            m_focusAnim->start();
        } else if (event->type() == QEvent::FocusOut) {
            auto &p = ThemeManager::instance().palette();
            m_focusAnim->stop();
            m_focusAnim->setStartValue(
                m_focusAnim->currentValue().isValid()
                    ? m_focusAnim->currentValue().value<QColor>()
                    : p.mauve);
            m_focusAnim->setEndValue(p.border_standard);
            m_focusAnim->start();
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
