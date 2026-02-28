#pragma once

#include <QColor>
#include <QMap>
#include <QString>

struct ThemePalette {
    QString name;  // "mocha", "macchiato", "frappe", "latte"
    bool isLight = false;

    // --- Catppuccin canonical colors ---
    QColor base, mantle, crust;
    QColor surface0, surface1, surface2;
    QColor overlay0, overlay1, overlay2;
    QColor text, subtext0, subtext1;
    QColor mauve, blue, green, red, yellow, peach, sky;
    QColor lavender, teal, sapphire, maroon, pink, flamingo, rosewater;

    // --- Semantic tokens (computed from canonical) ---
    QColor bg_base;       // deepest: terminal, editor margins
    QColor bg_surface;    // cards, menus, elevated panels
    QColor bg_window;     // main window, editor paper
    QColor bg_raised;     // buttons, selected items

    QColor border_subtle;    // within same surface
    QColor border_standard;  // between panels, inputs
    QColor border_focus;     // focused inputs

    QColor text_primary;
    QColor text_secondary;
    QColor text_muted;
    QColor text_faint;

    QColor on_accent;  // text on accent-colored buttons

    // --- Derived colors (blended) ---
    QColor diff_add_bg;
    QColor diff_del_bg;
    QColor diff_phantom_bg;
    QColor success_btn_bg;
    QColor success_btn_hover;
    QColor error_btn_bg;
    QColor error_btn_hover;
    QColor hover_raised;
    QColor pressed_raised;

    // Precomputed rgba-style tokens for QSS template
    QColor mauve_5pct;
    QColor white_2pct;
    QColor red_30pct;

    void computeDerived()
    {
        bg_base = crust;
        bg_surface = mantle;
        bg_window = base;
        bg_raised = surface0;

        border_subtle = surface0;
        border_standard = surface1;
        border_focus = surface2;

        text_primary = text;
        text_secondary = subtext0;
        text_muted = overlay0;
        text_faint = surface1;

        on_accent = crust;

        auto blend = [](QColor fg, QColor bg, qreal alpha) -> QColor {
            return QColor(
                int(fg.red()   * alpha + bg.red()   * (1.0 - alpha)),
                int(fg.green() * alpha + bg.green() * (1.0 - alpha)),
                int(fg.blue()  * alpha + bg.blue()  * (1.0 - alpha)));
        };

        qreal diffAlpha = isLight ? 0.15 : 0.12;
        diff_add_bg = blend(green, base, diffAlpha);
        diff_del_bg = blend(red, base, diffAlpha);
        diff_phantom_bg = blend(blue, base, 0.08);

        success_btn_bg = blend(green, base, 0.30);
        success_btn_hover = blend(green, base, 0.40);
        error_btn_bg = blend(red, base, 0.30);
        error_btn_hover = blend(red, base, 0.40);

        hover_raised = blend(text, surface0, 0.10);
        pressed_raised = blend(text, surface0, 0.15);

        mauve_5pct = blend(mauve, base, 0.05);
        red_30pct = blend(red, base, 0.30);
        white_2pct = blend(QColor(255, 255, 255), base, 0.025);

        if (isLight)
            on_accent = QColor("#eff1f5");  // light base for text on dark accents
    }

    QColor color(const QString &token) const
    {
        static QMap<QString, QColor ThemePalette::*> members;
        if (members.isEmpty()) {
            // Canonical
            members["base"] = &ThemePalette::base;
            members["mantle"] = &ThemePalette::mantle;
            members["crust"] = &ThemePalette::crust;
            members["surface0"] = &ThemePalette::surface0;
            members["surface1"] = &ThemePalette::surface1;
            members["surface2"] = &ThemePalette::surface2;
            members["overlay0"] = &ThemePalette::overlay0;
            members["overlay1"] = &ThemePalette::overlay1;
            members["overlay2"] = &ThemePalette::overlay2;
            members["text"] = &ThemePalette::text;
            members["subtext0"] = &ThemePalette::subtext0;
            members["subtext1"] = &ThemePalette::subtext1;
            members["mauve"] = &ThemePalette::mauve;
            members["blue"] = &ThemePalette::blue;
            members["green"] = &ThemePalette::green;
            members["red"] = &ThemePalette::red;
            members["yellow"] = &ThemePalette::yellow;
            members["peach"] = &ThemePalette::peach;
            members["sky"] = &ThemePalette::sky;
            members["lavender"] = &ThemePalette::lavender;
            members["teal"] = &ThemePalette::teal;
            members["sapphire"] = &ThemePalette::sapphire;
            members["maroon"] = &ThemePalette::maroon;
            members["pink"] = &ThemePalette::pink;
            members["flamingo"] = &ThemePalette::flamingo;
            members["rosewater"] = &ThemePalette::rosewater;
            // Semantic
            members["bg_base"] = &ThemePalette::bg_base;
            members["bg_surface"] = &ThemePalette::bg_surface;
            members["bg_window"] = &ThemePalette::bg_window;
            members["bg_raised"] = &ThemePalette::bg_raised;
            members["border_subtle"] = &ThemePalette::border_subtle;
            members["border_standard"] = &ThemePalette::border_standard;
            members["border_focus"] = &ThemePalette::border_focus;
            members["text_primary"] = &ThemePalette::text_primary;
            members["text_secondary"] = &ThemePalette::text_secondary;
            members["text_muted"] = &ThemePalette::text_muted;
            members["text_faint"] = &ThemePalette::text_faint;
            members["on_accent"] = &ThemePalette::on_accent;
            // Derived
            members["diff_add_bg"] = &ThemePalette::diff_add_bg;
            members["diff_del_bg"] = &ThemePalette::diff_del_bg;
            members["diff_phantom_bg"] = &ThemePalette::diff_phantom_bg;
            members["success_btn_bg"] = &ThemePalette::success_btn_bg;
            members["success_btn_hover"] = &ThemePalette::success_btn_hover;
            members["error_btn_bg"] = &ThemePalette::error_btn_bg;
            members["error_btn_hover"] = &ThemePalette::error_btn_hover;
            members["hover_raised"] = &ThemePalette::hover_raised;
            members["pressed_raised"] = &ThemePalette::pressed_raised;
            members["mauve_5pct"] = &ThemePalette::mauve_5pct;
            members["white_2pct"] = &ThemePalette::white_2pct;
            members["red_30pct"] = &ThemePalette::red_30pct;
        }
        auto it = members.find(token);
        if (it != members.end())
            return this->*it.value();
        return {};
    }

    QString hex(const QString &token) const
    {
        return color(token).name();  // returns "#rrggbb"
    }
};
