#include "barwindow.h"
#include "config.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qbar"));
    QCoreApplication::setOrganizationName(QStringLiteral("qbar"));

    BarWindow window(loadConfig());
    window.show();

    return app.exec();
}
