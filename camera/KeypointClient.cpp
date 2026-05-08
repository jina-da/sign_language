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

    connect(m_frameSocket,    &QTcpSocket::readyRead,
            this,             &KeypointClient::onFrameReadyRead);
    connect(m_keypointSocket, &QTcpSocket::readyRead,
            this,             &KeypointClient::onKeypointReadyRead);
    connect(m_frameSocket,    &QTcpSocket::disconnected,
            this,             &KeypointClient::onDisconnected);
    connect(m_reconnectTimer, &QTimer::timeout,
            this,             &KeypointClient::tryReconnect);
}

void KeypointClient::connectToServer(const QString &host)
{
    m_host = host;
    m_frameSocket->connectToHost(host, FRAME_PORT);
    m_keypointSocket->connectToHost(host, KEYPOINT_PORT);
    m_controlSocket->connectToHost(host, CONTROL_PORT);

    // 연결 성공 시그널
    connect(m_frameSocket, &QTcpSocket::connected, this, [this]{
        if (!m_connected) {
            m_connected = true;
            qDebug() << "[KP] keypoint_server 연결 성공";
            emit connectionChanged(true);
        }
    });

    // 제어 소켓 연결 완료 시 대기 중인 우세손 설정 전송
    connect(m_controlSocket, &QTcpSocket::connected, this, [this]{
        qDebug() << "[KP] 제어 소켓 연결 완료";
        if (m_hasPendingDominant)
            sendDominantHand();
    });
}

void KeypointClient::disconnectFromServer()
{
    m_reconnectTimer->stop();
    m_frameSocket->disconnectFromHost();
    m_keypointSocket->disconnectFromHost();
    m_controlSocket->disconnectFromHost();
}

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

// ─────────────────────────────────────────────────────────────
// 프레임 수신 처리
// ─────────────────────────────────────────────────────────────
void KeypointClient::onFrameReadyRead()
{
    m_frameBuffer.append(m_frameSocket->readAll());
    processBuffer(m_frameBuffer, true);
}

// ─────────────────────────────────────────────────────────────
// 키포인트 수신 처리
// ─────────────────────────────────────────────────────────────
void KeypointClient::onKeypointReadyRead()
{
    m_keypointBuffer.append(m_keypointSocket->readAll());
    processBuffer(m_keypointBuffer, false);
}

// ─────────────────────────────────────────────────────────────
// 버퍼에서 완성된 패킷 꺼내기
// ─────────────────────────────────────────────────────────────
void KeypointClient::processBuffer(QByteArray &buffer, bool isFrame)
{
    while (true) {
        // 헤더 4바이트 확인
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
            // JPEG → QImage 변환
            QImage img;
            img.loadFromData(body, "JPEG");
            if (!img.isNull())
                emit frameReady(img);
        } else {
            // JSON 파싱
            QJsonDocument doc = QJsonDocument::fromJson(body);
            if (doc.isObject())
                emit keypointReady(doc.object());
        }
    }
}

// ─────────────────────────────────────────────────────────────
// 연결 끊김 → 재연결 시도
// ─────────────────────────────────────────────────────────────
void KeypointClient::onDisconnected()
{
    if (m_connected) {
        m_connected = false;
        qDebug() << "[KP] keypoint_server 연결 끊김, 재연결 시도";
        emit connectionChanged(false);
    }
    m_reconnectTimer->start();
}

void KeypointClient::tryReconnect()
{
    qDebug() << "[KP] 재연결 시도...";
    m_frameBuffer.clear();
    m_keypointBuffer.clear();
    m_frameSocket->connectToHost(m_host, FRAME_PORT);
    m_keypointSocket->connectToHost(m_host, KEYPOINT_PORT);
}
