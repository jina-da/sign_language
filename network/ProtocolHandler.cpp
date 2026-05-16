#include "ProtocolHandler.h"

#include <QDataStream>
#include <QJsonParseError>

// 생성자
ProtocolHandler::ProtocolHandler(QObject *parent)
    : QObject(parent)
{
}

QByteArray ProtocolHandler::pack(const QJsonObject &json)
{
    // JSON → UTF-8 바이트
    QByteArray body = QJsonDocument(json).toJson(QJsonDocument::Compact);

    // body 길이를 big-endian 4바이트로 변환
    QByteArray header(HEADER_SIZE, 0);
    quint32 bodyLen = static_cast<quint32>(body.size());
    // big-endian으로 직접 채우기 (QDataStream 안 써도 됨)
    header[0] = static_cast<char>((bodyLen >> 24) & 0xFF);
    header[1] = static_cast<char>((bodyLen >> 16) & 0xFF);
    header[2] = static_cast<char>((bodyLen >>  8) & 0xFF);
    header[3] = static_cast<char>( bodyLen        & 0xFF);

    return header + body;
}

void ProtocolHandler::feed(const QByteArray &data)
{
    // 새로 받은 바이트를 내부 버퍼에 쌓는다.
    m_buffer.append(data);

    // 완성된 메시지가 있는 한 계속 꺼낸다.
    while (true) {

        // 헤더가 아직 다 안 쌓인 경우
        if (m_expectedBodySize < 0) {
            if (m_buffer.size() < HEADER_SIZE) {
                break; // 헤더 4바이트가 모일 때까지 기다림
            }

            // 헤더 4바이트를 big-endian으로 읽어 body 크기 계산
            quint32 bodyLen =
                (static_cast<quint8>(m_buffer[0]) << 24) |
                (static_cast<quint8>(m_buffer[1]) << 16) |
                (static_cast<quint8>(m_buffer[2]) <<  8) |
                (static_cast<quint8>(m_buffer[3])      );

            // 이상한 크기이면 (서버가 잘못된 데이터를 보낸 경우) 버퍼 버리고 종료
            if (bodyLen == 0 || bodyLen > static_cast<quint32>(MAX_BODY_SIZE)) {
                m_buffer.clear();
                m_expectedBodySize = -1;
                break;
            }

            m_expectedBodySize = static_cast<qint32>(bodyLen);
            // 헤더 4바이트는 버퍼에서 제거한다
            m_buffer.remove(0, HEADER_SIZE);
        }

        // body가 아직 다 안 쌓인 경우
        if (m_buffer.size() < m_expectedBodySize) {
            break; // body가 모일 때까지 기다림
        }

        // body 완성! JSON 파싱 후 시그널 발생
        QByteArray body = m_buffer.left(m_expectedBodySize);
        m_buffer.remove(0, m_expectedBodySize);
        m_expectedBodySize = -1; // 다음 메시지를 위해 초기화

        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            emit parseError(body);
            continue;
        }

        emit messageReady(doc.object());
        // 버퍼에 다음 메시지가 남아 있을 수 있으니 루프 계속
    }
}


// 내부 버퍼 초기화 - 재연결할 때 잔여 데이터 제거용
void ProtocolHandler::reset()
{
    m_buffer.clear();
    m_expectedBodySize = -1;
}
