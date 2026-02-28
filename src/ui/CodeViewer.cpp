#include "ui/CodeViewer.h"
#include "ui/DiffSplitView.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>

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

    // Tab widget with diff toggle button in corner
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setTabsClosable(true);
    m_tabWidget->setMovable(true);
    m_tabWidget->setDocumentMode(true);

    m_diffToggleBtn = new QPushButton("Diff", this);
    m_diffToggleBtn->setCheckable(true);
    m_diffToggleBtn->setFixedSize(40, 20);
    m_diffToggleBtn->setToolTip("Toggle side-by-side diff view");
    m_diffToggleBtn->setStyleSheet(
        "QPushButton { background: #252525; color: #6c7086; border: none; border-radius: 4px; "
        "font-size: 11px; margin: 2px 4px; }"
        "QPushButton:hover { color: #cdd6f4; background: #333; }"
        "QPushButton:checked { background: #2d5a27; color: #a6e3a1; }");
    m_tabWidget->setCornerWidget(m_diffToggleBtn, Qt::TopRightCorner);
    connect(m_diffToggleBtn, &QPushButton::clicked, this, &CodeViewer::toggleDiffMode);

    layout->addWidget(m_tabWidget);

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
    });

    connect(m_tabWidget, &QTabWidget::currentChanged, this, [this](int idx) {
        if (m_tabs.contains(idx))
            m_diffToggleBtn->setChecked(m_tabs[idx].inDiffMode);
    });
}

// ---------------------------------------------------------------------------
// Editor factory
// ---------------------------------------------------------------------------

#ifndef NO_QSCINTILLA

