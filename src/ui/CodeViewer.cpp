#include "ui/CodeViewer.h"
#include "ui/FileIconProvider.h"
#include "ui/BreadcrumbBar.h"
#include "ui/DiffSplitView.h"
#include "ui/InlineDiffOverlay.h"
#include "ui/InlineEditBar.h"
#include "ui/ThemeManager.h"
#include "util/MarkdownRenderer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QPainter>
#include <QResizeEvent>
#include <QTimer>
#include <QTabBar>

// ---------------------------------------------------------------------------
// Empty state widget — shown when no files are open
// ---------------------------------------------------------------------------

class CodeViewerEmptyState : public QWidget {
public:
    explicit CodeViewerEmptyState(QWidget *parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        const auto &pal = ThemeManager::instance().palette();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), pal.bg_window);

        QPoint c = rect().center();

        // File icon — two stacked rounded rects
        p.setPen(Qt::NoPen);
        p.setBrush(pal.bg_raised);
        p.drawRoundedRect(c.x() - 12, c.y() - 52, 34, 44, 3, 3);
        p.setBrush(pal.surface0);
        p.drawRoundedRect(c.x() - 18, c.y() - 58, 34, 44, 3, 3);
        // Folded corner cutout
        p.setBrush(pal.bg_window);
        QPolygon corner;
        corner << QPoint(c.x() + 6,  c.y() - 58)
               << QPoint(c.x() + 16, c.y() - 48)
               << QPoint(c.x() + 16, c.y() - 58);
        p.drawPolygon(corner);
        // Folded corner fill
        p.setBrush(pal.hover_raised);
        QPolygon fold;
        fold << QPoint(c.x() + 6,  c.y() - 58)
             << QPoint(c.x() + 16, c.y() - 48)
             << QPoint(c.x() + 6,  c.y() - 48);
        p.drawPolygon(fold);
        // Text line hints inside icon
        p.setBrush(pal.pressed_raised);
        for (int i = 0; i < 3; ++i) {
            int lw = (i == 2) ? 14 : 22;
            p.drawRoundedRect(c.x() - 14, c.y() - 46 + i * 9, lw, 3, 1, 1);
        }

        // Primary label
        QFont tf = font();
        tf.setPixelSize(14);
        tf.setWeight(QFont::Medium);
        p.setFont(tf);
        p.setPen(pal.text_faint);
        p.drawText(QRect(c.x() - 150, c.y() - 4, 300, 22), Qt::AlignCenter, "No file open");

        // Hint line
        QFont hf = font();
        hf.setPixelSize(11);
        p.setFont(hf);
        p.setPen(pal.surface0);
        p.drawText(QRect(c.x() - 220, c.y() + 22, 440, 18), Qt::AlignCenter,
                   "Select a file from the explorer or ask Claude to open one");
    }
};

#ifndef NO_QSCINTILLA
#include <Qsci/qscilexercpp.h>
#include <Qsci/qscilexerpython.h>
#include <Qsci/qscilexerjavascript.h>
#include <Qsci/qscilexerbash.h>
#include <Qsci/qscilexerjava.h>
#include <Qsci/qscilexercsharp.h>
#include <Qsci/qscilexerruby.h>
#include <Qsci/qscilexerhtml.h>
#include <Qsci/qscilexercss.h>
#include <Qsci/qscilexerxml.h>
#include <Qsci/qscilexeryaml.h>
#include <Qsci/qscilexersql.h>
#include <Qsci/qscilexerlua.h>
#include <Qsci/qscilexerperl.h>
#include <Qsci/qscilexermakefile.h>
#include <Qsci/qscilexercmake.h>
#include <Qsci/qscilexerdiff.h>
#include <Qsci/qscilexerjson.h>
#include <Qsci/qscilexermarkdown.h>
#include <Qsci/qscilexerbatch.h>
#include <Qsci/qscilexerproperties.h>
#include <Qsci/qscilexertex.h>
#include <Qsci/qscilexerfortran.h>
#include <Qsci/qscilexerpascal.h>
#include <Qsci/qscilexerd.h>
#include <Qsci/qscilexertcl.h>
#include <Qsci/qscilexercoffeescript.h>
#include <Qsci/qscilexeroctave.h>
#else
#include <QTextDocument>
#endif

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

CodeViewer::CodeViewer(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Empty state (shown when no tabs are open)
    m_emptyState = new CodeViewerEmptyState(this);
    layout->addWidget(m_emptyState);

    m_breadcrumb = new BreadcrumbBar(this);
    layout->addWidget(m_breadcrumb);

    // Tab widget with diff toggle button in corner
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);

    m_diffToggleBtn = new QPushButton("Diff", this);
    m_diffToggleBtn->setCheckable(true);
    m_diffToggleBtn->setFixedSize(40, 20);
    m_diffToggleBtn->setToolTip("Toggle side-by-side diff view");
    m_tabWidget->setCornerWidget(m_diffToggleBtn, Qt::TopRightCorner);
    connect(m_diffToggleBtn, &QPushButton::clicked, this, &CodeViewer::toggleDiffMode);

    layout->addWidget(m_tabWidget);

    // Initial state: no tabs → show empty state
    m_tabWidget->hide();

    m_fileWatcher = new QFileSystemWatcher(this);
    connect(m_fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &CodeViewer::onExternalFileChanged);

    connect(m_tabWidget, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        if (!confirmCloseTab(idx))
            return;

        if (m_tabs.contains(idx))
            unwatchFile(m_tabs[idx].filePath);

        m_tabs.remove(idx);
        m_tabWidget->removeTab(idx);

        QMap<int, FileTab> reindexed;
        int i = 0;
        for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it, ++i)
            reindexed[i] = it.value();
        m_tabs = reindexed;
        updateEmptyState();
    });

    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        if (m_tabs.contains(idx))
            m_diffToggleBtn->setChecked(m_tabs[idx].inDiffMode);
        if (m_tabs.contains(idx) && m_breadcrumb)
            m_breadcrumb->setFilePath(m_tabs[idx].filePath, m_rootPath);
    });

    // Apply theme colors and listen for changes
    applyThemeColors();
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &CodeViewer::applyThemeColors);
}

// ---------------------------------------------------------------------------
// Editor factory
// ---------------------------------------------------------------------------

#ifndef NO_QSCINTILLA

