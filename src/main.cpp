#include <QApplication>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QFont>
#include <QStringList>
#include "ui/MainWindow.h"
#include "core/TelegramDaemon.h"
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
    // Check for --daemon flag before creating QApplication (daemon needs no GUI)
    bool daemonMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--daemon") {
            daemonMode = true;
            break;
        }
    }

    if (daemonMode) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("CCCPP");
        app.setApplicationVersion("0.1.0");
        app.setOrganizationName("cccpp");

        Config::instance().load();

        TelegramDaemon daemon;
        if (!daemon.start()) {
            qWarning() << "Failed to start Telegram daemon";
            return 1;
        }

        return app.exec();
    }

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
    for (int i = 1; i < args.size(); ++i) {
        if (!args[i].startsWith('-')) {
            window.openWorkspace(args[i]);
            break;
        }
    }

    return app.exec();
}
