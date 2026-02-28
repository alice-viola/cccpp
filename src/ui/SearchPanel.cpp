#include "ui/SearchPanel.h"
#include "ui/ThemeManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QHeaderView>
#include <QRegularExpression>
#include <QApplication>
#include <QTimer>

SearchPanel::SearchPanel(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QLabel("  SEARCH", this);
    header->setFixedHeight(26);
    layout->addWidget(header);

    auto *controlsLayout = new QVBoxLayout();
    controlsLayout->setContentsMargins(8, 6, 8, 6);
    controlsLayout->setSpacing(4);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem("File Name", static_cast<int>(SearchMode::FileName));
    m_modeCombo->addItem("Text in Files", static_cast<int>(SearchMode::TextContent));
    controlsLayout->addWidget(m_modeCombo);

    auto *inputRow = new QHBoxLayout();
    inputRow->setSpacing(4);
    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText("Search...");
    m_searchInput->setClearButtonEnabled(true);
    inputRow->addWidget(m_searchInput, 1);

    m_searchBtn = new QPushButton("Go", this);
    m_searchBtn->setFixedWidth(36);
    inputRow->addWidget(m_searchBtn);
    controlsLayout->addLayout(inputRow);

    auto *optionsRow = new QHBoxLayout();
    optionsRow->setSpacing(8);
    m_caseSensitive = new QCheckBox("Aa", this);
    m_caseSensitive->setToolTip("Case sensitive");
    m_regexCheck = new QCheckBox(".*", this);
    m_regexCheck->setToolTip("Use regular expression");
    optionsRow->addWidget(m_caseSensitive);
    optionsRow->addWidget(m_regexCheck);
    optionsRow->addStretch();
    controlsLayout->addLayout(optionsRow);

    layout->addLayout(controlsLayout);

    m_results = new QTreeWidget(this);
    m_results->setHeaderHidden(true);
    m_results->setRootIsDecorated(true);
    m_results->setIndentation(14);
    m_results->setColumnCount(1);
    layout->addWidget(m_results, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setFixedHeight(20);
    m_statusLabel->setContentsMargins(8, 0, 8, 0);
    layout->addWidget(m_statusLabel);

    connect(m_searchInput, &QLineEdit::returnPressed, this, &SearchPanel::onSearch);
    connect(m_searchBtn, &QPushButton::clicked, this, &SearchPanel::onSearch);
    connect(m_results, &QTreeWidget::itemClicked, this, &SearchPanel::onResultClicked);
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, &SearchPanel::onResultClicked);

    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &SearchPanel::applyThemeColors);
}

void SearchPanel::setRootPath(const QString &path)
{
    m_rootPath = path;
}

void SearchPanel::onSearch()
{
    QString query = m_searchInput->text().trimmed();
    if (query.isEmpty() || m_rootPath.isEmpty())
        return;

    m_results->clear();
    m_resultCount = 0;

    auto mode = static_cast<SearchMode>(m_modeCombo->currentData().toInt());
    if (mode == SearchMode::FileName)
        searchFileNames(query);
    else
        searchTextContent(query);
}

void SearchPanel::searchFileNames(const QString &query)
{
    m_statusLabel->setText("Searching files...");
    QApplication::processEvents();

    QDir root(m_rootPath);
    addFileResults(m_rootPath, query, root);

    m_statusLabel->setText(QStringLiteral("%1 file(s) found").arg(m_resultCount));
}

