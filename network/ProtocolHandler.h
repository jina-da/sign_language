#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>

/**
 * ProtocolHandler
 *
 * 역할: 운용 서버와의 통신 규약(패킷 포맷)을 처리한다.
 *
 * 패킷 구조 (변경 금지 — 서버와 합의된 규약):
 * ┌─────────────────────────────────────┐
 * │  4 bytes : JSON body 길이           │  big-endian uint32
 * ├─────────────────────────────────────┤
 * │  N bytes : JSON UTF-8 body          │
 * └─────────────────────────────────────┘
 *
 * 사용 예:
 *   QByteArray packet = ProtocolHandler::pack(jsonObj);
 *   // → 소켓에 write(packet)
 *
 *   // 소켓에서 데이터가 들어올 때:
 *   handler.feed(newBytes);
 *   // → 완전한 메시지가 모이면 messageReady 시그널 발생
 */
class ProtocolHandler : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolHandler(QObject *parent = nullptr);

    // ── 송신 ──────────────────────────────────────────
    /**
     * JSON 객체를 [4바이트 길이 헤더 + JSON body] 패킷으로 변환해 반환한다.
     * 반환값을 소켓에 write() 하면 된다.
     *
     * 사용 예:
     *   QByteArray packet = ProtocolHandler::pack({{"type","login"},
     *                                               {"username","user001"},
     *                                               {"password_hash","abc..."}});
     *   socket->write(packet);
     */
    static QByteArray pack(const QJsonObject &json);

    // ── 수신 ──────────────────────────────────────────
    /**
     * 소켓에서 받은 바이트를 누적한다.
     * 완전한 메시지(헤더 + body)가 모이면 messageReady를 emit한다.
     *
     * ※ TCP는 스트림이기 때문에 한 번에 패킷 하나가 딱 오지 않을 수 있다.
     *    이 메서드가 그 조각들을 모아서 완성된 메시지를 꺼낸다.
     *
     * 사용 예:
     *   // QTcpSocket::readyRead 시그널에서 호출
     *   connect(socket, &QTcpSocket::readyRead, this, [this]() {
     *       handler.feed(socket->readAll());
     *   });
     */
    void feed(const QByteArray &data);

    /**
     * 내부 버퍼를 비운다.
     * 서버 재연결 시 잔여 데이터를 제거할 때 사용.
     */
    void reset();

signals:
    /**
     * 완전한 메시지가 조립되면 발생한다.
     * @param msg 서버가 보낸 JSON 객체
     */
    void messageReady(const QJsonObject &msg);

    /**
     * JSON 파싱에 실패했을 때 발생한다. (디버깅용)
     * @param raw 파싱에 실패한 원본 바이트
     */
    void parseError(const QByteArray &raw);

private:
    // 조각들이 쌓이는 내부 버퍼
    QByteArray m_buffer;

    // 현재 읽고 있는 메시지의 body 길이.
    // -1 이면 아직 헤더(4바이트)를 다 못 받은 상태.
    qint32 m_expectedBodySize = -1;

    static constexpr int HEADER_SIZE = 4;               // 헤더 고정 4바이트
    static constexpr int MAX_BODY_SIZE = 10 * 1024 * 1024; // 최대 10 MB
};