static void applyThemeToLexer(QsciLexer *lexer)
{
    if (!lexer) return;

    const auto &pal = ThemeManager::instance().palette();

    QFont monoFont("SF Mono", 13);
    lexer->setFont(monoFont);
    lexer->setDefaultFont(monoFont);
    lexer->setPaper(pal.bg_window);
    lexer->setDefaultPaper(pal.bg_window);
    lexer->setColor(pal.text_primary);
    lexer->setDefaultColor(pal.text_primary);

    for (int i = 0; i <= 255; ++i) {
        lexer->setPaper(pal.bg_window, i);
        lexer->setColor(pal.text_primary, i);
        lexer->setFont(monoFont, i);
    }

    // --- C / C++ / Objective-C (also covers Java, C# which inherit QsciLexerCPP) ---
    if (auto *L = qobject_cast<QsciLexerCPP *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerCPP::Keyword);
        L->setColor(pal.green, QsciLexerCPP::SingleQuotedString);
        L->setColor(pal.green, QsciLexerCPP::DoubleQuotedString);
        L->setColor(pal.overlay0, QsciLexerCPP::Comment);
        L->setColor(pal.overlay0, QsciLexerCPP::CommentLine);
        L->setColor(pal.overlay0, QsciLexerCPP::CommentDoc);
        L->setColor(pal.peach, QsciLexerCPP::Number);
        L->setColor(pal.blue, QsciLexerCPP::PreProcessor);
        L->setColor(pal.red, QsciLexerCPP::Operator);
        L->setColor(pal.text_primary, QsciLexerCPP::Identifier);
        L->setColor(pal.mauve, QsciLexerCPP::KeywordSet2);
        L->setColor(pal.green, QsciLexerCPP::RawString);
        return;
    }

    // --- Python ---
    if (auto *L = qobject_cast<QsciLexerPython *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerPython::Keyword);
        L->setColor(pal.green, QsciLexerPython::SingleQuotedString);
        L->setColor(pal.green, QsciLexerPython::DoubleQuotedString);
        L->setColor(pal.green, QsciLexerPython::TripleSingleQuotedString);
        L->setColor(pal.green, QsciLexerPython::TripleDoubleQuotedString);
        L->setColor(pal.overlay0, QsciLexerPython::Comment);
        L->setColor(pal.peach, QsciLexerPython::Number);
        L->setColor(pal.blue, QsciLexerPython::Decorator);
        L->setColor(pal.yellow, QsciLexerPython::FunctionMethodName);
        L->setColor(pal.red, QsciLexerPython::Operator);
        return;
    }

    // --- JavaScript / TypeScript ---
    if (auto *L = qobject_cast<QsciLexerJavaScript *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerJavaScript::Keyword);
        L->setColor(pal.green, QsciLexerJavaScript::SingleQuotedString);
        L->setColor(pal.green, QsciLexerJavaScript::DoubleQuotedString);
        L->setColor(pal.overlay0, QsciLexerJavaScript::Comment);
        L->setColor(pal.overlay0, QsciLexerJavaScript::CommentLine);
        L->setColor(pal.overlay0, QsciLexerJavaScript::CommentDoc);
        L->setColor(pal.peach, QsciLexerJavaScript::Number);
        L->setColor(pal.red, QsciLexerJavaScript::Operator);
        L->setColor(pal.text_primary, QsciLexerJavaScript::Identifier);
        return;
    }

    // --- Ruby ---
    if (auto *L = qobject_cast<QsciLexerRuby *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerRuby::Keyword);
        L->setColor(pal.green, QsciLexerRuby::DoubleQuotedString);
        L->setColor(pal.green, QsciLexerRuby::SingleQuotedString);
        L->setColor(pal.green, QsciLexerRuby::HereDocument);
        L->setColor(pal.overlay0, QsciLexerRuby::Comment);
        L->setColor(pal.peach, QsciLexerRuby::Number);
        L->setColor(pal.red, QsciLexerRuby::Operator);
        L->setColor(pal.yellow, QsciLexerRuby::FunctionMethodName);
        L->setColor(pal.blue, QsciLexerRuby::ClassName);
        L->setColor(pal.red, QsciLexerRuby::Symbol);
        L->setColor(pal.peach, QsciLexerRuby::Regex);
        return;
    }

    // --- HTML (also handles embedded JS/CSS/PHP) ---
    if (auto *L = qobject_cast<QsciLexerHTML *>(lexer)) {
        L->setColor(pal.red, QsciLexerHTML::Tag);
        L->setColor(pal.red, QsciLexerHTML::UnknownTag);
        L->setColor(pal.peach, QsciLexerHTML::Attribute);
        L->setColor(pal.peach, QsciLexerHTML::UnknownAttribute);
        L->setColor(pal.green, QsciLexerHTML::HTMLDoubleQuotedString);
        L->setColor(pal.green, QsciLexerHTML::HTMLSingleQuotedString);
        L->setColor(pal.peach, QsciLexerHTML::HTMLNumber);
        L->setColor(pal.overlay0, QsciLexerHTML::HTMLComment);
        L->setColor(pal.sky, QsciLexerHTML::Entity);
        L->setColor(pal.text_primary, QsciLexerHTML::OtherInTag);
        return;
    }

    // --- CSS ---
    if (auto *L = qobject_cast<QsciLexerCSS *>(lexer)) {
        L->setColor(pal.red, QsciLexerCSS::Tag);
        L->setColor(pal.blue, QsciLexerCSS::ClassSelector);
        L->setColor(pal.peach, QsciLexerCSS::IDSelector);
        L->setColor(pal.mauve, QsciLexerCSS::PseudoClass);
        L->setColor(pal.sky, QsciLexerCSS::CSS1Property);
        L->setColor(pal.sky, QsciLexerCSS::CSS2Property);
        L->setColor(pal.sky, QsciLexerCSS::CSS3Property);
        L->setColor(pal.green, QsciLexerCSS::DoubleQuotedString);
        L->setColor(pal.green, QsciLexerCSS::SingleQuotedString);
        L->setColor(pal.peach, QsciLexerCSS::Value);
        L->setColor(pal.overlay0, QsciLexerCSS::Comment);
        L->setColor(pal.red, QsciLexerCSS::Important);
        L->setColor(pal.red, QsciLexerCSS::AtRule);
        return;
    }

    // --- XML ---
    if (auto *L = qobject_cast<QsciLexerXML *>(lexer)) {
        L->setColor(pal.red, QsciLexerHTML::Tag);
        L->setColor(pal.peach, QsciLexerHTML::Attribute);
        L->setColor(pal.green, QsciLexerHTML::HTMLDoubleQuotedString);
        L->setColor(pal.green, QsciLexerHTML::HTMLSingleQuotedString);
        L->setColor(pal.overlay0, QsciLexerHTML::HTMLComment);
        L->setColor(pal.sky, QsciLexerHTML::Entity);
        return;
    }

    // --- YAML ---
    if (auto *L = qobject_cast<QsciLexerYAML *>(lexer)) {
        L->setColor(pal.blue, QsciLexerYAML::Identifier);
        L->setColor(pal.mauve, QsciLexerYAML::Keyword);
        L->setColor(pal.peach, QsciLexerYAML::Number);
        L->setColor(pal.overlay0, QsciLexerYAML::Comment);
        L->setColor(pal.red, QsciLexerYAML::Reference);
        L->setColor(pal.red, QsciLexerYAML::Operator);
        L->setColor(pal.sky, QsciLexerYAML::DocumentDelimiter);
        L->setColor(pal.yellow, QsciLexerYAML::TextBlockMarker);
        return;
    }

    // --- SQL ---
    if (auto *L = qobject_cast<QsciLexerSQL *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerSQL::Keyword);
        L->setColor(pal.green, QsciLexerSQL::SingleQuotedString);
        L->setColor(pal.green, QsciLexerSQL::DoubleQuotedString);
        L->setColor(pal.overlay0, QsciLexerSQL::Comment);
        L->setColor(pal.overlay0, QsciLexerSQL::CommentLine);
        L->setColor(pal.overlay0, QsciLexerSQL::CommentDoc);
        L->setColor(pal.peach, QsciLexerSQL::Number);
        L->setColor(pal.red, QsciLexerSQL::Operator);
        L->setColor(pal.blue, QsciLexerSQL::KeywordSet5);
        L->setColor(pal.blue, QsciLexerSQL::KeywordSet6);
        return;
    }

    // --- JSON ---
    if (auto *L = qobject_cast<QsciLexerJSON *>(lexer)) {
        L->setColor(pal.blue, QsciLexerJSON::Property);
        L->setColor(pal.green, QsciLexerJSON::String);
        L->setColor(pal.peach, QsciLexerJSON::Number);
        L->setColor(pal.mauve, QsciLexerJSON::Keyword);
        L->setColor(pal.red, QsciLexerJSON::Operator);
        L->setColor(pal.sky, QsciLexerJSON::EscapeSequence);
        L->setColor(pal.overlay0, QsciLexerJSON::CommentLine);
        L->setColor(pal.overlay0, QsciLexerJSON::CommentBlock);
        L->setColor(pal.red, QsciLexerJSON::Error);
        return;
    }

    // --- Markdown ---
    if (auto *L = qobject_cast<QsciLexerMarkdown *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerMarkdown::Header1);
        L->setColor(pal.blue, QsciLexerMarkdown::Header2);
        L->setColor(pal.sky, QsciLexerMarkdown::Header3);
        L->setColor(pal.green, QsciLexerMarkdown::Header4);
        L->setColor(pal.yellow, QsciLexerMarkdown::Header5);
        L->setColor(pal.peach, QsciLexerMarkdown::Header6);
        // Strong/StrongEmphasis not available in all QScintilla versions
        L->setColor(pal.blue, QsciLexerMarkdown::Link);
        L->setColor(pal.green, QsciLexerMarkdown::CodeBackticks);
        L->setColor(pal.green, QsciLexerMarkdown::CodeDoubleBackticks);
        L->setColor(pal.green, QsciLexerMarkdown::CodeBlock);
        L->setColor(pal.overlay0, QsciLexerMarkdown::BlockQuote);
        L->setColor(pal.red, QsciLexerMarkdown::HorizontalRule);
        L->setColor(pal.peach, QsciLexerMarkdown::UnorderedListItem);
        L->setColor(pal.peach, QsciLexerMarkdown::OrderedListItem);
        L->setColor(pal.red, QsciLexerMarkdown::StrikeOut);
        return;
    }

    // --- Lua ---
    if (auto *L = qobject_cast<QsciLexerLua *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerLua::Keyword);
        L->setColor(pal.green, QsciLexerLua::String);
        L->setColor(pal.green, QsciLexerLua::LiteralString);
        L->setColor(pal.overlay0, QsciLexerLua::Comment);
        L->setColor(pal.overlay0, QsciLexerLua::LineComment);
        L->setColor(pal.peach, QsciLexerLua::Number);
        L->setColor(pal.red, QsciLexerLua::Operator);
        L->setColor(pal.blue, QsciLexerLua::BasicFunctions);
        return;
    }

    // --- Perl ---
    if (auto *L = qobject_cast<QsciLexerPerl *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerPerl::Keyword);
        L->setColor(pal.green, QsciLexerPerl::DoubleQuotedString);
        L->setColor(pal.green, QsciLexerPerl::SingleQuotedString);
        L->setColor(pal.green, QsciLexerPerl::HereDocumentDelimiter);
        L->setColor(pal.overlay0, QsciLexerPerl::Comment);
        L->setColor(pal.peach, QsciLexerPerl::Number);
        L->setColor(pal.red, QsciLexerPerl::Operator);
        L->setColor(pal.peach, QsciLexerPerl::Regex);
        L->setColor(pal.blue, QsciLexerPerl::Array);
        L->setColor(pal.sky, QsciLexerPerl::Hash);
        return;
    }

    // --- Bash / Shell ---
    if (auto *L = qobject_cast<QsciLexerBash *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerBash::Keyword);
        L->setColor(pal.green, QsciLexerBash::DoubleQuotedString);
        L->setColor(pal.green, QsciLexerBash::SingleQuotedString);
        L->setColor(pal.overlay0, QsciLexerBash::Comment);
        L->setColor(pal.peach, QsciLexerBash::Number);
        L->setColor(pal.red, QsciLexerBash::Operator);
        L->setColor(pal.blue, QsciLexerBash::Backticks);
        L->setColor(pal.sky, QsciLexerBash::Scalar);
        L->setColor(pal.yellow, QsciLexerBash::ParameterExpansion);
        return;
    }

    // --- Makefile ---
    if (auto *L = qobject_cast<QsciLexerMakefile *>(lexer)) {
        L->setColor(pal.blue, QsciLexerMakefile::Target);
        L->setColor(pal.green, QsciLexerMakefile::Variable);
        L->setColor(pal.overlay0, QsciLexerMakefile::Comment);
        L->setColor(pal.sky, QsciLexerMakefile::Preprocessor);
        L->setColor(pal.red, QsciLexerMakefile::Operator);
        return;
    }

    // --- CMake ---
    if (auto *L = qobject_cast<QsciLexerCMake *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerCMake::KeywordSet3);
        L->setColor(pal.green, QsciLexerCMake::String);
        L->setColor(pal.green, QsciLexerCMake::StringLeftQuote);
        L->setColor(pal.green, QsciLexerCMake::StringRightQuote);
        L->setColor(pal.overlay0, QsciLexerCMake::Comment);
        L->setColor(pal.blue, QsciLexerCMake::Function);
        L->setColor(pal.sky, QsciLexerCMake::Variable);
        L->setColor(pal.peach, QsciLexerCMake::Number);
        return;
    }

    // --- Diff / Patch ---
    if (auto *L = qobject_cast<QsciLexerDiff *>(lexer)) {
        L->setColor(pal.green, QsciLexerDiff::LineAdded);
        L->setColor(pal.red, QsciLexerDiff::LineRemoved);
        L->setColor(pal.blue, QsciLexerDiff::Header);
        L->setColor(pal.mauve, QsciLexerDiff::Position);
        L->setColor(pal.overlay0, QsciLexerDiff::Comment);
        return;
    }

    // --- Properties / INI / .env ---
    if (auto *L = qobject_cast<QsciLexerProperties *>(lexer)) {
        L->setColor(pal.blue, QsciLexerProperties::Section);
        L->setColor(pal.yellow, QsciLexerProperties::Assignment);
        L->setColor(pal.green, QsciLexerProperties::DefaultValue);
        L->setColor(pal.overlay0, QsciLexerProperties::Comment);
        return;
    }

    // --- TeX / LaTeX ---
    if (auto *L = qobject_cast<QsciLexerTeX *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerTeX::Command);
        L->setColor(pal.blue, QsciLexerTeX::Group);
        L->setColor(pal.red, QsciLexerTeX::Special);
        L->setColor(pal.green, QsciLexerTeX::Text);
        return;
    }

    // --- Batch (Windows CMD) ---
    if (auto *L = qobject_cast<QsciLexerBatch *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerBatch::Keyword);
        L->setColor(pal.overlay0, QsciLexerBatch::Comment);
        L->setColor(pal.sky, QsciLexerBatch::Variable);
        L->setColor(pal.red, QsciLexerBatch::Operator);
        return;
    }

    // --- CoffeeScript ---
    if (auto *L = qobject_cast<QsciLexerCoffeeScript *>(lexer)) {
        L->setColor(pal.mauve, QsciLexerCoffeeScript::Keyword);
        L->setColor(pal.green, QsciLexerCoffeeScript::SingleQuotedString);
        L->setColor(pal.green, QsciLexerCoffeeScript::DoubleQuotedString);
        L->setColor(pal.overlay0, QsciLexerCoffeeScript::Comment);
        L->setColor(pal.overlay0, QsciLexerCoffeeScript::CommentLine);
        L->setColor(pal.peach, QsciLexerCoffeeScript::Number);
        L->setColor(pal.red, QsciLexerCoffeeScript::Operator);
        return;
    }
}

