#pragma once

#include "ThemePalette.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QObject>
#include <QRegularExpression>

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager &instance()
    {
        static ThemeManager mgr;
        return mgr;
    }

    void initialize()
    {
        if (!m_palettes.isEmpty())
            return;

        // --- Mocha ---
        ThemePalette mocha;
        mocha.name = "mocha";
        mocha.isLight = false;
        mocha.base      = QColor("#1e1e2e"); mocha.mantle    = QColor("#181825"); mocha.crust     = QColor("#11111b");
        mocha.surface0  = QColor("#313244"); mocha.surface1  = QColor("#45475a"); mocha.surface2  = QColor("#585b70");
        mocha.overlay0  = QColor("#6c7086"); mocha.overlay1  = QColor("#7f849c"); mocha.overlay2  = QColor("#9399b2");
        mocha.text      = QColor("#cdd6f4"); mocha.subtext0  = QColor("#a6adc8"); mocha.subtext1  = QColor("#bac2de");
        mocha.mauve     = QColor("#cba6f7"); mocha.blue      = QColor("#89b4fa"); mocha.green     = QColor("#a6e3a1");
        mocha.red       = QColor("#f38ba8"); mocha.yellow    = QColor("#f9e2af"); mocha.peach     = QColor("#fab387");
        mocha.sky       = QColor("#89dceb"); mocha.lavender  = QColor("#b4befe"); mocha.teal      = QColor("#94e2d5");
        mocha.sapphire  = QColor("#74c7ec"); mocha.maroon    = QColor("#eba0ac"); mocha.pink      = QColor("#f5c2e7");
        mocha.flamingo  = QColor("#f2cdcd"); mocha.rosewater = QColor("#f5e0dc");
        mocha.computeDerived();
        m_palettes["mocha"] = mocha;

        // --- Macchiato ---
        ThemePalette macchiato;
        macchiato.name = "macchiato";
        macchiato.isLight = false;
        macchiato.base      = QColor("#24273a"); macchiato.mantle    = QColor("#1e2030"); macchiato.crust     = QColor("#181926");
        macchiato.surface0  = QColor("#363a4f"); macchiato.surface1  = QColor("#494d64"); macchiato.surface2  = QColor("#5b6078");
        macchiato.overlay0  = QColor("#6e738d"); macchiato.overlay1  = QColor("#8087a2"); macchiato.overlay2  = QColor("#939ab7");
        macchiato.text      = QColor("#cad3f5"); macchiato.subtext0  = QColor("#a5adcb"); macchiato.subtext1  = QColor("#b8c0e0");
        macchiato.mauve     = QColor("#c6a0f6"); macchiato.blue      = QColor("#8aadf4"); macchiato.green     = QColor("#a6da95");
        macchiato.red       = QColor("#ed8796"); macchiato.yellow    = QColor("#eed49f"); macchiato.peach     = QColor("#f5a97f");
        macchiato.sky       = QColor("#91d7e3"); macchiato.lavender  = QColor("#b7bdf8"); macchiato.teal      = QColor("#8bd5ca");
        macchiato.sapphire  = QColor("#7dc4e4"); macchiato.maroon    = QColor("#ee99a0"); macchiato.pink      = QColor("#f5bde6");
        macchiato.flamingo  = QColor("#f0c6c6"); macchiato.rosewater = QColor("#f4dbd6");
        macchiato.computeDerived();
        m_palettes["macchiato"] = macchiato;

        // --- Frappé ---
        ThemePalette frappe;
        frappe.name = "frappe";
        frappe.isLight = false;
        frappe.base      = QColor("#303446"); frappe.mantle    = QColor("#292c3c"); frappe.crust     = QColor("#232634");
        frappe.surface0  = QColor("#414559"); frappe.surface1  = QColor("#51576d"); frappe.surface2  = QColor("#626880");
        frappe.overlay0  = QColor("#737994"); frappe.overlay1  = QColor("#838ba7"); frappe.overlay2  = QColor("#949cbb");
        frappe.text      = QColor("#c6d0f5"); frappe.subtext0  = QColor("#a5adce"); frappe.subtext1  = QColor("#b5bfe2");
        frappe.mauve     = QColor("#ca9ee6"); frappe.blue      = QColor("#8caaee"); frappe.green     = QColor("#a6d189");
        frappe.red       = QColor("#e78284"); frappe.yellow    = QColor("#e5c890"); frappe.peach     = QColor("#ef9f76");
        frappe.sky       = QColor("#99d1db"); frappe.lavender  = QColor("#babbf1"); frappe.teal      = QColor("#81c8be");
        frappe.sapphire  = QColor("#85c1dc"); frappe.maroon    = QColor("#ea999c"); frappe.pink      = QColor("#f4b8e4");
        frappe.flamingo  = QColor("#eebebe"); frappe.rosewater = QColor("#f2d5cf");
        frappe.computeDerived();
        m_palettes["frappe"] = frappe;

        // --- Latte ---
        ThemePalette latte;
        latte.name = "latte";
        latte.isLight = true;
        latte.base      = QColor("#eff1f5"); latte.mantle    = QColor("#e6e9ef"); latte.crust     = QColor("#dce0e8");
        latte.surface0  = QColor("#ccd0da"); latte.surface1  = QColor("#bcc0cc"); latte.surface2  = QColor("#acb0be");
        latte.overlay0  = QColor("#9ca0b0"); latte.overlay1  = QColor("#8c8fa1"); latte.overlay2  = QColor("#7c7f93");
        latte.text      = QColor("#4c4f69"); latte.subtext0  = QColor("#6c6f85"); latte.subtext1  = QColor("#5c5f77");
        latte.mauve     = QColor("#8839ef"); latte.blue      = QColor("#1e66f5"); latte.green     = QColor("#40a02b");
        latte.red       = QColor("#d20f39"); latte.yellow    = QColor("#df8e1d"); latte.peach     = QColor("#fe640b");
        latte.sky       = QColor("#04a5e5"); latte.lavender  = QColor("#7287fd"); latte.teal      = QColor("#179299");
        latte.sapphire  = QColor("#209fb5"); latte.maroon    = QColor("#e64553"); latte.pink      = QColor("#ea76cb");
        latte.flamingo  = QColor("#dd7878"); latte.rosewater = QColor("#dc8a78");
        latte.computeDerived();
        m_palettes["latte"] = latte;

        // --- Cursor Dark (VS Code-inspired neutral grays) ---
        ThemePalette cursor;
        cursor.name = "cursor";
        cursor.isLight = false;
        cursor.base      = QColor("#1e1e1e"); cursor.mantle    = QColor("#181818"); cursor.crust     = QColor("#111111");
        cursor.surface0  = QColor("#2d2d2d"); cursor.surface1  = QColor("#3c3c3c"); cursor.surface2  = QColor("#4e4e4e");
        cursor.overlay0  = QColor("#646464"); cursor.overlay1  = QColor("#7a7a7a"); cursor.overlay2  = QColor("#909090");
        cursor.text      = QColor("#d4d4d4"); cursor.subtext0  = QColor("#a0a0a0"); cursor.subtext1  = QColor("#b0b0b0");
        cursor.mauve     = QColor("#c586c0"); cursor.blue      = QColor("#007acc"); cursor.green     = QColor("#4ec9b0");
        cursor.red       = QColor("#f44747"); cursor.yellow    = QColor("#dcdcaa"); cursor.peach     = QColor("#ce9178");
        cursor.sky       = QColor("#9cdcfe"); cursor.lavender  = QColor("#569cd6"); cursor.teal      = QColor("#4ec9b0");
        cursor.sapphire  = QColor("#4fc1ff"); cursor.maroon    = QColor("#d16969"); cursor.pink      = QColor("#c586c0");
        cursor.flamingo  = QColor("#d7ba7d"); cursor.rosewater = QColor("#d4d4d4");
        cursor.computeDerived();
        cursor.on_accent = QColor("#ffffff");
        m_palettes["cursor"] = cursor;
    }

    const ThemePalette &palette() const
    {
        auto it = m_palettes.constFind(m_currentTheme);
        if (it != m_palettes.constEnd())
            return it.value();
        static const ThemePalette fallback;
        return fallback;
    }
    QString currentThemeName() const { return m_currentTheme; }
    QStringList availableThemes() const { return {"cursor", "mocha", "macchiato", "frappe", "latte"}; }

    QColor color(const QString &token) const { return palette().color(token); }
    QString hex(const QString &token) const { return palette().hex(token); }

    QString generateStyleSheet() const
    {
        QString tmpl = loadQssTemplate();
        return substituteTokens(tmpl);
    }

    void setTheme(const QString &name)
    {
        QString key = name.toLower();
        if (key == "dark") key = "cursor";  // backward compat
        if (!m_palettes.contains(key))
            return;
        m_currentTheme = key;
        qApp->setStyleSheet(generateStyleSheet());
        emit themeChanged(key);
    }

