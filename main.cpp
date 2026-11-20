#include "mainwindow.h"

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QStyleFactory>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("Ralf Tralow"));
    app.setApplicationName(QStringLiteral("HP3488A"));
    app.setApplicationVersion(QStringLiteral(HP3488_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/hp3488-icon-c.ico")));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont font = app.font();
    font.setFamilies({QStringLiteral("Inter"), QStringLiteral("Segoe UI"), QStringLiteral("Arial")});
    font.setPointSizeF(10.5);
    app.setFont(font);
    MainWindow window;
    window.show();
    return app.exec();
}
