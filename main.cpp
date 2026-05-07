#include <QApplication>
#include "AppController.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("수화배움");
    app.setOrganizationName("SignLearn");

    AppController ctrl;
    ctrl.start();

    return app.exec();
}