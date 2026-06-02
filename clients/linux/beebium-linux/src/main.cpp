#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
#include <kddockwidgets/Config.h>
#endif

#include "ConnectionTarget.hpp"
#include "MainWindow.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Beebium Linux"));
    QApplication::setOrganizationName(QStringLiteral("Beebium"));

#if defined(BEEBIUM_HAVE_KDDOCKWIDGETS)
    KDDockWidgets::initFrontend(KDDockWidgets::FrontendType::QtWidgets);
    KDDockWidgets::Config::self().setFlags(
        KDDockWidgets::Config::Flag_AlwaysShowTabs
        | KDDockWidgets::Config::Flag_HideTitleBarWhenTabsVisible
        | KDDockWidgets::Config::Flag_DisableDoubleClick);
#endif

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Native Linux frontend for the Beebium emulator"));
    parser.addHelpOption();

    QCommandLineOption hostOption(QStringList() << QStringLiteral("H") << QStringLiteral("host"),
                                  QStringLiteral("Server host"),
                                  QStringLiteral("host"),
                                  QStringLiteral("127.0.0.1"));
    QCommandLineOption portOption(QStringList() << QStringLiteral("p") << QStringLiteral("port"),
                                  QStringLiteral("Server port"),
                                  QStringLiteral("port"),
                                  QStringLiteral("48875"));
    QCommandLineOption configFolderOption(QStringList() << QStringLiteral("config-folder"),
                                          QStringLiteral("Config folder for persisted UI state and layout"),
                                          QStringLiteral("path"));
    parser.addOption(hostOption);
    parser.addOption(portOption);
    parser.addOption(configFolderOption);
    parser.process(app);

    const QString configFolder = parser.value(configFolderOption).trimmed();
    if (!configFolder.isEmpty()) {
        QDir().mkpath(configFolder);
        app.setProperty("configFolder", configFolder);
    }

    ConnectionTarget initialTarget;
    initialTarget.host = parser.value(hostOption);
    initialTarget.port = parser.value(portOption).toInt();
    if (initialTarget.port <= 0) {
        initialTarget.port = 48875;
    }

    MainWindow window(initialTarget);
    window.show();
    return app.exec();
}
