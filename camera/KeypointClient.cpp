#include "KeypointClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

KeypointClient::KeypointClient(QObject *parent)
    : QObject(parent)
    , m_frameSocket(new QTcpSocket(this))
    , m_keypointSocket(new QTcpSocket(this))
    , m_controlSocket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(RECONNECT_MS);

    // 데이터 수신
    connect(m_frameSocket,    &QTcpSocket::readyRead,
            this,             &KeypointClient::onFrameReadyRead);
    connect(m_keypointSocket, &QTcpSocket::readyRead,
            this,             &KeypointClient::onKeypointReadyRead);

    // frameSocket 연결 성공
    connect(m_frameSocket, &QTcpSocket::connected, this, [this] {
        m_frameConnected = true;
        qDebug() << "[KP] frameSocket 연결됨";
        checkAndEmitConnected();
    });

    // keypointSocket 연결 성공
    connect(m_keypointSocket, &QTcpSocket::connected, this, [this] {
        m_keypointConnected = true;
        qDebug() << "[KP] keypointSocket 연결됨";
        checkAndEmitConnected();
    });

    // controlSocket 연결 성공 → 대기 중 우세손 전송
    connect(m_controlSocket, &QTcpSocket::connected, this, [this] {
        qDebug() << "[KP] 제어 소켓 연결 완료";
        if (m_hasPendingDominant)
            sendDominantHand();
    });

    // 연결 실패 (초기 연결 시도가 거부된 경우)
    connect(m_frameSocket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        if (m_intentionalDisconnect || m_connected) return;
        if (!m_reconnectTimer->isActive())
            m_reconnectTimer->start();
    });

    // 데이터 소켓 끊김 (frame / keypoint)
    connect(m_frameSocket,    &QTcpSocket::disconnected,
            this,             &KeypointClient::onDataSocketDisconnected);
    connect(m_keypointSocket, &QTcpSocket::disconnected,
            this,             &KeypointClient::onDataSocketDisconnected);

    // controlSocket 끊김 — 데이터 소켓과 독립 처리
    connect(m_controlSocket, &QTcpSocket::disconnected,
            this,            &KeypointClient::onControlDisconnected);

    // 재연결 타이머
    connect(m_reconnectTimer, &QTimer::timeout,
            this,             &KeypointClient::tryReconnect);
}

// 두 데이터 소켓이 모두 연결됐을 때 connected 시그널 발생
void KeypointClient::checkAndEmitConnected()
{
    if (m_frameConnected && m_keypointConnected && !m_connected) {
        m_connected = true;
        m_reconnectTimer->stop();
        qDebug() << "[KP] keypoint_server 연결 성공";
        emit connectionChanged(true);
    }
}

// 최초 연결
void KeypointClient::connectToServer(const QString &host)
{
    m_host = host;
    m_intentionalDisconnect = false;

    if (m_frameSocket->state() != QAbstractSocket::UnconnectedState)
        return;

    qDebug() << "[KP] 연결 시도:" << host;
    m_frameSocket->connectToHost(host, FRAME_PORT);
    m_keypointSocket->connectToHost(host, KEYPOINT_PORT);
    m_controlSocket->connectToHost(host, CONTROL_PORT);

    // 타이머는 연결 실패 시(onDataSocketDisconnected)에만 시작한다.
    // 여기서 start()하면 두 소켓이 모두 연결되기 전에 타이머가 만료돼
    // tryReconnect()가 정상 연결을 끊는 문제가 발생한다.
}

// 명시적 연결 해제
void KeypointClient::disconnectFromServer()
{
    m_intentionalDisconnect = true;
    m_reconnectTimer->stop();
    m_frameSocket->disconnectFromHost();
    m_keypointSocket->disconnectFromHost();
    m_controlSocket->disconnectFromHost();
}

// 우세손 설정
void KeypointClient::setDominantHand(bool isDominantLeft)
{
    m_pendingDominantLeft = isDominantLeft;
    m_hasPendingDominant  = true;

    if (m_controlSocket->state() != QAbstractSocket::ConnectedState) {
        qDebug() << "[KP] 제어 소켓 연결 대기 중 - 연결 후 자동 전송";
        return;
    }
    sendDominantHand();
}

void KeypointClient::sendDominantHand()
{
    QJsonObject msg;
    msg["type"]             = "set_dominant_hand";
    msg["is_dominant_left"] = m_pendingDominantLeft;

    QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray header(4, 0);
    quint32 len = static_cast<quint32>(body.size());
    header[0] = (len >> 24) & 0xFF;
    header[1] = (len >> 16) & 0xFF;
    header[2] = (len >>  8) & 0xFF;
    header[3] =  len        & 0xFF;

    m_controlSocket->write(header + body);
    m_hasPendingDominant = false;
    qDebug() << "[KP] 우세손 설정 전송:"
             << (m_pendingDominantLeft ? "왼손" : "오른손");
}

