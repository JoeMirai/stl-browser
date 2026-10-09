#include <QApplication>

#include "app.h"

int main(int argc, char* argv[])
{
    // Force C locale to force decimal point
    QLocale::setDefault(QLocale::c());

    QCoreApplication::setOrganizationName("JoeMirai");
    QCoreApplication::setOrganizationDomain("github.com/JoeMirai/stl-browser");
    QCoreApplication::setApplicationName("stl-browser");
    QCoreApplication::setApplicationVersion(FSTL_VERSION);
    QGuiApplication::setDesktopFileName("stl-browser");
    App a(argc, argv);

    return a.exec();
}
