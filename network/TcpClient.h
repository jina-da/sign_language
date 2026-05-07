#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QJsonObject>

#include "ProtocolHandler.h"

/**
 * TcpClient
 *
 * 역할: 운용 서버(PORT 9000)와의 TCP 연결 및 메시지 송수신 담당.
 *
 * 주요 기능:
 *   - 서버 연결 / 연결 끊김 감지
 *   - 연결 끊기면 3초 후 자동 재연결 (무한 시도)
 *   - 서버가 없어도 프로그램이 정상 실행됨 (오프라인 모드)
 *   - ProtocolHandler를 이용해 패킷 조립/분해
 *
 * 사용 예:
 *   TcpClient *client = new TcpClient(this);
 *   connect(client, &TcpClient::messageReceived, this, &MyClass::onMessage);
 *   connect(client, &TcpClient::connectionChanged, this, &MyClass::onStatus);
 *   client->connectToServer("127.0.0.1", 9000);
 *
 *   // 메시지 보내기
 *   client->sendMessage({{"type", "login"},
 *                        {"username", "user001"},
 *                        {"password_hash", "abc..."}});
 */
class TcpClient : public QObject
{
    Q_OBJECT

public:
    explicit TcpClient(QObject *parent = nullptr);
    ~TcpClient() override = default;

    // ── 연결 제어 ─────────────────────────────────────
    /**
     * 서버에 연결을 시작한다.
     * 실패하면 자동으로 3초 후 재시도한다.
     */
    void connectToServer(const QString &host, quint16 port);

    /**
     * 서버 연결을 끊고 자동 재연결도 멈춘다.
     * (사용자가 명시적으로 종료할 때 호출)
     */
    void disconnectFromServer();

    /**
     * 현재 서버에 연결되어 있는지 반환한다.
     */
    bool isConnected() const;

    // ── 메시지 송신 ───────────────────────────────────
    /**
     * JSON 메시지를 서버로 전송한다.
     * 연결되어 있지 않으면 전송하지 않고 false를 반환한다.
     *
     * 사용 예:
     *   client->sendMessage({
     *       {"type", "keypoint"},
     *       {"word_id", 101},
     *       {"is_dominant_left", false},
     *       {"keypoint", keypointArray}
     *   });
     */
    bool sendMessage(const QJsonObject &msg);

signals:
    // ── 수신 시그널 ───────────────────────────────────
    /**
     * 서버로부터 완성된 메시지가 도착하면 발생한다.
     * @param msg 서버가 보낸 JSON 객체 (반드시 "type" 필드를 포함)
     *
     * 연결 방법:
     *   connect(client, &TcpClient::messageReceived, this, [this](const QJsonObject &msg) {
     *       QString type = msg["type"].toString();
     *       if (type == "login_result") { ... }
     *       else if (type == "keypoint_result") { ... }
     *   });
     */
    void messageReceived(const QJsonObject &msg);

    /**
     * 연결 상태가 바뀔 때마다 발생한다.
     * @param connected true = 연결됨, false = 끊김
     *
     * 사용 예:
     *   connect(client, &TcpClient::connectionChanged, this,
     *           [this](bool ok){ statusLabel->setText(ok ? "연결됨" : "연결 끊김"); });
     */
    void connectionChanged(bool connected);

    /**
     * 자동 재연결 시도 횟수가 올라갈 때 발생한다. (UI 표시용)
     * @param attempt 현재 시도 횟수 (1, 2, 3, ...)
     */
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