bool KeypointClient::isConnected() const
{
    return m_connected;
}

// 프레임/키포인트 수신
void KeypointClient::onFrameReadyRead()
{
    m_frameBuffer.append(m_frameSocket->readAll());
    processBuffer(m_frameBuffer, true);
}

void KeypointClient::onKeypointReadyRead()
{
    m_keypointBuffer.append(m_keypointSocket->readAll());
    processBuffer(m_keypointBuffer, false);
}

// 버퍼에서 완성된 패킷 꺼내기
void KeypointClient::processBuffer(QByteArray &buffer, bool isFrame)
{
    while (true) {
        if (buffer.size() < 4) break;

        quint32 bodyLen =
            (static_cast<quint8>(buffer[0]) << 24) |
            (static_cast<quint8>(buffer[1]) << 16) |
            (static_cast<quint8>(buffer[2]) <<  8) |
            (static_cast<quint8>(buffer[3])      );

        if (bodyLen == 0 || bodyLen > 10 * 1024 * 1024) {
            buffer.clear();
            break;
        }

        if (buffer.size() < 4 + (int)bodyLen) break;

        QByteArray body = buffer.mid(4, bodyLen);
        buffer.remove(0, 4 + bodyLen);

        if (isFrame) {
            QImage img;
            img.loadFromData(body, "JPEG");
            if (!img.isNull())
                emit frameReady(img);
        } else {
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject())
                emit keypointReady(doc.object());
        }
    }
}

// 데이터 소켓(frame/keypoint) 끊김
// abort()가 disconnected를 재발생시키는 재진입을 플래그로 방지
void KeypointClient::onDataSocketDisconnected()
{
    if (m_intentionalDisconnect)
        return;

    // 이미 처리 중이면 무시 (abort()에 의한 재진입 방지)
    if (m_handlingDisconnect)
        return;
    m_handlingDisconnect = true;

    if (m_connected) {
        m_connected = false;
        qDebug() << "[KP] keypoint_server 연결 끊김 — 재연결 대기 중...";
        emit connectionChanged(false);
    }

    m_frameConnected    = false;
    m_keypointConnected = false;

    // blockSignals로 abort() 호출 시 추가 disconnected 시그널 억제
    m_frameSocket->blockSignals(true);
    m_keypointSocket->blockSignals(true);
    m_frameSocket->abort();
    m_keypointSocket->abort();
    m_frameSocket->blockSignals(false);
    m_keypointSocket->blockSignals(false);

    m_handlingDisconnect = false;

    // 타이머가 비활성이고 아직 연결 안 된 상태일 때만 시작
    if (!m_reconnectTimer->isActive() && !m_connected)
        m_reconnectTimer->start();
}

// controlSocket 끊김 — 데이터 소켓에 영향 없이 독립 재연결
void KeypointClient::onControlDisconnected()
{
    if (m_intentionalDisconnect)
        return;

    qDebug() << "[KP] 제어 소켓 끊김 — 재연결 시도...";
    m_controlSocket->abort();

    // 데이터 소켓이 살아있으면 제어 소켓만 재연결
    if (m_connected) {
        QTimer::singleShot(1000, this, [this] {
            if (!m_intentionalDisconnect &&
                m_controlSocket->state() == QAbstractSocket::UnconnectedState)
                m_controlSocket->connectToHost(m_host, CONTROL_PORT);
        });
    }
}

// 재연결 시도
void KeypointClient::tryReconnect()
{
    if (m_intentionalDisconnect)
        return;

    qDebug() << "[KP] 재연결 시도 (" << m_host << ")...";

    m_frameBuffer.clear();
    m_keypointBuffer.clear();
    m_frameConnected    = false;
    m_keypointConnected = false;

    auto abortSilent = [](QTcpSocket *s) {
        if (s->state() != QAbstractSocket::UnconnectedState) {
            s->blockSignals(true);
            s->abort();
            s->blockSignals(false);
        }
    };

    abortSilent(m_frameSocket);
    abortSilent(m_keypointSocket);
    abortSilent(m_controlSocket);

    m_frameSocket->connectToHost(m_host, FRAME_PORT);
    m_keypointSocket->connectToHost(m_host, KEYPOINT_PORT);
    m_controlSocket->connectToHost(m_host, CONTROL_PORT);

    // 연결 실패 시 onDataSocketDisconnected()가 타이머를 시작, 연결 성공 시 checkAndEmitConnected()가 타이머를 멈춤.
    // 여기서 start()하면 연결 성공 후에도 타이머가 재시작되어 tryReconnect()가 다시 호출되어 정상 연결을 끊는 문제 발생.
}