QsciScintilla *CodeViewer::createEditor()
{
    auto *ed = new QsciScintilla(this);
    ed->setReadOnly(false);
    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginsFont(QFont("SF Mono", 12));
    ed->setFont(QFont("SF Mono", 13));
    ed->setTabWidth(4);
    ed->setIndentationsUseTabs(false);
    ed->setAutoIndent(true);
    ed->setIndentationGuides(true);
    ed->setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
    ed->setBraceMatching(QsciScintilla::SloppyBraceMatch);
    ed->setCaretLineVisible(true);
    ed->setEolMode(QsciScintilla::EolUnix);
    ed->setUtf8(true);

    // Diff markers (green = added, red = removed)
    ed->markerDefine(QsciScintilla::Background, 1);
    ed->markerDefine(QsciScintilla::Background, 2);

    applyEditorThemeColors(ed);

    return ed;
}

void CodeViewer::applyEditorThemeColors(QsciScintilla *ed)
{
    const auto &pal = ThemeManager::instance().palette();

    ed->setMarginsForegroundColor(pal.overlay0);
    ed->setMarginsBackgroundColor(pal.bg_window);
    ed->setCaretForegroundColor(pal.text_primary);
    ed->setCaretLineBackgroundColor(pal.white_2pct);
    ed->setPaper(pal.bg_window);
    ed->setColor(pal.text_primary);
    ed->setIndentationGuidesForegroundColor(pal.border_subtle);
    ed->setFoldMarginColors(pal.bg_window, pal.bg_window);
    ed->setMatchedBraceForegroundColor(pal.yellow);
    ed->setMatchedBraceBackgroundColor(pal.pressed_raised);
    ed->setSelectionBackgroundColor(pal.pressed_raised);
    ed->setSelectionForegroundColor(pal.text_primary);

    // Diff marker backgrounds
    ed->setMarkerBackgroundColor(pal.diff_add_bg, 1);
    ed->setMarkerBackgroundColor(pal.diff_del_bg, 2);
}

