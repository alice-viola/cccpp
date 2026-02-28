#include <QApplication>
#include "ui/MainWindow.h"
#include "util/Config.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("CCCPP");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("cccpp");

    Config::instance().load();

    MainWindow window;
    window.show();

    // If a path was provided as argument, open it as workspace
    QStringList args = app.arguments();
    if (args.size() > 1)
        window.openWorkspace(args.at(1));

    return app.exec();
}
