#include <QApplication>
#include "AppController.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("수화배움");
    app.setOrganizationName("SignLearn");

    AppController ctrl;
    ctrl.start();

    // 앱 종료 시(X 버튼, 강제 종료 등 모든 경로) keypoint_server 정리 보장
    QObject::connect(&app, &QApplication::aboutToQuit, [&ctrl] {
        ctrl.cleanup();
    });

    return app.exec();
}