#else // NO_QSCINTILLA fallback

QPlainTextEdit *CodeViewer::createEditor()
{
    const auto &pal = ThemeManager::instance().palette();
    auto *ed = new QPlainTextEdit(this);
    ed->setReadOnly(false);
    ed->setTabStopDistance(32);
    ed->setStyleSheet(
        QStringLiteral("QPlainTextEdit { background: %1; color: %2; border: none; "
        "font-family: 'SF Mono','JetBrains Mono','Fira Code','Menlo','Consolas',monospace; font-size: 13px; }")
            .arg(pal.bg_window.name(), pal.text_primary.name()));
    return ed;
}

#endif // NO_QSCINTILLA

// ---------------------------------------------------------------------------
// Editor signal wiring (dirty tracking)
// ---------------------------------------------------------------------------

void CodeViewer::connectEditorSignals(FileTab &tab)
{
#ifndef NO_QSCINTILLA
    auto *editor = tab.editor;
    connect(editor, &QsciScintilla::modificationChanged, this,
            [this, editor](bool modified) {
        for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
            if (it->editor == editor) {
                it->dirty = modified;
                updateTabTitle(it.key());
                emit dirtyStateChanged(it->filePath, modified);
                break;
            }
        }
    });
#else
    auto *editor = tab.editor;
    connect(editor->document(), &QTextDocument::modificationChanged, this,
            [this, editor](bool modified) {
        for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
            if (it->editor == editor) {
                it->dirty = modified;
                updateTabTitle(it.key());
                emit dirtyStateChanged(it->filePath, modified);
                break;
            }
        }
    });
#endif
}

// ---------------------------------------------------------------------------
// Theme color application
// ---------------------------------------------------------------------------

void CodeViewer::applyThemeColors()
{
    const auto &pal = ThemeManager::instance().palette();

    // Diff toggle button
    m_diffToggleBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton { background: %1; color: %2; border: none; border-radius: 4px; "
            "font-size: 11px; margin: 2px 4px; }"
            "QPushButton:hover { color: %3; background: %4; }"
            "QPushButton:checked { background: %5; color: %6; }")
            .arg(pal.bg_raised.name(), pal.text_muted.name(),
                 pal.text_primary.name(), pal.hover_raised.name(),
                 pal.success_btn_bg.name(), pal.green.name()));

    // Re-apply editor colors for all open tabs
#ifndef NO_QSCINTILLA
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->editor) {
            applyEditorThemeColors(it->editor);
            // Re-apply lexer colors if a lexer is set
            QsciLexer *lexer = it->editor->lexer();
            if (lexer) {
                applyThemeToLexer(lexer);
            } else {
                it->editor->setPaper(pal.bg_window);
                it->editor->setColor(pal.text_primary);
            }
        }
    }
#endif

    // Force repaint of empty state
    if (m_emptyState)
        m_emptyState->update();
}

// ---------------------------------------------------------------------------
// Empty state management
// ---------------------------------------------------------------------------

void CodeViewer::updateEmptyState()
{
    bool empty = m_tabs.isEmpty();
    m_emptyState->setVisible(empty);
    m_tabWidget->setVisible(!empty);
    if (m_breadcrumb) m_breadcrumb->setVisible(!empty);
}

void CodeViewer::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_inlineDiffOverlay && m_inlineDiffOverlay->isVisible()) {
        int w = m_tabWidget->width();
        int tabBarH = m_tabWidget->tabBar()->height();
        int barH = qMin(m_inlineDiffOverlay->sizeHint().height(), 200);
        m_inlineDiffOverlay->setGeometry(0, tabBarH, w, barH);
        m_inlineDiffOverlay->raise();
    }
}

// ---------------------------------------------------------------------------
// File loading / refreshing
// ---------------------------------------------------------------------------

