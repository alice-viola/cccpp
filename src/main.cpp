#include <QApplication>
#include <QFontDatabase>
#include <QFont>
#include "ui/MainWindow.h"
#include "util/Config.h"

static void loadBundledFonts()
{
    const QStringList fontFiles = {
        QStringLiteral(":/fonts/Inter-Regular.ttf"),
        QStringLiteral(":/fonts/Inter-Medium.ttf"),
        QStringLiteral(":/fonts/Inter-SemiBold.ttf"),
        QStringLiteral(":/fonts/Inter-Bold.ttf"),
        QStringLiteral(":/fonts/JetBrainsMono-Regular.ttf"),
        QStringLiteral(":/fonts/JetBrainsMono-Medium.ttf"),
        QStringLiteral(":/fonts/JetBrainsMono-Bold.ttf"),
    };
    for (const auto &f : fontFiles)
        QFontDatabase::addApplicationFont(f);
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("CCCPP");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("cccpp");

    loadBundledFonts();

    QFont defaultFont(QStringLiteral("Inter"), 13);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(defaultFont);

    Config::instance().load();

    MainWindow window;
    window.show();

    // If a path was provided as argument, open it as workspace
    QStringList args = app.arguments();
    if (args.size() > 1)
        window.openWorkspace(args.at(1));

    return app.exec();
}
