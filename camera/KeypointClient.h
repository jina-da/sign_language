#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QImage>
#include <QJsonObject>
#include <QTimer>

/**
 * KeypointClient
 *
 * keypoint_server.py 와 통신하는 클라이언트.
 * 포트 7000: JPEG 프레임 수신 → frameReady 시그널
 * 포트 7001: 키포인트 JSON 수신 → keypointReady 시그널
 *
 * 패킷 구조: [4byte big-endian 길이] + [data]
 */
class KeypointClient : public QObject
{
    Q_OBJECT

public:
    explicit KeypointClient(QObject *parent = nullptr);

    void connectToServer(const QString &host = "127.0.0.1");
    void disconnectFromServer();
    bool isConnected() const;

    // 우세손 정보를 keypoint_server에 전달
    void setDominantHand(bool isDominantLeft);

signals:
    // 카메라 프레임 수신 (StudyWidget::onCameraFrame으로 연결)
    void frameReady(const QImage &frame);

    // 키포인트 JSON 수신 (StudyWidget::onKeypointFrame으로 연결)
    void keypointReady(const QJsonObject &keypoint);

    // 연결 상태 변경
    void connectionChanged(bool connected);

private slots:
    void onFrameReadyRead();
    void onKeypointReadyRead();
    void onDataSocketDisconnected();   // frame/keypoint 소켓 전용
    void onControlDisconnected();      // control 소켓 전용
    void tryReconnect();

private:
    void processBuffer(QByteArray &buffer, bool isFrame);
    void checkAndEmitConnected();      // 두 데이터 소켓 모두 연결됐는지 확인

    QTcpSocket *m_frameSocket;
    QTcpSocket *m_keypointSocket;
    QTcpSocket *m_controlSocket;
    QTimer     *m_reconnectTimer;

    QByteArray m_frameBuffer;
    QByteArray m_keypointBuffer;

    QString  m_host;
    bool     m_connected             = false;
    bool     m_frameConnected        = false;
    bool     m_keypointConnected     = false;
    bool     m_intentionalDisconnect = false;
    bool     m_handlingDisconnect    = false;  // abort() 재진입 방지
    bool     m_hasPendingDominant    = false;
    bool     m_pendingDominantLeft   = false;

    void sendDominantHand();

    static constexpr int FRAME_PORT     = 7000;
    static constexpr int KEYPOINT_PORT  = 7001;
    static constexpr int CONTROL_PORT   = 7002;
    static constexpr int RECONNECT_MS   = 3000;
};