void CodeViewer::loadFile(const QString &filePath)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->filePath == filePath) {
            m_tabWidget->setCurrentIndex(it.key());
            return;
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(file.readAll());
    auto *editor = createEditor();

    // Stack: index 0 = editor, index 1 = diff split view
    auto *stack = new QStackedWidget(this);
    stack->addWidget(editor);

    auto *diffView = new DiffSplitView(this);
    stack->addWidget(diffView);
    stack->setCurrentIndex(0);

    connect(diffView, &DiffSplitView::closed, this, [this] {
        auto *tab = currentTab();
        if (tab && tab->inDiffMode) {
            tab->inDiffMode = false;
            tab->stack->setCurrentIndex(0);
            m_diffToggleBtn->setChecked(false);
        }
    });

    QFileInfo fi(filePath);
    int idx = m_tabWidget->addTab(stack, fi.fileName());
    m_tabWidget->setTabIcon(idx, FileIconProvider::iconForFile(fi.fileName()));
    m_tabWidget->setTabToolTip(idx, filePath);

    FileTab tab;
    tab.filePath = filePath;
    tab.editor = editor;
    tab.dirty = false;
    tab.inDiffMode = false;
    tab.stack = stack;
    tab.diffView = diffView;
    m_tabs[idx] = tab;

    connectEditorSignals(m_tabs[idx]);

    m_tabWidget->setCurrentIndex(idx);
    updateEmptyState();

#ifndef NO_QSCINTILLA
    setLexerForFile(filePath, editor);
    editor->setText(content);
    editor->setModified(false);
#else
    editor->setPlainText(content);
    editor->document()->setModified(false);
#endif

    watchFile(filePath);
}

void CodeViewer::closeFile(const QString &filePath)
{
    int idx = indexForFile(filePath);
    if (idx < 0) return;

    unwatchFile(filePath);
    m_tabs.remove(idx);
    m_tabWidget->removeTab(idx);

    QMap<int, FileTab> reindexed;
    int i = 0;
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it, ++i)
        reindexed[i] = it.value();
    m_tabs = reindexed;
    updateEmptyState();
}

void CodeViewer::openMarkdown(const QString &filePath)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->filePath == filePath) {
            m_tabWidget->setCurrentIndex(it.key());
            if (it->isMarkdown && it->markdownView) {
                QFile file(filePath);
                if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                    MarkdownRenderer renderer;
                    it->markdownView->setHtml(renderer.toHtml(QString::fromUtf8(file.readAll())));
                }
            }
            return;
        }
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(file.readAll());

    const auto &pal = ThemeManager::instance().palette();
    auto *browser = new QTextBrowser(this);
    browser->setOpenExternalLinks(true);
    browser->setFrameShape(QFrame::NoFrame);
    browser->setStyleSheet(
        QStringLiteral(
        "QTextBrowser {"
        "  background: %1; color: %2;"
        "  font-family: '.AppleSystemUIFont','Segoe UI','Helvetica Neue',sans-serif;"
        "  font-size: 13px; padding: 16px 24px;"
        "  selection-background-color: %3;"
        "}")
            .arg(pal.bg_window.name(), pal.text_primary.name(), pal.surface1.name()));
    browser->document()->setDefaultStyleSheet(
        QStringLiteral(
        "h1 { color: %1; font-size: 20px; margin: 18px 0 8px 0; }"
        "h2 { color: %2; font-size: 17px; margin: 16px 0 6px 0; }"
        "h3 { color: %3; font-size: 15px; margin: 14px 0 4px 0; }"
        "h4 { color: %4; font-size: 14px; margin: 12px 0 4px 0; }"
        "code { background: %5; color: %6; padding: 1px 4px;"
        "       border-radius: 3px; font-family: 'SF Mono','JetBrains Mono','Fira Code','Menlo','Consolas',monospace; font-size: 12px; }"
        "pre  { background: %7; border: 1px solid %8; border-radius: 6px;"
        "       padding: 10px 12px; font-family: 'SF Mono','JetBrains Mono','Fira Code','Menlo','Consolas',monospace; font-size: 12px;"
        "       color: %9; }"
        "a    { color: %2; }"
        "table { border-collapse: collapse; margin: 8px 0; }"
        "th   { background: %5; color: %1; padding: 4px 10px;"
        "       border: 1px solid %8; font-size: 12px; }"
        "td   { padding: 4px 10px; border: 1px solid %8; font-size: 12px; }"
        "strong { color: %10; }"
        "ul, ol { margin: 4px 0; }"
        )
            .arg(pal.mauve.name(),      // %1
                 pal.blue.name(),        // %2
                 pal.sky.name(),         // %3
                 pal.green.name(),       // %4
                 pal.bg_raised.name(),   // %5
                 pal.peach.name(),       // %6
                 pal.bg_base.name(),     // %7
                 pal.border_standard.name(), // %8
                 pal.text_primary.name()) // %9
            .arg(pal.pink.name())        // %10
    );

    MarkdownRenderer renderer;
    browser->setHtml(renderer.toHtml(content));

    auto *stack = new QStackedWidget(this);
    stack->addWidget(browser);

    QFileInfo fi(filePath);
    int idx = m_tabWidget->addTab(stack, fi.fileName());
    m_tabWidget->setTabIcon(idx, FileIconProvider::iconForFile(fi.fileName()));
    m_tabWidget->setTabToolTip(idx, filePath);

    FileTab tab;
    tab.filePath = filePath;
    tab.isMarkdown = true;
    tab.markdownView = browser;
    tab.stack = stack;
    m_tabs[idx] = tab;

    m_tabWidget->setCurrentIndex(idx);
    updateEmptyState();

    watchFile(filePath);
}

void CodeViewer::refreshFile(const QString &filePath)
{
    FileTab *tab = tabForFile(filePath);
    if (!tab) return;

    // Markdown tabs have no editor — refresh the rendered view instead.
    if (tab->isMarkdown) {
        if (tab->markdownView) {
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                MarkdownRenderer renderer;
                tab->markdownView->setHtml(renderer.toHtml(QString::fromUtf8(file.readAll())));
            }
        }
        return;
    }

    if (!tab->editor) return;

    if (tab->dirty) {
        QFileInfo fi(filePath);
        auto result = QMessageBox::question(
            this, "File Changed",
            QStringLiteral("%1 has been modified externally.\n\n"
                           "You have unsaved changes. Reload from disk?")
                .arg(fi.fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes)
            return;
        // Re-lookup: the modal dialog may have run the event loop and modified m_tabs.
        tab = tabForFile(filePath);
        if (!tab || !tab->editor) return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(file.readAll());

#ifndef NO_QSCINTILLA
    tab->editor->setText(content);
    tab->editor->setModified(false);
#else
    tab->editor->setPlainText(content);
    tab->editor->document()->setModified(false);
#endif

    tab->dirty = false;
    int idx = indexForFile(filePath);
    if (idx >= 0)
        updateTabTitle(idx);
}

void CodeViewer::forceReloadFile(const QString &filePath)
{
    FileTab *tab = tabForFile(filePath);
    if (!tab) return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // File was deleted (e.g., reverting a Write that created it)
        int idx = indexForFile(filePath);
        if (idx >= 0) {
            unwatchFile(filePath);
            m_tabs.remove(idx);
            m_tabWidget->removeTab(idx);
            QMap<int, FileTab> reindexed;
            int i = 0;
            for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it, ++i)
                reindexed[i] = it.value();
            m_tabs = reindexed;
            updateEmptyState();
        }
        return;
    }

    QString content = QString::fromUtf8(file.readAll());

