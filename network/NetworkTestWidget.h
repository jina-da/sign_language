#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QLineEdit>
#include <QGroupBox>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QFont>

#include "TcpClient.h"

/**
 * NetworkTestWidget — 1단계 통신 테스트 UI
 *
 * 테스트 방법:
 *   1. 빌드 후 실행
 *   2. 서버 주소/포트 입력 후 "연결" 버튼
 *   3. 로그인·로그아웃·단어목록 버튼으로 메시지 전송 테스트
 *   4. 하단 로그창에서 송수신 내용 확인
 */
class NetworkTestWidget : public QWidget
{
    Q_OBJECT

public:
    explicit NetworkTestWidget(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_client(new TcpClient(this))
    {
        setWindowTitle("SignLearn — 1단계 통신 테스트");
        setMinimumSize(640, 560);

        auto *root = new QVBoxLayout(this);
        root->setSpacing(10);
        root->setContentsMargins(12, 12, 12, 12);

        // ── ① 연결 상태 배너 ───────────────────────────
        m_statusLabel = new QLabel("  서버 연결 끊김", this);
        m_statusLabel->setAlignment(Qt::AlignCenter);
        m_statusLabel->setFixedHeight(36);
        m_statusLabel->setStyleSheet(
            "background:#ffdddd; color:#c0392b; font-weight:bold;"
            "font-size:14px; border-radius:6px;");
        root->addWidget(m_statusLabel);

        // ── ② 서버 연결 그룹 ───────────────────────────
        auto *connGroup  = new QGroupBox("서버 연결", this);
        auto *connLayout = new QHBoxLayout(connGroup);

        connLayout->addWidget(new QLabel("주소:"));
        m_hostEdit = new QLineEdit("127.0.0.1", this);
        m_hostEdit->setFixedWidth(150);
        connLayout->addWidget(m_hostEdit);

        connLayout->addWidget(new QLabel("포트:"));
        m_portEdit = new QLineEdit("9000", this);
        m_portEdit->setFixedWidth(60);
        connLayout->addWidget(m_portEdit);

        connLayout->addSpacing(10);

        m_connectBtn = new QPushButton("연결", this);
        m_connectBtn->setFixedWidth(80);
        m_connectBtn->setStyleSheet(
            "QPushButton{background:#2ecc71;color:white;font-weight:bold;"
            "border-radius:4px;padding:6px;}"
            "QPushButton:hover{background:#27ae60;}");
        connLayout->addWidget(m_connectBtn);

        m_disconnectBtn = new QPushButton("연결 끊기", this);
        m_disconnectBtn->setFixedWidth(90);
        m_disconnectBtn->setStyleSheet(
            "QPushButton{background:#e74c3c;color:white;font-weight:bold;"
            "border-radius:4px;padding:6px;}"
            "QPushButton:hover{background:#c0392b;}");
        connLayout->addWidget(m_disconnectBtn);
        connLayout->addStretch();

        root->addWidget(connGroup);

        // ── ③ 메시지 전송 그룹 ─────────────────────────
        auto *sendGroup = new QGroupBox("메시지 전송 테스트", this);
        auto *sendGrid  = new QGridLayout(sendGroup);
        sendGrid->setSpacing(8);

        auto makeBtn = [&](const QString &label, const QString &bg) -> QPushButton* {
            auto *btn = new QPushButton(label, this);
            btn->setStyleSheet(QString(
                                   "QPushButton{background:%1;color:white;font-weight:bold;"
                                   "border-radius:4px;padding:8px;}"
                                   "QPushButton:hover{background:%1;opacity:0.85;}").arg(bg));
            return btn;
        };

        auto *loginBtn    = makeBtn("로그인 전송",    "#3498db");
        auto *logoutBtn   = makeBtn("로그아웃 전송",  "#95a5a6");
        auto *wordBtn     = makeBtn("단어 목록 요청", "#9b59b6");
        auto *progressBtn = makeBtn("진도 조회",      "#1abc9c");

        sendGrid->addWidget(loginBtn,    0, 0);
        sendGrid->addWidget(logoutBtn,   0, 1);
        sendGrid->addWidget(wordBtn,     0, 2);
        sendGrid->addWidget(progressBtn, 0, 3);

        root->addWidget(sendGroup);

        // ── ④ 로그 창 ──────────────────────────────────
        auto *logGroup  = new QGroupBox("통신 로그", this);
        auto *logLayout = new QVBoxLayout(logGroup);

        auto *clearBtn = new QPushButton("로그 지우기", this);
        clearBtn->setFixedWidth(90);
        clearBtn->setStyleSheet("padding:4px 8px;");
        logLayout->addWidget(clearBtn, 0, Qt::AlignRight);

        m_log = new QTextEdit(this);
        m_log->setReadOnly(true);
        QFont mono("Courier New", 9);
        mono.setStyleHint(QFont::Monospace);
        m_log->setFont(mono);
        m_log->setStyleSheet("background:#1e1e1e; color:#d4d4d4;");
        logLayout->addWidget(m_log);

        root->addWidget(logGroup, 1);   // 로그창이 남은 공간 차지

        // ── 버튼 시그널 연결 ───────────────────────────
        connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
            QString host = m_hostEdit->text().trimmed();
            quint16 port = static_cast<quint16>(m_portEdit->text().toUShort());
            m_client->connectToServer(host, port);
            addLog("연결 시도: " + host + ":" + QString::number(port), "#61afef");
        });