static void applyDarkThemeToLexer(QsciLexer *lexer)
{
    if (!lexer) return;

    QFont monoFont("Menlo", 13);
    lexer->setFont(monoFont);
    lexer->setDefaultFont(monoFont);
    lexer->setPaper(QColor("#1a1a1a"));
    lexer->setDefaultPaper(QColor("#1a1a1a"));
    lexer->setColor(QColor("#cdd6f4"));
    lexer->setDefaultColor(QColor("#cdd6f4"));

    for (int i = 0; i <= 255; ++i) {
        lexer->setPaper(QColor("#1a1a1a"), i);
        lexer->setColor(QColor("#cdd6f4"), i);
        lexer->setFont(monoFont, i);
    }

    // --- C / C++ / Objective-C (also covers Java, C# which inherit QsciLexerCPP) ---
    if (auto *L = qobject_cast<QsciLexerCPP *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerCPP::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerCPP::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerCPP::DoubleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerCPP::Comment);
        L->setColor(QColor("#6c7086"), QsciLexerCPP::CommentLine);
        L->setColor(QColor("#6c7086"), QsciLexerCPP::CommentDoc);
        L->setColor(QColor("#fab387"), QsciLexerCPP::Number);
        L->setColor(QColor("#89b4fa"), QsciLexerCPP::PreProcessor);
        L->setColor(QColor("#f38ba8"), QsciLexerCPP::Operator);
        L->setColor(QColor("#cdd6f4"), QsciLexerCPP::Identifier);
        L->setColor(QColor("#cba6f7"), QsciLexerCPP::KeywordSet2);
        L->setColor(QColor("#a6e3a1"), QsciLexerCPP::RawString);
        return;
    }

    // --- Python ---
    if (auto *L = qobject_cast<QsciLexerPython *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerPython::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerPython::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerPython::DoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerPython::TripleSingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerPython::TripleDoubleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerPython::Comment);
        L->setColor(QColor("#fab387"), QsciLexerPython::Number);
        L->setColor(QColor("#89b4fa"), QsciLexerPython::Decorator);
        L->setColor(QColor("#f9e2af"), QsciLexerPython::FunctionMethodName);
        L->setColor(QColor("#f38ba8"), QsciLexerPython::Operator);
        return;
    }

    // --- JavaScript / TypeScript ---
    if (auto *L = qobject_cast<QsciLexerJavaScript *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerJavaScript::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerJavaScript::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerJavaScript::DoubleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerJavaScript::Comment);
        L->setColor(QColor("#6c7086"), QsciLexerJavaScript::CommentLine);
        L->setColor(QColor("#6c7086"), QsciLexerJavaScript::CommentDoc);
        L->setColor(QColor("#fab387"), QsciLexerJavaScript::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerJavaScript::Operator);
        L->setColor(QColor("#cdd6f4"), QsciLexerJavaScript::Identifier);
        return;
    }

    // --- Ruby ---
    if (auto *L = qobject_cast<QsciLexerRuby *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerRuby::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerRuby::DoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerRuby::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerRuby::HereDocument);
        L->setColor(QColor("#6c7086"), QsciLexerRuby::Comment);
        L->setColor(QColor("#fab387"), QsciLexerRuby::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerRuby::Operator);
        L->setColor(QColor("#f9e2af"), QsciLexerRuby::FunctionMethodName);
        L->setColor(QColor("#89b4fa"), QsciLexerRuby::ClassName);
        L->setColor(QColor("#f38ba8"), QsciLexerRuby::Symbol);
        L->setColor(QColor("#fab387"), QsciLexerRuby::Regex);
        return;
    }

    // --- HTML (also handles embedded JS/CSS/PHP) ---
    if (auto *L = qobject_cast<QsciLexerHTML *>(lexer)) {
        L->setColor(QColor("#f38ba8"), QsciLexerHTML::Tag);
        L->setColor(QColor("#f38ba8"), QsciLexerHTML::UnknownTag);
        L->setColor(QColor("#fab387"), QsciLexerHTML::Attribute);
        L->setColor(QColor("#fab387"), QsciLexerHTML::UnknownAttribute);
        L->setColor(QColor("#a6e3a1"), QsciLexerHTML::HTMLDoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerHTML::HTMLSingleQuotedString);
        L->setColor(QColor("#fab387"), QsciLexerHTML::HTMLNumber);
        L->setColor(QColor("#6c7086"), QsciLexerHTML::HTMLComment);
        L->setColor(QColor("#89dceb"), QsciLexerHTML::Entity);
        L->setColor(QColor("#cdd6f4"), QsciLexerHTML::OtherInTag);
        return;
    }

    // --- CSS ---
    if (auto *L = qobject_cast<QsciLexerCSS *>(lexer)) {
        L->setColor(QColor("#f38ba8"), QsciLexerCSS::Tag);
        L->setColor(QColor("#89b4fa"), QsciLexerCSS::ClassSelector);
        L->setColor(QColor("#fab387"), QsciLexerCSS::IDSelector);
        L->setColor(QColor("#cba6f7"), QsciLexerCSS::PseudoClass);
        L->setColor(QColor("#89dceb"), QsciLexerCSS::CSS1Property);
        L->setColor(QColor("#89dceb"), QsciLexerCSS::CSS2Property);
        L->setColor(QColor("#89dceb"), QsciLexerCSS::CSS3Property);
        L->setColor(QColor("#a6e3a1"), QsciLexerCSS::DoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerCSS::SingleQuotedString);
        L->setColor(QColor("#fab387"), QsciLexerCSS::Value);
        L->setColor(QColor("#6c7086"), QsciLexerCSS::Comment);
        L->setColor(QColor("#f38ba8"), QsciLexerCSS::Important);
        L->setColor(QColor("#f38ba8"), QsciLexerCSS::AtRule);
        return;
    }

    // --- XML ---
    if (auto *L = qobject_cast<QsciLexerXML *>(lexer)) {
        L->setColor(QColor("#f38ba8"), QsciLexerHTML::Tag);
        L->setColor(QColor("#fab387"), QsciLexerHTML::Attribute);
        L->setColor(QColor("#a6e3a1"), QsciLexerHTML::HTMLDoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerHTML::HTMLSingleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerHTML::HTMLComment);
        L->setColor(QColor("#89dceb"), QsciLexerHTML::Entity);
        return;
    }

    // --- YAML ---
    if (auto *L = qobject_cast<QsciLexerYAML *>(lexer)) {
        L->setColor(QColor("#89b4fa"), QsciLexerYAML::Identifier);
        L->setColor(QColor("#cba6f7"), QsciLexerYAML::Keyword);
        L->setColor(QColor("#fab387"), QsciLexerYAML::Number);
        L->setColor(QColor("#6c7086"), QsciLexerYAML::Comment);
        L->setColor(QColor("#f38ba8"), QsciLexerYAML::Reference);
        L->setColor(QColor("#f38ba8"), QsciLexerYAML::Operator);
        L->setColor(QColor("#89dceb"), QsciLexerYAML::DocumentDelimiter);
        L->setColor(QColor("#f9e2af"), QsciLexerYAML::TextBlockMarker);
        return;
    }

    // --- SQL ---
    if (auto *L = qobject_cast<QsciLexerSQL *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerSQL::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerSQL::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerSQL::DoubleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerSQL::Comment);
        L->setColor(QColor("#6c7086"), QsciLexerSQL::CommentLine);
        L->setColor(QColor("#6c7086"), QsciLexerSQL::CommentDoc);
        L->setColor(QColor("#fab387"), QsciLexerSQL::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerSQL::Operator);
        L->setColor(QColor("#89b4fa"), QsciLexerSQL::KeywordSet5);
        L->setColor(QColor("#89b4fa"), QsciLexerSQL::KeywordSet6);
        return;
    }

    // --- JSON ---
    if (auto *L = qobject_cast<QsciLexerJSON *>(lexer)) {
        L->setColor(QColor("#89b4fa"), QsciLexerJSON::Property);
        L->setColor(QColor("#a6e3a1"), QsciLexerJSON::String);
        L->setColor(QColor("#fab387"), QsciLexerJSON::Number);
        L->setColor(QColor("#cba6f7"), QsciLexerJSON::Keyword);
        L->setColor(QColor("#f38ba8"), QsciLexerJSON::Operator);
        L->setColor(QColor("#89dceb"), QsciLexerJSON::EscapeSequence);
        L->setColor(QColor("#6c7086"), QsciLexerJSON::CommentLine);
        L->setColor(QColor("#6c7086"), QsciLexerJSON::CommentBlock);
        L->setColor(QColor("#f38ba8"), QsciLexerJSON::Error);
        return;
    }

    // --- Markdown ---
    if (auto *L = qobject_cast<QsciLexerMarkdown *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerMarkdown::Header1);
        L->setColor(QColor("#89b4fa"), QsciLexerMarkdown::Header2);
        L->setColor(QColor("#89dceb"), QsciLexerMarkdown::Header3);
        L->setColor(QColor("#a6e3a1"), QsciLexerMarkdown::Header4);
        L->setColor(QColor("#f9e2af"), QsciLexerMarkdown::Header5);
        L->setColor(QColor("#fab387"), QsciLexerMarkdown::Header6);
        // Strong/StrongEmphasis not available in all QScintilla versions
        L->setColor(QColor("#89b4fa"), QsciLexerMarkdown::Link);
        L->setColor(QColor("#a6e3a1"), QsciLexerMarkdown::CodeBackticks);
        L->setColor(QColor("#a6e3a1"), QsciLexerMarkdown::CodeDoubleBackticks);
        L->setColor(QColor("#a6e3a1"), QsciLexerMarkdown::CodeBlock);
        L->setColor(QColor("#6c7086"), QsciLexerMarkdown::BlockQuote);
        L->setColor(QColor("#f38ba8"), QsciLexerMarkdown::HorizontalRule);
        L->setColor(QColor("#fab387"), QsciLexerMarkdown::UnorderedListItem);
        L->setColor(QColor("#fab387"), QsciLexerMarkdown::OrderedListItem);
        L->setColor(QColor("#f38ba8"), QsciLexerMarkdown::StrikeOut);
        return;
    }

    // --- Lua ---
    if (auto *L = qobject_cast<QsciLexerLua *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerLua::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerLua::String);
        L->setColor(QColor("#a6e3a1"), QsciLexerLua::LiteralString);
        L->setColor(QColor("#6c7086"), QsciLexerLua::Comment);
        L->setColor(QColor("#6c7086"), QsciLexerLua::LineComment);
        L->setColor(QColor("#fab387"), QsciLexerLua::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerLua::Operator);
        L->setColor(QColor("#89b4fa"), QsciLexerLua::BasicFunctions);
        return;
    }

    // --- Perl ---
    if (auto *L = qobject_cast<QsciLexerPerl *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerPerl::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerPerl::DoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerPerl::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerPerl::HereDocumentDelimiter);
        L->setColor(QColor("#6c7086"), QsciLexerPerl::Comment);
        L->setColor(QColor("#fab387"), QsciLexerPerl::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerPerl::Operator);
        L->setColor(QColor("#fab387"), QsciLexerPerl::Regex);
        L->setColor(QColor("#89b4fa"), QsciLexerPerl::Array);
        L->setColor(QColor("#89dceb"), QsciLexerPerl::Hash);
        return;
    }

    // --- Bash / Shell ---
    if (auto *L = qobject_cast<QsciLexerBash *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerBash::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerBash::DoubleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerBash::SingleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerBash::Comment);
        L->setColor(QColor("#fab387"), QsciLexerBash::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerBash::Operator);
        L->setColor(QColor("#89b4fa"), QsciLexerBash::Backticks);
        L->setColor(QColor("#89dceb"), QsciLexerBash::Scalar);
        L->setColor(QColor("#f9e2af"), QsciLexerBash::ParameterExpansion);
        return;
    }

    // --- Makefile ---
    if (auto *L = qobject_cast<QsciLexerMakefile *>(lexer)) {
        L->setColor(QColor("#89b4fa"), QsciLexerMakefile::Target);
        L->setColor(QColor("#a6e3a1"), QsciLexerMakefile::Variable);
        L->setColor(QColor("#6c7086"), QsciLexerMakefile::Comment);
        L->setColor(QColor("#89dceb"), QsciLexerMakefile::Preprocessor);
        L->setColor(QColor("#f38ba8"), QsciLexerMakefile::Operator);
        return;
    }

    // --- CMake ---
    if (auto *L = qobject_cast<QsciLexerCMake *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerCMake::KeywordSet3);
        L->setColor(QColor("#a6e3a1"), QsciLexerCMake::String);
        L->setColor(QColor("#a6e3a1"), QsciLexerCMake::StringLeftQuote);
        L->setColor(QColor("#a6e3a1"), QsciLexerCMake::StringRightQuote);
        L->setColor(QColor("#6c7086"), QsciLexerCMake::Comment);
        L->setColor(QColor("#89b4fa"), QsciLexerCMake::Function);
        L->setColor(QColor("#89dceb"), QsciLexerCMake::Variable);
        L->setColor(QColor("#fab387"), QsciLexerCMake::Number);
        return;
    }

    // --- Diff / Patch ---
    if (auto *L = qobject_cast<QsciLexerDiff *>(lexer)) {
        L->setColor(QColor("#a6e3a1"), QsciLexerDiff::LineAdded);
        L->setColor(QColor("#f38ba8"), QsciLexerDiff::LineRemoved);
        L->setColor(QColor("#89b4fa"), QsciLexerDiff::Header);
        L->setColor(QColor("#cba6f7"), QsciLexerDiff::Position);
        L->setColor(QColor("#6c7086"), QsciLexerDiff::Comment);
        return;
    }

    // --- Properties / INI / .env ---
    if (auto *L = qobject_cast<QsciLexerProperties *>(lexer)) {
        L->setColor(QColor("#89b4fa"), QsciLexerProperties::Section);
        L->setColor(QColor("#f9e2af"), QsciLexerProperties::Assignment);
        L->setColor(QColor("#a6e3a1"), QsciLexerProperties::DefaultValue);
        L->setColor(QColor("#6c7086"), QsciLexerProperties::Comment);
        return;
    }

    // --- TeX / LaTeX ---
    if (auto *L = qobject_cast<QsciLexerTeX *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerTeX::Command);
        L->setColor(QColor("#89b4fa"), QsciLexerTeX::Group);
        L->setColor(QColor("#f38ba8"), QsciLexerTeX::Special);
        L->setColor(QColor("#a6e3a1"), QsciLexerTeX::Text);
        return;
    }

    // --- Batch (Windows CMD) ---
    if (auto *L = qobject_cast<QsciLexerBatch *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerBatch::Keyword);
        L->setColor(QColor("#6c7086"), QsciLexerBatch::Comment);
        L->setColor(QColor("#89dceb"), QsciLexerBatch::Variable);
        L->setColor(QColor("#f38ba8"), QsciLexerBatch::Operator);
        return;
    }

    // --- CoffeeScript ---
    if (auto *L = qobject_cast<QsciLexerCoffeeScript *>(lexer)) {
        L->setColor(QColor("#cba6f7"), QsciLexerCoffeeScript::Keyword);
        L->setColor(QColor("#a6e3a1"), QsciLexerCoffeeScript::SingleQuotedString);
        L->setColor(QColor("#a6e3a1"), QsciLexerCoffeeScript::DoubleQuotedString);
        L->setColor(QColor("#6c7086"), QsciLexerCoffeeScript::Comment);
        L->setColor(QColor("#6c7086"), QsciLexerCoffeeScript::CommentLine);
        L->setColor(QColor("#fab387"), QsciLexerCoffeeScript::Number);
        L->setColor(QColor("#f38ba8"), QsciLexerCoffeeScript::Operator);
        return;
    }
}