#ifndef NO_QSCINTILLA
    tab->editor->setText(content);
    tab->editor->setModified(false);
#else
    tab->editor->setPlainText(content);
    tab->editor->document()->setModified(false);
#endif

    tab->dirty = false;
    int idx = indexForFile(filePath);
    if (idx >= 0)
        updateTabTitle(idx);
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

bool CodeViewer::saveCurrentFile()
{
    int idx = m_tabWidget->currentIndex();
    return saveFile(idx);
}

bool CodeViewer::saveFile(int tabIndex)
{
    if (!m_tabs.contains(tabIndex))
        return false;

    auto &tab = m_tabs[tabIndex];
    if (tab.filePath.isEmpty())
        return false;

    m_savingFiles.insert(tab.filePath);
    m_fileWatcher->removePath(tab.filePath);

    QFile file(tab.filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        m_savingFiles.remove(tab.filePath);
        m_fileWatcher->addPath(tab.filePath);
        return false;
    }

#ifndef NO_QSCINTILLA
    file.write(tab.editor->text().toUtf8());
#else
    file.write(tab.editor->toPlainText().toUtf8());
#endif
    file.close();

#ifndef NO_QSCINTILLA
    tab.editor->setModified(false);
#else
    tab.editor->document()->setModified(false);
#endif

    tab.dirty = false;
    updateTabTitle(tabIndex);

    m_fileWatcher->addPath(tab.filePath);
    m_savingFiles.remove(tab.filePath);

    emit fileSaved(tab.filePath);
    return true;
}

int CodeViewer::saveAllFiles()
{
    int saved = 0;
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->dirty && saveFile(it.key()))
            ++saved;
    }
    return saved;
}

// ---------------------------------------------------------------------------
// Dirty state queries
// ---------------------------------------------------------------------------

bool CodeViewer::isCurrentDirty() const
{
    int idx = m_tabWidget->currentIndex();
    if (m_tabs.contains(idx))
        return m_tabs[idx].dirty;
    return false;
}

bool CodeViewer::hasDirtyTabs() const
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->dirty)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Editor actions (forwarded to current editor)
// ---------------------------------------------------------------------------

void CodeViewer::undo()
{
    auto *tab = currentTab();
    if (tab && tab->editor)
        tab->editor->undo();
}

void CodeViewer::redo()
{
    auto *tab = currentTab();
    if (tab && tab->editor)
        tab->editor->redo();
}

void CodeViewer::cut()
{
    auto *tab = currentTab();
    if (tab && tab->editor)
        tab->editor->cut();
}

void CodeViewer::copy()
{
    auto *tab = currentTab();
    if (tab && tab->editor)
        tab->editor->copy();
}

void CodeViewer::paste()
{
    auto *tab = currentTab();
    if (tab && tab->editor)
        tab->editor->paste();
}

// ---------------------------------------------------------------------------
// Lexer assignment (QScintilla only)
// ---------------------------------------------------------------------------

#ifndef NO_QSCINTILLA
void CodeViewer::setLexerForFile(const QString &filePath, QsciScintilla *editor)
{
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();
    QString name = fi.fileName().toLower();
    QsciLexer *lexer = nullptr;

    // --- By filename first (for extensionless files) ---
    if (name == "makefile" || name == "gnumakefile") {
        lexer = new QsciLexerMakefile(editor);
    } else if (name == "cmakelists.txt") {
        lexer = new QsciLexerCMake(editor);
    } else if (name == "dockerfile" || name.startsWith("dockerfile.")) {
        lexer = new QsciLexerBash(editor);
    } else if (name == "gemfile" || name == "rakefile" || name == "vagrantfile") {
        lexer = new QsciLexerRuby(editor);
    } else if (name == ".bashrc" || name == ".bash_profile" || name == ".zshrc" ||
               name == ".profile" || name == ".zprofile") {
        lexer = new QsciLexerBash(editor);
    }

    // --- By extension ---
    if (!lexer) {
        if (ext == "cpp" || ext == "cxx" || ext == "cc" || ext == "c" ||
            ext == "h" || ext == "hpp" || ext == "hxx" || ext == "m" || ext == "mm" ||
            ext == "ino" || ext == "pde") {
            lexer = new QsciLexerCPP(editor);

        } else if (ext == "java") {
            lexer = new QsciLexerJava(editor);

        } else if (ext == "cs") {
            lexer = new QsciLexerCSharp(editor);

        } else if (ext == "js" || ext == "ts" || ext == "jsx" || ext == "tsx" ||
                   ext == "mjs" || ext == "cjs") {
        lexer = new QsciLexerJavaScript(editor);

        } else if (ext == "py" || ext == "pyw" || ext == "pyi" || ext == "pyx") {
            lexer = new QsciLexerPython(editor);

        } else if (ext == "rb" || ext == "rake" || ext == "gemspec") {
            lexer = new QsciLexerRuby(editor);

        } else if (ext == "rs" || ext == "go" || ext == "swift" || ext == "kt" ||
                   ext == "kts" || ext == "dart" || ext == "scala" || ext == "groovy" ||
                   ext == "gradle" || ext == "proto" || ext == "thrift") {
            // C-family syntax — CPP lexer is a reasonable approximation
            lexer = new QsciLexerCPP(editor);

        } else if (ext == "html" || ext == "htm" || ext == "vue" || ext == "svelte" ||
                   ext == "astro" || ext == "erb" || ext == "ejs" || ext == "hbs" ||
                   ext == "twig" || ext == "njk" || ext == "blade" ||
                   ext == "php" || ext == "phtml") {
            lexer = new QsciLexerHTML(editor);

        } else if (ext == "css" || ext == "scss" || ext == "sass" || ext == "less") {
            lexer = new QsciLexerCSS(editor);

        } else if (ext == "xml" || ext == "xsl" || ext == "xslt" || ext == "xsd" ||
                   ext == "svg" || ext == "plist" || ext == "csproj" || ext == "fsproj" ||
                   ext == "vcxproj" || ext == "sln" || ext == "xaml" || ext == "wsdl" ||
                   ext == "rss" || ext == "atom" || ext == "ui") {
            lexer = new QsciLexerXML(editor);

        } else if (ext == "json" || ext == "jsonc" || ext == "geojson" ||
                   ext == "jsonl" || ext == "json5") {
            lexer = new QsciLexerJSON(editor);

        } else if (ext == "yml" || ext == "yaml") {
            lexer = new QsciLexerYAML(editor);

        } else if (ext == "sql" || ext == "ddl" || ext == "dml" || ext == "pgsql" ||
                   ext == "plsql" || ext == "mysql") {
            lexer = new QsciLexerSQL(editor);

        } else if (ext == "sh" || ext == "bash" || ext == "zsh" || ext == "fish" ||
                   ext == "ksh" || ext == "csh" || ext == "tcsh") {
            lexer = new QsciLexerBash(editor);

        } else if (ext == "lua") {
            lexer = new QsciLexerLua(editor);

        } else if (ext == "pl" || ext == "pm" || ext == "pod" || ext == "t") {
            lexer = new QsciLexerPerl(editor);

        } else if (ext == "mk") {
            lexer = new QsciLexerMakefile(editor);

        } else if (ext == "cmake") {
            lexer = new QsciLexerCMake(editor);

        } else if (ext == "md" || ext == "markdown" || ext == "mdx" || ext == "rst") {
            lexer = new QsciLexerMarkdown(editor);

        } else if (ext == "diff" || ext == "patch") {
            lexer = new QsciLexerDiff(editor);

        } else if (ext == "bat" || ext == "cmd") {
            lexer = new QsciLexerBatch(editor);

        } else if (ext == "ini" || ext == "cfg" || ext == "conf" || ext == "properties" ||
                   ext == "env" || ext == "toml" || ext == "editorconfig" || ext == "gitconfig") {
            lexer = new QsciLexerProperties(editor);

        } else if (ext == "tex" || ext == "latex" || ext == "sty" || ext == "cls" || ext == "bib") {
            lexer = new QsciLexerTeX(editor);

        } else if (ext == "d") {
            lexer = new QsciLexerD(editor);

        } else if (ext == "pas" || ext == "pp" || ext == "dpr" || ext == "lpr") {
            lexer = new QsciLexerPascal(editor);

        } else if (ext == "f" || ext == "for" || ext == "f90" || ext == "f95" || ext == "f03") {
            lexer = new QsciLexerFortran(editor);

        } else if (ext == "tcl" || ext == "tk") {
            lexer = new QsciLexerTCL(editor);

        } else if (ext == "coffee" || ext == "litcoffee") {
            lexer = new QsciLexerCoffeeScript(editor);

        } else if (ext == "r" || ext == "rmd") {
            // No dedicated R lexer in QScintilla; Python is a reasonable fallback
            lexer = new QsciLexerPython(editor);
        }
    }

    if (lexer) {
        applyThemeToLexer(lexer);
        editor->setLexer(lexer);
    } else {
        const auto &pal = ThemeManager::instance().palette();
        editor->setLexer(nullptr);
        editor->setPaper(pal.bg_window);
        editor->setColor(pal.text_primary);
    }
}
#endif // NO_QSCINTILLA