        connect(m_disconnectBtn, &QPushButton::clicked, this, [this]() {
            m_client->disconnectFromServer();
            addLog("연결 끊기 요청", "#e06c75");
        });

        connect(loginBtn, &QPushButton::clicked, this, [this]() {
            bool ok = m_client->sendMessage({
                {"type",          "login"},
                {"username",      "test_user"},
                {"password_hash", "abc123def456"}
            });
            addLog(ok ? "[전송] login {username:test_user}"
                      : "[오류] 전송 실패 — 서버에 연결되지 않음",
                   ok ? "#98c379" : "#e06c75");
        });

        connect(logoutBtn, &QPushButton::clicked, this, [this]() {
            bool ok = m_client->sendMessage({{"type", "logout"}});
            addLog(ok ? "[전송] logout"
                      : "[오류] 전송 실패 — 서버에 연결되지 않음",
                   ok ? "#98c379" : "#e06c75");
        });

        connect(wordBtn, &QPushButton::clicked, this, [this]() {
            bool ok = m_client->sendMessage({
                {"type", "word_list"},
                {"mode", "study"}
            });
            addLog(ok ? "[전송] word_list {mode:study}"
                      : "[오류] 전송 실패 — 서버에 연결되지 않음",
                   ok ? "#98c379" : "#e06c75");
        });

        connect(progressBtn, &QPushButton::clicked, this, [this]() {
            bool ok = m_client->sendMessage({{"type", "progress"}});
            addLog(ok ? "[전송] progress"
                      : "[오류] 전송 실패 — 서버에 연결되지 않음",
                   ok ? "#98c379" : "#e06c75");
        });

        connect(clearBtn, &QPushButton::clicked, m_log, &QTextEdit::clear);

        // ── TcpClient 시그널 연결 ──────────────────────
        connect(m_client, &TcpClient::connectionChanged,
                this, [this](bool connected) {
                    if (connected) {
                        m_statusLabel->setText(
                            "  서버 연결됨  (" + m_hostEdit->text() + ":" + m_portEdit->text() + ")");
                        m_statusLabel->setStyleSheet(
                            "background:#d5f5e3; color:#1e8449; font-weight:bold;"
                            "font-size:14px; border-radius:6px;");
                        addLog("서버 연결 성공!", "#98c379");
                    } else {
                        m_statusLabel->setText("  서버 연결 끊김  — 3초 후 자동 재연결");
                        m_statusLabel->setStyleSheet(
                            "background:#ffdddd; color:#c0392b; font-weight:bold;"
                            "font-size:14px; border-radius:6px;");
                        addLog("서버 연결 끊김", "#e06c75");
                    }
                });

        connect(m_client, &TcpClient::reconnectAttempt,
                this, [this](int attempt) {
                    addLog(QString("재연결 시도 #%1 ...").arg(attempt), "#e5c07b");
                });

        connect(m_client, &TcpClient::messageReceived,
                this, [this](const QJsonObject &msg) {
                    QString type = msg["type"].toString();

                    if (type == "login_result") {
                        bool ok = msg["success"].toBool();
                        addLog(QString("[수신] login_result — %1  user_id=%2")
                                   .arg(ok ? "로그인 성공" : "로그인 실패")
                                   .arg(msg["user_id"].toInt()),
                               ok ? "#98c379" : "#e06c75");

                    } else if (type == "keypoint_result") {
                        addLog(QString("[수신] keypoint_result — verdict=%1  confidence=%2%  elapsed=%3ms")
                                   .arg(msg["verdict"].toString())
                                   .arg(msg["confidence"].toDouble())
                                   .arg(msg["elapsed_ms"].toInt()), "#56b6c2");

                    } else if (type == "word_list_result") {
                        int cnt = msg["words"].toArray().size();
                        addLog(QString("[수신] word_list_result — 단어 %1개").arg(cnt), "#56b6c2");

                    } else if (type == "error") {
                        addLog("[수신] error — " + msg["message"].toString(), "#e06c75");

                    } else {
                        addLog("[수신] type=" + type, "#abb2bf");
                    }
                });

        // 시작 안내 메시지
        addLog("통신 테스트 준비 완료. 서버 주소 입력 후 [연결] 버튼을 누르세요.", "#abb2bf");
        addLog("서버가 없어도 됩니다 — 자동 재연결(3초)이 동작하는지 확인하세요.", "#abb2bf");
    }

private:
    // 로그창에 컬러 한 줄 추가
    void addLog(const QString &text, const QString &color = "#d4d4d4")
    {
        QString time = QDateTime::currentDateTime().toString("hh:mm:ss.zzz");
        m_log->append(
            QString("<span style='color:#5c6370;'>[%1]</span> "
                    "<span style='color:%2;'>%3</span>")
                .arg(time, color, text.toHtmlEscaped()));
    }

    TcpClient   *m_client;
    QLabel      *m_statusLabel;
    QLineEdit   *m_hostEdit;
    QLineEdit   *m_portEdit;
    QPushButton *m_connectBtn;
    QPushButton *m_disconnectBtn;
    QTextEdit   *m_log;
};