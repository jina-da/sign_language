#include "TcpClient.h"

#include <QDebug>

// ─────────────────────────────────────────────────────────────
// 생성자 — 소켓, 프로토콜 핸들러, 재연결 타이머 초기화
// ─────────────────────────────────────────────────────────────
TcpClient::TcpClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_protocol(new ProtocolHandler(this))
    , m_reconnectTimer(new QTimer(this))
{
    // 재연결 타이머: 한 번만 울리는 단발 타이머
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(RECONNECT_INTERVAL_MS);

    // ── 소켓 시그널 연결 ──────────────────────────────
    connect(m_socket, &QTcpSocket::connected,
            this,     &TcpClient::onConnected);

    connect(m_socket, &QTcpSocket::disconnected,
            this,     &TcpClient::onDisconnected);

    connect(m_socket, &QTcpSocket::readyRead,
            this,     &TcpClient::onReadyRead);

    // Qt6: errorOccurred 시그널 사용
    connect(m_socket, &QAbstractSocket::errorOccurred,
            this,     &TcpClient::onSocketError);

    // ── 프로토콜 핸들러 시그널 연결 ──────────────────
    // ProtocolHandler가 완성된 메시지를 만들면 우리도 바깥에 알린다
    connect(m_protocol, &ProtocolHandler::messageReady,
            this,       &TcpClient::messageReceived);

    connect(m_protocol, &ProtocolHandler::parseError,
            this, [](const QByteArray &raw) {
                qWarning() << "[TcpClient] JSON 파싱 오류. 원본 길이:" << raw.size();
            });

    // ── 재연결 타이머 ─────────────────────────────────
    connect(m_reconnectTimer, &QTimer::timeout,
            this,             &TcpClient::onReconnectTimer);
}

// ─────────────────────────────────────────────────────────────
// connectToServer() — 서버에 연결 시작
// ─────────────────────────────────────────────────────────────
void TcpClient::connectToServer(const QString &host, quint16 port)
{
    m_host = host;
    m_port = port;
    m_intentionalDisconnect = false; // 자동 재연결 허용 상태로 초기화
    m_reconnectAttempts = 0;

    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort(); // 이전 연결 강제 해제
    }

    qDebug() << "[TcpClient] 서버 연결 시도:" << host << ":" << port;
    m_socket->connectToHost(host, port);
}

// ─────────────────────────────────────────────────────────────
// disconnectFromServer() — 사용자가 직접 연결 끊기
// ─────────────────────────────────────────────────────────────
void TcpClient::disconnectFromServer()
{
    m_intentionalDisconnect = true;       // 자동 재연결 하지 않음
    m_reconnectTimer->stop();
    m_socket->disconnectFromHost();
}

// ─────────────────────────────────────────────────────────────
// isConnected()
// ─────────────────────────────────────────────────────────────
bool TcpClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

// ─────────────────────────────────────────────────────────────
// sendMessage() — JSON 메시지를 서버로 전송
// ─────────────────────────────────────────────────────────────
bool TcpClient::sendMessage(const QJsonObject &msg)
{
    if (!isConnected()) {
        qWarning() << "[TcpClient] 서버 미연결 상태. 전송 불가. type:"
                   << msg["type"].toString();
        return false;
    }

    QByteArray packet = ProtocolHandler::pack(msg);
    qint64 written = m_socket->write(packet);

    if (written != packet.size()) {
        qWarning() << "[TcpClient] 전송 오류: 예상" << packet.size()
                   << "바이트, 실제" << written << "바이트";
        return false;
    }

    return true;
}

// ─────────────────────────────────────────────────────────────
// onConnected() — 연결 성공
// ─────────────────────────────────────────────────────────────
void TcpClient::onConnected()
{
    m_reconnectAttempts = 0;
    m_reconnectTimer->stop();
    m_protocol->reset(); // 이전 연결의 잔여 데이터 초기화

    qDebug() << "[TcpClient] 서버 연결 성공:" << m_host << ":" << m_port;
    emit connectionChanged(true);
}

// ─────────────────────────────────────────────────────────────
// onDisconnected() — 연결 끊김
// ─────────────────────────────────────────────────────────────
void TcpClient::onDisconnected()
{
    qDebug() << "[TcpClient] 서버 연결 끊김.";
    emit connectionChanged(false);

    // 사용자가 직접 끊은 경우에는 재연결하지 않는다
    if (m_intentionalDisconnect) {
        return;
    }

    // 3초 후 재연결 시도
    m_reconnectTimer->start();
}

// ─────────────────────────────────────────────────────────────
// onReadyRead() — 소켓에서 데이터가 들어옴
// ─────────────────────────────────────────────────────────────
void TcpClient::onReadyRead()
{
    // 받은 바이트를 프로토콜 핸들러에 넘긴다.
    // 완성된 메시지가 있으면 ProtocolHandler가 messageReady를 emit하고
    // 그게 우리 messageReceived 시그널로 연결된다.
    m_protocol->feed(m_socket->readAll());
}

// ─────────────────────────────────────────────────────────────
// onSocketError() — 소켓 오류 발생
// ─────────────────────────────────────────────────────────────
void TcpClient::onSocketError(QAbstractSocket::SocketError err)
{
    // RemoteClosedError는 onDisconnected()에서 처리하므로 여기서는 무시
    if (err == QAbstractSocket::RemoteHostClosedError) {
        return;
    }

    qWarning() << "[TcpClient] 소켓 오류:" << m_socket->errorString();

    // 연결 자체를 못 한 경우 (서버가 꺼져 있을 때 등)
    // onDisconnected가 자동으로 호출되어 재연결 타이머가 동작한다.
}

// ─────────────────────────────────────────────────────────────
// onReconnectTimer() — 재연결 타이머 만료 → 재연결 시도
// ─────────────────────────────────────────────────────────────
void TcpClient::onReconnectTimer()
{
    if (m_intentionalDisconnect) {
        return;
    }

    m_reconnectAttempts++;
    qDebug() << "[TcpClient] 재연결 시도 #" << m_reconnectAttempts;
    emit reconnectAttempt(m_reconnectAttempts);

    m_socket->connectToHost(m_host, m_port);
}
