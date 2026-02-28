#pragma once

#include <QFileIconProvider>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QFileInfo>
#include <QMap>
#include <QString>

struct FileIconDef {
    QColor color;
    QString glyph;
};

class FileIconProvider : public QFileIconProvider {
public:
    static QIcon iconForFile(const QString &fileName)
    {
        QFileInfo fi(fileName);
        QString ext = fi.suffix().toLower();
        QString base = fi.fileName().toLower();

        auto def = lookup(ext, base);
        return paintIcon(def.color, def.glyph);
    }

    static QIcon folderIcon()
    {
        return paintIcon(QColor("#dcb67a"), "\xf0\x9f\x93\x81");
    }

    QIcon icon(IconType type) const override
    {
        if (type == Folder)
            return paintIcon(QColor("#dcb67a"), "\xf0\x9f\x93\x81");
        return paintIcon(QColor("#909090"), "\xf0\x9f\x93\x84");
    }

    QIcon icon(const QFileInfo &info) const override
    {
        if (info.isDir())
            return paintIcon(QColor("#dcb67a"), "\xf0\x9f\x93\x81");
        return iconForFile(info.fileName());
    }

private:
    static FileIconDef lookup(const QString &ext, const QString &baseName)
    {
        static const QMap<QString, FileIconDef> byExt = {
            // C / C++
            {"c",       {QColor("#519aba"), "C"}},
            {"h",       {QColor("#519aba"), "H"}},
            {"cpp",     {QColor("#519aba"), "C+"}},
            {"cxx",     {QColor("#519aba"), "C+"}},
            {"cc",      {QColor("#519aba"), "C+"}},
            {"hpp",     {QColor("#519aba"), "H+"}},
            {"hxx",     {QColor("#519aba"), "H+"}},
            // Rust
            {"rs",      {QColor("#dea584"), "Rs"}},
            // Go
            {"go",      {QColor("#519aba"), "Go"}},
            // Python
            {"py",      {QColor("#f1c04e"), "Py"}},
            {"pyw",     {QColor("#f1c04e"), "Py"}},
            // JavaScript / TypeScript
            {"js",      {QColor("#f1c04e"), "JS"}},
            {"mjs",     {QColor("#f1c04e"), "JS"}},
            {"jsx",     {QColor("#61dafb"), "Jx"}},
            {"ts",      {QColor("#3178c6"), "TS"}},
            {"tsx",     {QColor("#3178c6"), "Tx"}},
            // Web
            {"html",    {QColor("#e44d26"), "<>"}},
            {"htm",     {QColor("#e44d26"), "<>"}},
            {"css",     {QColor("#563d7c"), "#"}},
            {"scss",    {QColor("#c6538c"), "S#"}},
            {"vue",     {QColor("#41b883"), "V"}},
            {"svelte",  {QColor("#ff3e00"), "Sv"}},
            // Data / Config
            {"json",    {QColor("#f1c04e"), "{}"}},
            {"jsonc",   {QColor("#f1c04e"), "{}"}},
            {"yaml",    {QColor("#cb171e"), "Y"}},
            {"yml",     {QColor("#cb171e"), "Y"}},
            {"toml",    {QColor("#9c4221"), "T"}},
            {"xml",     {QColor("#e44d26"), "Xm"}},
            {"svg",     {QColor("#ffb13b"), "Sv"}},
            // Docs
            {"md",      {QColor("#519aba"), "Md"}},
            {"mdx",     {QColor("#519aba"), "Md"}},
            {"txt",     {QColor("#909090"), "Tx"}},
            {"rst",     {QColor("#909090"), "Rs"}},
            // Shell
            {"sh",      {QColor("#4eaa25"), "$"}},
            {"bash",    {QColor("#4eaa25"), "$"}},
            {"zsh",     {QColor("#4eaa25"), "$"}},
            {"fish",    {QColor("#4eaa25"), "$"}},
            // Ruby
            {"rb",      {QColor("#cc342d"), "Rb"}},
            {"rake",    {QColor("#cc342d"), "Rb"}},
            // Java / Kotlin
            {"java",    {QColor("#b07219"), "Jv"}},
            {"kt",      {QColor("#a97bff"), "Kt"}},
            // C# / .NET
            {"cs",      {QColor("#68217a"), "C#"}},
            // PHP
            {"php",     {QColor("#4f5d95"), "Ph"}},
            // Swift
            {"swift",   {QColor("#f05138"), "Sw"}},
            // SQL
            {"sql",     {QColor("#e38c00"), "Sq"}},
            // Lua
            {"lua",     {QColor("#000080"), "Lu"}},
            // Docker / Infra
            {"dockerfile", {QColor("#2496ed"), "Dk"}},
            // Build
            {"cmake",   {QColor("#064f8c"), "Cm"}},
            {"mk",      {QColor("#427819"), "Mk"}},
            // Images
            {"png",     {QColor("#a074c4"), "Im"}},
            {"jpg",     {QColor("#a074c4"), "Im"}},
            {"jpeg",    {QColor("#a074c4"), "Im"}},
            {"gif",     {QColor("#a074c4"), "Im"}},
            {"webp",    {QColor("#a074c4"), "Im"}},
            {"ico",     {QColor("#a074c4"), "Im"}},
            // Git
            {"gitignore", {QColor("#f05032"), "Gi"}},
            // Misc
            {"env",     {QColor("#ecd53f"), ".e"}},
            {"lock",    {QColor("#909090"), "Lk"}},
            {"log",     {QColor("#909090"), "Lg"}},
        };

        static const QMap<QString, FileIconDef> byName = {
            {"makefile",       {QColor("#427819"), "Mk"}},
            {"gnumakefile",    {QColor("#427819"), "Mk"}},
            {"cmakelists.txt", {QColor("#064f8c"), "Cm"}},
            {"dockerfile",     {QColor("#2496ed"), "Dk"}},
            {"gemfile",        {QColor("#cc342d"), "Rb"}},
            {"rakefile",       {QColor("#cc342d"), "Rb"}},
            {".gitignore",     {QColor("#f05032"), "Gi"}},
            {".env",           {QColor("#ecd53f"), ".e"}},
            {"readme.md",      {QColor("#519aba"), "Md"}},
            {"license",        {QColor("#909090"), "Li"}},
            {"license.md",     {QColor("#909090"), "Li"}},
        };

        auto nameIt = byName.find(baseName);
        if (nameIt != byName.end())
            return nameIt.value();

        auto extIt = byExt.find(ext);
        if (extIt != byExt.end())
            return extIt.value();

        return {QColor("#909090"), "\xf0\x9f\x93\x84"};
    }

    static QIcon paintIcon(const QColor &color, const QString &glyph)
    {
        constexpr int sz = 16;
        QPixmap pm(sz * 2, sz * 2);
        pm.fill(Qt::transparent);
        pm.setDevicePixelRatio(2.0);

        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);

        bool isEmoji = !glyph.isEmpty() && glyph.at(0).unicode() > 0x2000;

        if (isEmoji) {
            QFont f;
            f.setPixelSize(13);
            p.setFont(f);
            p.setPen(color);
            p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, glyph);
        } else {
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRoundedRect(1, 1, sz - 2, sz - 2, 3, 3);

            QFont f("SF Mono", 7);
            f.setBold(true);
            f.setPixelSize(glyph.size() > 2 ? 6 : 8);
            p.setFont(f);
            p.setPen(Qt::white);
            p.drawText(QRect(0, 0, sz, sz), Qt::AlignCenter, glyph);
        }

        p.end();
        return QIcon(pm);
    }
};
