#pragma once

#include <QObject>
#include <QJsonObject>
#include "widgets/LoginWidget.h"
#include "widgets/MainWindow.h"
#include "widgets/ReviewWidget.h"
#include "widgets/DictWidget.h"
#include "widgets/SettingsWidget.h"
#include "widgets/StudyWidget.h"
#include "widgets/RegisterWidget.h"
#include "network/TcpClient.h"

// ─── 서버 접속 정보 ───────────────────────────────────────────
// IP나 포트를 변경할 때 이 부분만 수정하면 됩니다.
static constexpr const char* SERVER_HOST = "10.10.10.114";
static constexpr int         SERVER_PORT = 9000;
// ─────────────────────────────────────────────────────────────

class AppController : public QObject
{
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    void start();

private slots:
    void onMessageReceived(const QJsonObject &msg);
    void onLoginRequested(const QString &username, const QString &passwordHash);
    void onConnectionChanged(bool connected);

private:
    TcpClient      *m_client;
    LoginWidget    *m_loginWidget;
    MainWindow     *m_mainWindow;
    RegisterWidget *m_registerWidget;

    QString m_sessionToken;
    QString m_lastUsername;
};