// ---------------------------------------------------------------------------
// Tab / file helpers
// ---------------------------------------------------------------------------

QString CodeViewer::currentFile() const
{
    int idx = m_tabWidget->currentIndex();
    if (m_tabs.contains(idx))
        return m_tabs[idx].filePath;
    return {};
}

FileTab *CodeViewer::currentTab()
{
    int idx = m_tabWidget->currentIndex();
    if (m_tabs.contains(idx))
        return &m_tabs[idx];
    return nullptr;
}

FileTab *CodeViewer::tabForFile(const QString &filePath)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->filePath == filePath)
            return &it.value();
    }
    return nullptr;
}

int CodeViewer::indexForFile(const QString &filePath)
{
    for (auto it = m_tabs.begin(); it != m_tabs.end(); ++it) {
        if (it->filePath == filePath)
            return it.key();
    }
    return -1;
}

void CodeViewer::updateTabTitle(int index)
{
    if (!m_tabs.contains(index)) return;
    QFileInfo fi(m_tabs[index].filePath);
    QString title = fi.fileName();
    if (m_tabs[index].dirty)
        title.prepend(QStringLiteral("\u2022 ")); // bullet dot prefix for dirty
    m_tabWidget->setTabText(index, title);
}

bool CodeViewer::confirmCloseTab(int index)
{
    if (!m_tabs.contains(index) || !m_tabs[index].dirty)
        return true;

    QFileInfo fi(m_tabs[index].filePath);
    auto result = QMessageBox::warning(
        this, "Unsaved Changes",
        QStringLiteral("Save changes to %1?").arg(fi.fileName()),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);

    if (result == QMessageBox::Cancel)
        return false;
    if (result == QMessageBox::Save)
        return saveFile(index);
    return true; // Discard
}

// ---------------------------------------------------------------------------
// File system watcher
// ---------------------------------------------------------------------------

void CodeViewer::watchFile(const QString &filePath)
{
    if (!filePath.isEmpty() && QFile::exists(filePath))
        m_fileWatcher->addPath(filePath);
}

void CodeViewer::unwatchFile(const QString &filePath)
{
    if (!filePath.isEmpty())
        m_fileWatcher->removePath(filePath);
}

