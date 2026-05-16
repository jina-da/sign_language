#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>

#include "ProtocolHandler.h"

class TcpClient : public QObject
{
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient() override = default;

    // 연결 제어
    void connectToServer(const QString &host, quint16 port);
    void disconnectFromServer();
    bool isConnected() const;

    // 메시지 송신
    bool sendMessage(const QJsonObject &msg);

signals:
    // 수신 시그널
    void messageReceived(const QJsonObject &msg);
    void connectionChanged(bool connected);
    void reconnectAttempt(int attempt);

private slots:
    void onConnected();
    void onDisconnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError err);
    void onReconnectTimer();

private:
    QTcpSocket      *m_socket;
    ProtocolHandler *m_protocol;
    QTimer          *m_reconnectTimer;

    QString  m_host;
    quint16  m_port = 9000;

    bool m_intentionalDisconnect = false; // 사용자가 직접 끊은 경우
    int  m_reconnectAttempts     = 0;

    static constexpr int RECONNECT_INTERVAL_MS = 3000; // 재연결 간격 3초
    static constexpr int CONNECT_TIMEOUT_MS    = 5000; // 연결 타임아웃 5초
};
