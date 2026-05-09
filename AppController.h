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
