#pragma once

#include <QObject>
#include <QJsonObject>
#include "widgets/LoginWidget.h"
#include "widgets/MainWindow.h"
#include "widgets/StudyWidget.h"
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
    TcpClient   *m_client;
    LoginWidget *m_loginWidget;
    MainWindow  *m_mainWindow;
};
