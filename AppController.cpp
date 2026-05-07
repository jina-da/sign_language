#include "AppController.h"
#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>

AppController::AppController(QObject *parent)
    : QObject(parent)
    , m_client(new TcpClient(this))
{
    qDebug() << "[App] Creating LoginWidget...";
    m_loginWidget = new LoginWidget;
    qDebug() << "[App] LoginWidget OK";

    qDebug() << "[App] Creating MainWindow...";
    m_mainWindow = new MainWindow;
    qDebug() << "[App] MainWindow OK";

    // ── 네트워크 ──────────────────────────────────────
    connect(m_client, &TcpClient::connectionChanged,
            this,     &AppController::onConnectionChanged);
    connect(m_client, &TcpClient::messageReceived,
            this,     &AppController::onMessageReceived);

    // ── 로그인 ────────────────────────────────────────
    connect(m_loginWidget, &LoginWidget::loginRequested,
            this,          &AppController::onLoginRequested);
    connect(m_loginWidget, &LoginWidget::registerRequested,
            this, []{});

    // ── 디버그: 버튼 클릭 → 바로 메인화면 ───────────
    connect(m_loginWidget, &LoginWidget::debugLogin,
            this, [this]{
                qDebug() << "[Debug] 디버그 로그인 - 메인화면으로 이동";
                m_mainWindow->setUserInfo("디버그", false);
                m_mainWindow->setTodayProgress(0, 15);
                m_mainWindow->resize(1024, 680);
                m_mainWindow->show();
                m_loginWidget->hide();
            });

    // ── 로그아웃 ──────────────────────────────────────
    connect(m_mainWindow, &MainWindow::logoutRequested, this, [this]{
        m_client->sendMessage({{"type", "logout"}});
        m_mainWindow->hide();
        m_loginWidget->show();
    });

    // ── 학습 모드: 키포인트 전송 ──────────────────────
    connect(m_mainWindow->studyWidget(), &StudyWidget::keypointReady,
            this, [this](int wordId, bool isDominantLeft,
                         const QJsonArray &keypoints) {
        m_client->sendMessage({
            {"type",             "keypoint"},
            {"word_id",          wordId},
            {"is_dominant_left", isDominantLeft},
            {"keypoint",         keypoints}
        });
        qDebug() << "[App] keypoint 전송: word_id=" << wordId
                 << "frames=" << keypoints.size();
    });

    // ── 학습 완료 ─────────────────────────────────────
    connect(m_mainWindow->studyWidget(), &StudyWidget::studyFinished,
            this, [this]{
        qDebug() << "[App] 학습 완료";
        // TODO: 오늘의 테스트 화면으로 전환
    });

    qDebug() << "[App] constructor done";
}

void AppController::start()
{
    qDebug() << "[App] start()";
    m_loginWidget->resize(1024, 680);
    m_loginWidget->show();
    m_client->connectToServer("127.0.0.1", 9000);
}

void AppController::onConnectionChanged(bool connected)
{
    m_loginWidget->setConnected(connected);
    m_mainWindow->setConnected(connected);
}

void AppController::onLoginRequested(const QString &username,
                                     const QString &passwordHash)
{
    if (!m_client->isConnected()) {
        m_loginWidget->showError("서버에 연결되지 않았습니다.\n잠시 후 다시 시도해 주세요.");
        return;
    }
    m_client->sendMessage({
        {"type",          "login"},
        {"username",      username},
        {"password_hash", passwordHash}
    });
}

void AppController::onMessageReceived(const QJsonObject &msg)
{
    QString type = msg["type"].toString();
    qDebug() << "[App] 수신:" << type;

    if (type == "login_result") {
        if (msg["success"].toBool()) {
            m_mainWindow->setUserInfo(
                msg["username"].toString(),
                msg["is_dominant_left"].toBool());
            m_mainWindow->setTodayProgress(0, 15);
            m_mainWindow->resize(1024, 680);
            m_mainWindow->show();
            m_loginWidget->hide();

            // 로그인 성공 → 단어 목록 요청
            m_client->sendMessage({{"type", "word_list"}, {"mode", "study"}});
        } else {
            m_loginWidget->showError("아이디 또는 비밀번호가 올바르지 않습니다.");
        }
    }

    else if (type == "word_list_result") {
        // 단어 목록을 StudyWidget에 전달
        QList<StudyWidget::WordInfo> words;
        for (const auto &v : msg["words"].toArray()) {
            QJsonObject w = v.toObject();
            words.append({
                w["id"].toInt(),
                w["word"].toString(),
                w["category"].toString(),
                w["difficulty"].toInt()
            });
        }
        m_mainWindow->studyWidget()->setWordList(words);
        qDebug() << "[App] 단어 목록 수신:" << words.size() << "개";
    }

    else if (type == "keypoint_result") {
        // 인식 결과를 StudyWidget에 전달
        m_mainWindow->studyWidget()->showResult(
            msg["verdict"].toString(),
            msg["confidence"].toDouble(),
            msg["predicted_word_id"].toInt());

        qDebug() << "[App] keypoint_result:"
                 << msg["verdict"].toString()
                 << msg["confidence"].toDouble() << "%";
    }

    else if (type == "progress_result") {
        // TODO: 진도 정보 파싱
    }

    else if (type == "logout_ack") {
        qDebug() << "[App] 로그아웃 완료";
    }

    else if (type == "error") {
        QString err = msg["message"].toString();
        qWarning() << "[App] 서버 오류:" << err;
        m_loginWidget->showError("오류: " + err);
    }
}
