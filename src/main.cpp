#include "core/Settings.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName(QStringLiteral("LightIDE"));
    QApplication::setOrganizationName(QStringLiteral("LightIDE"));
    QApplication::setApplicationVersion(QStringLiteral(LIGHTIDE_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("lightide"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Lightweight C/C++ IDE"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("files"),
                                 QStringLiteral("Source files, a folder or a .lideproj project to open."),
                                 QStringLiteral("[files...]"));
    parser.process(application);

    Settings settings;
    settings.load();

    MainWindow window(&settings);
    window.openPaths(parser.positionalArguments());
    window.show();
    return QApplication::exec();
}
