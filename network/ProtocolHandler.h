#pragma once

#include <QObject>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>

class ProtocolHandler : public QObject
{
    Q_OBJECT

public:
    explicit ProtocolHandler(QObject *parent = nullptr);

    // 송신
    static QByteArray pack(const QJsonObject &json);

    // 수신
    void feed(const QByteArray &data);
    void reset();

signals:
    void messageReady(const QJsonObject &msg);
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