signals:
    void themeChanged(const QString &name);

private:
    ThemeManager() : m_currentTheme("cursor") {}

    QString loadQssTemplate() const
    {
        QStringList searchPaths = {
            QApplication::applicationDirPath() + "/../resources/themes/theme.qss",
            QApplication::applicationDirPath() + "/../../resources/themes/theme.qss",
            QDir::homePath() + "/.cccpp/themes/theme.qss",
        };

        for (const QString &path : searchPaths) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text))
                return QString::fromUtf8(file.readAll());
        }

        // Inline fallback template (matches theme.qss)
        return QStringLiteral(R"(
* { font-family: -apple-system, "SF Pro Text", "Inter", "Segoe UI", system-ui, sans-serif; font-size: 13px; }
QMainWindow, QWidget { background: {{bg_window}}; color: {{text_primary}}; }
QScrollBar:vertical { background: transparent; width: 8px; border: none; }
QScrollBar::handle:vertical { background: {{border_subtle}}; border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: {{border_standard}}; }
QScrollBar::handle:vertical:pressed { background: {{blue}}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical,
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { height: 0; border: none; }
QScrollBar:horizontal { background: transparent; height: 8px; border: none; }
QScrollBar::handle:horizontal { background: {{border_subtle}}; border-radius: 4px; min-width: 24px; }
QScrollBar::handle:horizontal:hover { background: {{border_standard}}; }
QScrollBar::handle:horizontal:pressed { background: {{blue}}; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal,
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { width: 0; border: none; }
QMenuBar { background: {{bg_base}}; color: {{text_secondary}}; border: none; font-size: 13px; }
QMenuBar::item { padding: 4px 10px; border-radius: 6px; }
QMenuBar::item:selected { background: {{bg_raised}}; color: {{text_primary}}; }
QMenu { background: {{bg_surface}}; color: {{text_primary}}; border: 1px solid {{border_subtle}}; border-radius: 8px; padding: 6px; }
QMenu::item { padding: 6px 20px 6px 12px; border-radius: 4px; }
QMenu::item:selected { background: {{bg_raised}}; }
QMenu::separator { height: 1px; background: {{border_subtle}}; margin: 4px 8px; }
QSplitter::handle { background: {{border_subtle}}; width: 1px; }
QSplitter::handle:hover { background: {{blue}}; width: 2px; }
QSplitter::handle:pressed { background: {{blue}}; width: 2px; }
QToolTip { background: {{bg_raised}}; color: {{text_primary}}; border: 1px solid {{border_subtle}}; padding: 4px 8px; border-radius: 6px; font-size: 12px; }
QTabWidget::pane { border: none; background: {{bg_window}}; }
QTabBar { background: {{bg_base}}; border-bottom: 1px solid {{border_subtle}}; qproperty-drawBase: 0; }
QTabBar::tab { background: transparent; color: {{text_muted}}; border: none; padding: 6px 14px; font-size: 12px; min-width: 40px; margin: 0; }
QTabBar::tab:selected { color: {{text_primary}}; border-bottom: 2px solid {{blue}}; background: {{white_2pct}}; }
QTabBar::tab:hover:!selected { color: {{text_secondary}}; background: {{white_2pct}}; }
QTabBar::close-button { subcontrol-position: right; padding: 2px; }
QTabBar::close-button:hover { background: {{red_30pct}}; border-radius: 3px; }
QScrollArea { background: {{bg_window}}; border: none; }
QTreeView, QTreeWidget { background: {{bg_base}}; color: {{text_secondary}}; border: none; font-size: 12px; outline: none; }
QTreeView::item, QTreeWidget::item { padding: 2px 4px; min-height: 22px; }
QTreeView::item:selected, QTreeWidget::item:selected { background: {{bg_raised}}; color: {{text_primary}}; }
QTreeView::item:hover, QTreeWidget::item:hover { background: {{white_2pct}}; }
QTreeView::branch, QTreeWidget::branch { background: {{bg_base}}; }
QTextBrowser { background: transparent; color: {{text_primary}}; border: none; font-size: 13px; selection-background-color: {{hover_raised}}; }
QTextEdit#chatInput { background: {{bg_surface}}; color: {{text_primary}}; border: 1px solid {{border_subtle}}; border-radius: 12px; padding: 8px 12px; font-size: 13px; }
QTextEdit#chatInput:focus { border-color: {{blue}}; }
QLabel { background: transparent; }
QPushButton { background: {{bg_raised}}; color: {{text_primary}}; border: none; border-radius: 6px; padding: 5px 12px; font-size: 12px; }
QPushButton:hover { background: {{hover_raised}}; }
QPushButton:pressed { background: {{pressed_raised}}; }
QPushButton:disabled { background: {{bg_window}}; color: {{text_faint}}; }
QToolBar { background: {{bg_base}}; border: none; border-bottom: 1px solid {{border_subtle}}; spacing: 2px; }
QComboBox { background: {{bg_raised}}; color: {{text_primary}}; border: none; border-radius: 6px; padding: 3px 10px; font-size: 12px; }
QComboBox:hover { background: {{hover_raised}}; }
QComboBox::drop-down { border: none; width: 18px; }
QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid {{text_muted}}; }
QComboBox QAbstractItemView { background: {{bg_surface}}; color: {{text_primary}}; border: 1px solid {{border_subtle}}; border-radius: 6px; selection-background-color: {{bg_raised}}; padding: 4px; font-size: 12px; }
QLineEdit { background: {{bg_surface}}; color: {{text_primary}}; border: 1px solid {{border_subtle}}; border-radius: 6px; padding: 4px 8px; font-size: 13px; }
QLineEdit:focus { border-color: {{blue}}; }
QStatusBar { background: {{bg_base}}; border-top: 1px solid {{border_subtle}}; color: {{text_muted}}; font-size: 11px; min-height: 24px; padding: 0; }
QStatusBar::item { border: none; padding: 0; }
QStatusBar QLabel { color: {{text_muted}}; font-size: 11px; padding: 0 10px; border-right: 1px solid {{border_subtle}}; background: transparent; min-height: 24px; }
QStatusBar QLabel:last-child { border-right: none; }
)");
    }

    QString substituteTokens(const QString &tmpl) const
    {
        QString result = tmpl;
        static QRegularExpression re(R"(\{\{(\w+)\}\})");
        auto it = re.globalMatch(tmpl);
        // Collect unique tokens first to avoid redundant replacements
        QMap<QString, QString> replacements;
        while (it.hasNext()) {
            auto match = it.next();
            QString token = match.captured(1);
            if (!replacements.contains(token))
                replacements[token] = palette().hex(token);
        }
        for (auto rit = replacements.constBegin(); rit != replacements.constEnd(); ++rit)
            result.replace("{{" + rit.key() + "}}", rit.value());
        return result;
    }

    QMap<QString, ThemePalette> m_palettes;
    QString m_currentTheme;
};