void SearchPanel::addFileResults(const QString &rootPath, const QString &query, const QDir &dir)
{
    if (m_resultCount >= MAX_RESULTS)
        return;

    Qt::CaseSensitivity cs = m_caseSensitive->isChecked()
                                 ? Qt::CaseSensitive : Qt::CaseInsensitive;
    bool useRegex = m_regexCheck->isChecked();

    QRegularExpression regex;
    if (useRegex) {
        QRegularExpression::PatternOptions opts = QRegularExpression::NoPatternOption;
        if (!m_caseSensitive->isChecked())
            opts |= QRegularExpression::CaseInsensitiveOption;
        regex.setPattern(query);
        regex.setPatternOptions(opts);
    }

    QDirIterator it(rootPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    QDir rootDir(rootPath);

    static const QStringList skipDirs = {
        ".git", "node_modules", "__pycache__", ".cache", "build",
        ".next", "dist", ".venv", "venv", ".tox"
    };

    while (it.hasNext() && m_resultCount < MAX_RESULTS) {
        it.next();
        QFileInfo info = it.fileInfo();

        if (info.isDir()) {
            if (skipDirs.contains(info.fileName()))
                it.next();
            continue;
        }

        QString fileName = info.fileName();
        bool matched = false;

        if (useRegex)
            matched = regex.match(fileName).hasMatch();
        else
            matched = fileName.contains(query, cs);

        if (matched) {
            QString relPath = rootDir.relativeFilePath(info.absoluteFilePath());
            auto *item = new QTreeWidgetItem(m_results);
            item->setText(0, relPath);
            item->setData(0, Qt::UserRole, info.absoluteFilePath());
            item->setData(0, Qt::UserRole + 1, 0);
            item->setToolTip(0, info.absoluteFilePath());
            m_resultCount++;
        }
    }
}

void SearchPanel::searchTextContent(const QString &query)
{
    m_statusLabel->setText("Searching content...");
    QApplication::processEvents();

    if (m_grepProcess) {
        m_grepProcess->kill();
        m_grepProcess->deleteLater();
    }

    m_grepProcess = new QProcess(this);
    m_grepProcess->setWorkingDirectory(m_rootPath);

    QStringList args;
    args << "--line-number" << "--recursive" << "--with-filename";

    static const QStringList excludeDirs = {
        ".git", "node_modules", "__pycache__", ".cache", "build",
        ".next", "dist", ".venv", "venv", ".tox"
    };
    for (const QString &d : excludeDirs)
        args << QStringLiteral("--exclude-dir=%1").arg(d);

    if (!m_caseSensitive->isChecked())
        args << "--ignore-case";

    if (m_regexCheck->isChecked())
        args << "--extended-regexp";
    else
        args << "--fixed-strings";

    args << "--max-count=50";
    args << "--" << query << ".";

    connect(m_grepProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int, QProcess::ExitStatus) {
        if (!m_grepProcess)
            return;

        QDir rootDir(m_rootPath);
        QString output = QString::fromUtf8(m_grepProcess->readAllStandardOutput());
        QStringList lines = output.split('\n', Qt::SkipEmptyParts);

        QMap<QString, QTreeWidgetItem *> fileItems;

        for (const QString &line : lines) {
            if (m_resultCount >= MAX_RESULTS)
                break;

            int firstColon = line.indexOf(':');
            if (firstColon < 0) continue;
            int secondColon = line.indexOf(':', firstColon + 1);
            if (secondColon < 0) continue;

            QString relFile = line.left(firstColon);
            if (relFile.startsWith("./"))
                relFile = relFile.mid(2);
            int lineNum = line.mid(firstColon + 1, secondColon - firstColon - 1).toInt();
            QString matchText = line.mid(secondColon + 1).trimmed();
            if (matchText.length() > 200)
                matchText = matchText.left(200) + "...";

            QString absPath = rootDir.absoluteFilePath(relFile);

            QTreeWidgetItem *fileItem = fileItems.value(relFile);
            if (!fileItem) {
                fileItem = new QTreeWidgetItem(m_results);
                fileItem->setText(0, relFile);
                fileItem->setData(0, Qt::UserRole, absPath);
                fileItem->setData(0, Qt::UserRole + 1, 1);
                fileItems[relFile] = fileItem;
            }

            auto *matchItem = new QTreeWidgetItem(fileItem);
            matchItem->setText(0, QStringLiteral("%1: %2").arg(lineNum).arg(matchText));
            matchItem->setData(0, Qt::UserRole, absPath);
            matchItem->setData(0, Qt::UserRole + 1, lineNum);
            matchItem->setToolTip(0, matchText);
            m_resultCount++;
        }

        int fileCount = fileItems.size();
        m_statusLabel->setText(QStringLiteral("%1 match(es) in %2 file(s)")
                                   .arg(m_resultCount).arg(fileCount));

        m_grepProcess->deleteLater();
        m_grepProcess = nullptr;
    });

    m_grepProcess->start("grep", args);
    if (!m_grepProcess->waitForStarted(3000)) {
        m_statusLabel->setText("grep not available, ensure it is installed");
        m_grepProcess->deleteLater();
        m_grepProcess = nullptr;
    }
}

void SearchPanel::onResultClicked(QTreeWidgetItem *item, int)
{
    if (!item) return;

    QString filePath = item->data(0, Qt::UserRole).toString();
    int line = item->data(0, Qt::UserRole + 1).toInt();

    if (filePath.isEmpty())
        return;

    if (line <= 0 && item->childCount() > 0) {
        item->setExpanded(!item->isExpanded());
        return;
    }

    emit fileSelected(filePath, line);
}

void SearchPanel::applyThemeColors()
{
    const auto &p = ThemeManager::instance().palette();

    findChild<QLabel *>()->setStyleSheet(QStringLiteral(
        "QLabel { background: %1; color: %2; font-size: 11px; "
        "font-weight: bold; letter-spacing: 1px; padding-left: 8px; "
        "border-bottom: 1px solid %3; }")
        .arg(p.bg_base.name(), p.text_muted.name(), p.border_standard.name()));

    m_searchInput->setStyleSheet(QStringLiteral(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 4px; padding: 3px 6px; font-size: 12px; }"
        "QLineEdit:focus { border-color: %4; }")
        .arg(p.bg_surface.name(), p.text_primary.name(),
             p.border_standard.name(), p.border_focus.name()));

    m_statusLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: %1; font-size: 11px; background: %2; "
        "border-top: 1px solid %3; }")
        .arg(p.text_muted.name(), p.bg_base.name(), p.border_standard.name()));

    QString checkStyle = QStringLiteral(
        "QCheckBox { color: %1; font-size: 11px; spacing: 2px; }"
        "QCheckBox::indicator { width: 14px; height: 14px; border-radius: 3px; "
        "border: 1px solid %2; background: %3; }"
        "QCheckBox::indicator:checked { background: %4; border-color: %4; }")
        .arg(p.text_muted.name(), p.border_standard.name(),
             p.bg_surface.name(), p.mauve.name());
    m_caseSensitive->setStyleSheet(checkStyle);
    m_regexCheck->setStyleSheet(checkStyle);
}
