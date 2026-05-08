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

    qDebug() << "[App] Creating RegisterWidget...";
    m_registerWidget = new RegisterWidget;
    qDebug() << "[App] RegisterWidget OK";

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

    // 회원가입 화면 전환
    connect(m_loginWidget, &LoginWidget::registerRequested, this, [this]{
        m_loginWidget->hide();
        m_registerWidget->resize(m_loginWidget->size());
        m_registerWidget->show();
    });

    // 회원가입 → 로그인으로 돌아가기
    connect(m_registerWidget, &RegisterWidget::backToLogin, this, [this]{
        m_registerWidget->hide();
        m_loginWidget->show();
    });

    // 회원가입 요청 처리
    connect(m_registerWidget, &RegisterWidget::registerRequested,
            this, [this](const QString &username, const QString &passwordHash,
                         bool isDeaf, bool isDominantLeft, bool keypointConsent) {
        if (!m_client->isConnected()) {
            m_registerWidget->showError("서버에 연결되지 않았습니다.");
            return;
        }
        m_lastUsername = username;
        m_client->sendMessage({
            {"type",              "REQ_REGISTER"},
            {"username",          username},
            {"password",          passwordHash},
            {"is_deaf",           isDeaf},
            {"dominant_hand",     isDominantLeft ? "left" : "right"},
            {"keypoint_consent",  keypointConsent}
        });
    });

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
        m_client->sendMessage({
            {"type",          "REQ_LOGOUT"},
            {"session_token", m_sessionToken}
        });
        m_mainWindow->hide();
        m_loginWidget->show();
    });

    // ── 학습 모드: 키포인트 전송 ──────────────────────
    connect(m_mainWindow->studyWidget(), &StudyWidget::keypointReady,
            this, [this](int wordId, bool isDominantLeft,
                         const QJsonArray &keypoints) {
        Q_UNUSED(isDominantLeft)
        m_client->sendMessage({
            {"type",              "REQ_INFER"},
            {"session_token",     m_sessionToken},
            {"word_id",           wordId},
            {"keypoint_version",  "v1"},
            {"total_frames",      keypoints.size()},
            {"frames",            keypoints}
        });
        qDebug() << "[App] REQ_INFER 전송: word_id=" << wordId
                 << "frames=" << keypoints.size();
    });

    // ── 학습 완료 ─────────────────────────────────────
    connect(m_mainWindow->studyWidget(), &StudyWidget::studyFinished,
            this, [this]{
        qDebug() << "[App] 학습 완료 → 홈 탭으로 이동";
        // 홈 탭(인덱스 0)으로 전환
        // MainWindow의 switchTab은 private이므로 navHome 버튼을 클릭
        // TODO: 오늘의 테스트 화면으로 전환 (추후 구현)
    });

    qDebug() << "[App] constructor done";
}

void AppController::start()
{
    qDebug() << "[App] start()";
    m_loginWidget->resize(1024, 680);
    m_loginWidget->show();
    m_client->connectToServer("10.10.10.114", 9000);
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
    m_lastUsername = username;  // 로그인 성공 후 화면에 표시하기 위해 보존
    m_client->sendMessage({
        {"type",     "REQ_LOGIN"},
        {"username", username},
        {"password", passwordHash}
    });
}

void AppController::onMessageReceived(const QJsonObject &msg)
{
    QString type = msg["type"].toString();
    qDebug() << "[App] 수신:" << type;

    // ── 102: 로그인 응답 ──────────────────────────────
    if (type == "RES_LOGIN") {
        qDebug() << "[App] RES_LOGIN 전체:" << msg;
        if (msg["status"].toString() == "ok") {
            m_sessionToken = msg["session_token"].toString();
            bool isDominantLeft = (msg["dominant_hand"].toString() == "left");
            qDebug() << "[App] dominant_hand 필드:" << msg["dominant_hand"].toString()
                     << "→ 왼손잡이:" << isDominantLeft;

            // 우세손 정보를 keypoint_server에 전달
            m_mainWindow->keypointClient()->setDominantHand(isDominantLeft);

            m_mainWindow->setUserInfo(m_lastUsername, isDominantLeft);
            m_mainWindow->setTodayProgress(0, 15);
            m_mainWindow->resize(1024, 680);
            m_mainWindow->show();
            m_loginWidget->hide();

            m_client->sendMessage({
                {"type",          "REQ_DAILY_WORDS"},
                {"session_token", m_sessionToken}
            });
        } else {
            m_loginWidget->showError("아이디 또는 비밀번호가 올바르지 않습니다.");
        }
    }

    // ── 106: 회원가입 응답 ────────────────────────────
    else if (type == "RES_REGISTER") {
        if (msg["status"].toString() == "ok") {
            m_registerWidget->hide();
            m_loginWidget->show();
            m_loginWidget->showError("회원가입이 완료됐습니다. 로그인해 주세요.");
        } else {
            m_registerWidget->showError("회원가입에 실패했습니다. 다시 시도해 주세요.");
        }
    }

    // ── 104: 로그아웃 응답 ────────────────────────────
    else if (type == "RES_LOGOUT") {
        m_sessionToken.clear();
        qDebug() << "[App] 로그아웃 완료";
    }

    // ── 202: 오늘의 단어 응답 ─────────────────────────
    else if (type == "RES_DAILY_WORDS") {
        QList<StudyWidget::WordInfo> words;
        for (const auto &v : msg["words"].toArray()) {
            QJsonObject w = v.toObject();
            words.append({
                w["id"].toInt(),
                w["word"].toString(),
                w.value("meaning").toString(),
                w.value("difficulty").toInt(1)
            });
        }
        m_mainWindow->studyWidget()->setWordList(words);

        // daily_goal이 포함되어 있으면 반영, 없으면 단어 수로 대체
        int goal = msg.contains("daily_goal")
            ? msg["daily_goal"].toInt()
            : words.size();
        m_mainWindow->setTodayProgress(0, goal);

        qDebug() << "[App] 단어 목록 수신:" << words.size() << "개 / 목표:" << goal;
    }

    // ── 204: 추론 결과 응답 ───────────────────────────
    else if (type == "RES_INFER") {
        // result: true=정답 / false=오답
        // accuracy: 0.0~1.0 → 퍼센트로 변환
        bool   isCorrect = msg["result"].toBool();
        double accuracy  = msg["accuracy"].toDouble() * 100.0;
        int    wordId    = msg["word_id"].toInt();

        QString verdict;
        if (isCorrect)
            verdict = "correct";
        else if (accuracy >= 50.0)
            verdict = "partial";
        else
            verdict = "incorrect";

        m_mainWindow->studyWidget()->showResult(verdict, accuracy, wordId);
        qDebug() << "[App] RES_INFER: result=" << isCorrect
                 << "accuracy=" << accuracy << "%";
    }

    // ── 206: 복습 단어 응답 ───────────────────────────
    else if (type == "RES_REVIEW_WORDS") {
        // TODO: 복습 모드 구현 시 처리
        qDebug() << "[App] RES_REVIEW_WORDS 수신";
    }

    // ── 703: 오류 응답 ────────────────────────────────
    else if (type == "RES_ERROR") {
        QString err = msg["message"].toString();
        qWarning() << "[App] 서버 오류:" << msg["error_code"].toInt() << err;
        m_loginWidget->showError("오류: " + err);
    }

    else {
        qDebug() << "[App] 미처리 메시지 type:" << type;
    }
}


