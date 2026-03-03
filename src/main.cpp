#include <QApplication>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QFont>
#include <QStringList>
#include <QLocalSocket>
#include <QLocalServer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QThread>
#include "ui/MainWindow.h"
#include "core/TelegramDaemon.h"
#include "util/Config.h"

#ifdef Q_OS_UNIX
#include <signal.h>
#endif

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

static int killDaemon()
{
    QCoreApplication::setApplicationName("CCCPP");

    // Try graceful shutdown via IPC socket first
    QLocalSocket sock;
    sock.connectToServer(TelegramDaemon::serverName());
    if (sock.waitForConnected(1000)) {
        QByteArray msg = QJsonDocument(
            QJsonObject{{"type", "shutdown"}}
        ).toJson(QJsonDocument::Compact) + '\n';
        sock.write(msg);
        sock.flush();
        sock.waitForBytesWritten(500);
        sock.disconnectFromServer();
        QThread::msleep(500); // give it a moment to exit
        fprintf(stdout, "Sent shutdown to daemon via IPC.\n");
        // Clean up any leftovers just in case
        TelegramDaemon::tryCleanupStale();
        return 0;
    }

    // Daemon not connectable — try SIGTERM via PID in lock file
#ifdef Q_OS_UNIX
    QFile f(TelegramDaemon::lockFilePath());
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        pid_t pid = static_cast<pid_t>(f.readAll().trimmed().toLongLong());
        f.close();
        if (pid > 0 && kill(pid, 0) == 0) {
            kill(pid, SIGTERM);
            fprintf(stdout, "Sent SIGTERM to daemon PID %d.\n", pid);
            QThread::msleep(1000);
            if (kill(pid, 0) == 0) {
                kill(pid, SIGKILL);
                fprintf(stdout, "Daemon did not exit, sent SIGKILL.\n");
            }
        }
    }
#endif

    // Remove lock file and socket regardless
    TelegramDaemon::tryCleanupStale();
    QFile::remove(TelegramDaemon::lockFilePath());
    QLocalServer::removeServer(TelegramDaemon::serverName());
    fprintf(stdout, "Cleaned up daemon lock and socket.\n");
    return 0;
}

int main(int argc, char *argv[])
{
    // Check for --daemon / --kill-daemon flags before creating QApplication
    bool daemonMode = false;
    bool killDaemonMode = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--daemon") {
            daemonMode = true;
            break;
        }
        if (QString(argv[i]) == "--kill-daemon") {
            killDaemonMode = true;
            break;
        }
    }

    if (killDaemonMode) {
        QCoreApplication app(argc, argv);
        app.setApplicationName("CCCPP");
        return killDaemon();
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

    qSetMessagePattern("[%{time HH:mm:ss.zzz}] %{message}");

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