QsciScintilla *CodeViewer::createEditor()
{
    auto *ed = new QsciScintilla(this);
    ed->setReadOnly(false);
    ed->setMarginType(0, QsciScintilla::NumberMargin);
    ed->setMarginWidth(0, "00000");
    ed->setMarginsForegroundColor(QColor("#4a4a4a"));
    ed->setMarginsBackgroundColor(QColor("#0e0e0e"));
    ed->setMarginsFont(QFont("Menlo", 12));
    ed->setCaretForegroundColor(QColor("#cdd6f4"));
    ed->setCaretLineVisible(true);
    ed->setCaretLineBackgroundColor(QColor("#1e1e1e"));
    ed->setPaper(QColor("#1a1a1a"));
    ed->setColor(QColor("#cdd6f4"));
    ed->setFont(QFont("Menlo", 13));
    ed->setTabWidth(4);
    ed->setIndentationsUseTabs(false);
    ed->setAutoIndent(true);
    ed->setIndentationGuides(true);
    ed->setIndentationGuidesForegroundColor(QColor("#2a2a2a"));
    ed->setFolding(QsciScintilla::BoxedTreeFoldStyle, 2);
    ed->setFoldMarginColors(QColor("#0e0e0e"), QColor("#0e0e0e"));
    ed->setBraceMatching(QsciScintilla::SloppyBraceMatch);
    ed->setMatchedBraceForegroundColor(QColor("#f9e2af"));
    ed->setMatchedBraceBackgroundColor(QColor("#3a3a3a"));
    ed->setSelectionBackgroundColor(QColor("#3a3a3a"));
    ed->setSelectionForegroundColor(QColor("#cdd6f4"));
    ed->setEolMode(QsciScintilla::EolUnix);
    ed->setUtf8(true);

    // Diff markers (green = added, red = removed) — unified palette
    ed->markerDefine(QsciScintilla::Background, 1);
    ed->setMarkerBackgroundColor(QColor(0x1a, 0x2e, 0x1a), 1);  // #1a2e1a
    ed->markerDefine(QsciScintilla::Background, 2);
    ed->setMarkerBackgroundColor(QColor(0x2e, 0x1a, 0x1e), 2);  // #2e1a1e

    return ed;
}

#else // NO_QSCINTILLA fallback

QPlainTextEdit *CodeViewer::createEditor()
{
    auto *ed = new QPlainTextEdit(this);
    ed->setReadOnly(false);
    ed->setTabStopDistance(32);
    ed->setStyleSheet(
        "QPlainTextEdit { background: #1a1a1a; color: #cdd6f4; border: none; "
        "font-family: Menlo, monospace; font-size: 13px; }");
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

void CodeViewer::refreshFile(const QString &filePath)
{
    FileTab *tab = tabForFile(filePath);
    if (!tab) return;

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
        applyDarkThemeToLexer(lexer);
        editor->setLexer(lexer);
    } else {
        editor->setLexer(nullptr);
        editor->setPaper(QColor("#1a1a1a"));
        editor->setColor(QColor("#cdd6f4"));
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