void CodeViewer::onExternalFileChanged(const QString &filePath)
{
    if (m_savingFiles.contains(filePath))
        return;

    FileTab *tab = tabForFile(filePath);
    if (!tab) return;

    // Re-add to watcher (some systems drop the watch after a change)
    if (QFile::exists(filePath))
        m_fileWatcher->addPath(filePath);

    if (tab->isMarkdown && tab->markdownView) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            MarkdownRenderer renderer;
            tab->markdownView->setHtml(renderer.toHtml(QString::fromUtf8(file.readAll())));
        }
        return;
    }

    if (tab->dirty) {
        QFileInfo fi(filePath);
        auto result = QMessageBox::question(
            this, "File Changed",
            QStringLiteral("%1 has been modified externally.\n\n"
                           "You have unsaved changes. Reload from disk?")
                .arg(fi.fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (result != QMessageBox::Yes)
            return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString content = QString::fromUtf8(file.readAll());

#ifndef NO_QSCINTILLA
    tab->editor->setText(content);
    tab->editor->setModified(false);
#else
    tab->editor->setPlainText(content);
    tab->editor->document()->setModified(false);
#endif

    tab->dirty = false;
    int idx = indexForFile(filePath);
    if (idx >= 0)
        updateTabTitle(idx);
}

// ---------------------------------------------------------------------------
// Diff markers
// ---------------------------------------------------------------------------

void CodeViewer::showDiff(const FileDiff &diff)
{
    clearDiffMarkers();
    applyDiffMarkers(diff);
}

void CodeViewer::clearDiffMarkers()
{
#ifndef NO_QSCINTILLA
    auto *tab = currentTab();
    if (tab && tab->editor) {
        tab->editor->markerDeleteAll(1);
        tab->editor->markerDeleteAll(2);
    }
#endif
}

void CodeViewer::applyDiffMarkers(const FileDiff &diff)
{
#ifndef NO_QSCINTILLA
    FileTab *tab = tabForFile(diff.filePath);
    if (!tab && !diff.filePath.isEmpty())
        tab = currentTab();
    if (!tab || !tab->editor) return;

    for (const auto &hunk : diff.hunks) {
        int marker = (hunk.type == DiffHunk::Added) ? 1 : 2;
        for (int i = 0; i < hunk.count; ++i) {
            int line = hunk.startLine + i;
            if (line >= 0 && line < tab->editor->lines())
                tab->editor->markerAdd(line, marker);
        }
    }
    if (!diff.hunks.isEmpty())
        tab->editor->setCursorPosition(diff.hunks.first().startLine, 0);
#else
    Q_UNUSED(diff);
#endif
}

void CodeViewer::scrollToLine(int line)
{
#ifndef NO_QSCINTILLA
    auto *tab = currentTab();
    if (tab && tab->editor) {
        tab->editor->setCursorPosition(line, 0);
        tab->editor->ensureLineVisible(line);
    }
#else
    Q_UNUSED(line);
#endif
}

// ---------------------------------------------------------------------------
// Diff mode toggle
// ---------------------------------------------------------------------------

void CodeViewer::toggleDiffMode()
{
    auto *tab = currentTab();
    if (!tab || !tab->stack) return;

    tab->inDiffMode = !tab->inDiffMode;
    m_diffToggleBtn->setChecked(tab->inDiffMode);

    if (tab->inDiffMode) {
        if (m_gitManager && m_gitManager->isGitRepo()) {
            m_gitManager->requestFileDiff(
                QDir(m_gitManager->workingDirectory()).relativeFilePath(tab->filePath), false);
        }
        tab->stack->setCurrentIndex(1);
    } else {
        tab->stack->setCurrentIndex(0);
    }
}

bool CodeViewer::isInDiffMode() const
{
    int idx = m_tabWidget->currentIndex();
    if (m_tabs.contains(idx))
        return m_tabs[idx].inDiffMode;
    return false;
}

void CodeViewer::showSplitDiff(const QString &filePath, const QString &oldContent,
                                const QString &newContent, const QString &leftLabel,
                                const QString &rightLabel)
{
    // Find or load the file tab
    FileTab *tab = tabForFile(filePath);
    if (!tab) {
        loadFile(filePath);
        tab = tabForFile(filePath);
    }
    if (!tab || !tab->diffView) return;

    tab->diffView->showDiff(
        QDir(m_gitManager ? m_gitManager->workingDirectory() : "").relativeFilePath(filePath),
        oldContent, newContent, leftLabel, rightLabel);
    tab->inDiffMode = true;
    tab->stack->setCurrentIndex(1);
    m_diffToggleBtn->setChecked(true);

    int idx = indexForFile(filePath);
    if (idx >= 0)
        m_tabWidget->setCurrentIndex(idx);
}

// ---------------------------------------------------------------------------
// Open files list (for context)
// ---------------------------------------------------------------------------

QStringList CodeViewer::openFiles() const
{
    QStringList files;
    for (auto it = m_tabs.constBegin(); it != m_tabs.constEnd(); ++it) {
        if (!it->filePath.isEmpty())
            files << it->filePath;
    }
    return files;
}

QString CodeViewer::selectedText() const
{
#ifndef NO_QSCINTILLA
    auto *tab = const_cast<CodeViewer *>(this)->currentTab();
    if (tab && tab->editor)
        return tab->editor->selectedText();
#endif
    return {};
}

int CodeViewer::currentLine() const
{
#ifndef NO_QSCINTILLA
    auto *tab = const_cast<CodeViewer *>(this)->currentTab();
    if (tab && tab->editor) {
        int line, col;
        tab->editor->getCursorPosition(&line, &col);
        return line + 1;
    }
#endif
    return 0;
}

// ---------------------------------------------------------------------------
// Inline diff overlay (for reviewing AI edits)
// ---------------------------------------------------------------------------

void CodeViewer::showInlineDiffOverlay(const QString &filePath, const QString &oldText,
                                        const QString &newText, int startLine)
{
    if (!m_inlineDiffOverlay) {
        m_inlineDiffOverlay = new InlineDiffOverlay(this);

        connect(m_inlineDiffOverlay, &InlineDiffOverlay::acceptAll, this, [this] {
            emit inlineDiffAccepted(m_inlineDiffOverlay->property("filePath").toString());
            hideInlineDiffOverlay();
        });
        connect(m_inlineDiffOverlay, &InlineDiffOverlay::rejectAll, this, [this] {
            auto *overlay = m_inlineDiffOverlay;
            QString fp = overlay->property("filePath").toString();
            emit inlineDiffRejected(fp, "", "");
            hideInlineDiffOverlay();
        });
        connect(m_inlineDiffOverlay, &InlineDiffOverlay::closed, this, [this] {
            hideInlineDiffOverlay();
        });
        connect(m_inlineDiffOverlay, &InlineDiffOverlay::acceptHunk, this, [this](int) {
            // Individual hunk accept - no-op since edit is already applied
        });
        connect(m_inlineDiffOverlay, &InlineDiffOverlay::rejectHunk, this, [this](int) {
            // Individual hunk reject would need per-hunk revert
            QString fp = m_inlineDiffOverlay->property("filePath").toString();
            emit inlineDiffRejected(fp, "", "");
        });
    }

    // Load the file if not already open
    if (tabForFile(filePath) == nullptr)
        loadFile(filePath);

    m_inlineDiffOverlay->setProperty("filePath", filePath);
    m_inlineDiffOverlay->setFilePath(filePath);
    m_inlineDiffOverlay->setDiff(oldText, newText, startLine);

    // Parent to the tab widget (not the stack) so it spans the full width
    m_inlineDiffOverlay->setParent(m_tabWidget);
    m_inlineDiffOverlay->setMaximumHeight(200);
    m_inlineDiffOverlay->show();
    m_inlineDiffOverlay->raise();

    // Position at the top of the tab content area, deferring so the layout has settled
    auto repositionOverlay = [this] {
        if (!m_inlineDiffOverlay || !m_inlineDiffOverlay->isVisible()) return;
        int w = m_tabWidget->width();
        int tabBarH = m_tabWidget->tabBar()->height();
        int barH = qMin(m_inlineDiffOverlay->sizeHint().height(), 200);
        m_inlineDiffOverlay->setGeometry(0, tabBarH, w, barH);
        m_inlineDiffOverlay->raise();
    };
    repositionOverlay();
    QTimer::singleShot(50, this, repositionOverlay);
    QTimer::singleShot(200, this, repositionOverlay);
}

void CodeViewer::hideInlineDiffOverlay()
{
    if (m_inlineDiffOverlay) {
        m_inlineDiffOverlay->clear();
        m_inlineDiffOverlay->hide();
    }
}

// ---------------------------------------------------------------------------
// Inline edit bar (Cmd+K)
// ---------------------------------------------------------------------------

void CodeViewer::showInlineEditBar()
{
    auto *tab = currentTab();
    if (!tab || !tab->editor) return;

    QString selected = selectedText();
    if (selected.isEmpty()) return;

    if (!m_inlineEditBar) {
        m_inlineEditBar = new InlineEditBar(this);
        connect(m_inlineEditBar, &InlineEditBar::submitted, this,
                &CodeViewer::inlineEditSubmitted);
        connect(m_inlineEditBar, &InlineEditBar::cancelled, this, [this] {
            hideInlineEditBar();
        });
    }

    m_inlineEditBar->setContext(tab->filePath, selected, currentLine());

    // Position below the editor toolbar
    if (tab->stack) {
        m_inlineEditBar->setParent(tab->stack);
        int w = tab->stack->width() - 16;
        m_inlineEditBar->setGeometry(8, 8, w, m_inlineEditBar->sizeHint().height());
    }

    m_inlineEditBar->show();
    m_inlineEditBar->raise();
    m_inlineEditBar->focusInput();
}

void CodeViewer::hideInlineEditBar()
{
    if (m_inlineEditBar) {
        m_inlineEditBar->clear();
        m_inlineEditBar->hide();
    }
}
