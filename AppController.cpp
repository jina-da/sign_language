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
        // 서버에 로그아웃 요청 (응답 여부와 무관하게 클라이언트는 즉시 초기화)
        if (!m_sessionToken.isEmpty()) {
            m_client->sendMessage({
                {"type",          "REQ_LOGOUT"},
                {"session_token", m_sessionToken}
            });
        }
        // 즉시 세션 초기화 (서버 응답 기다리지 않음)
        m_sessionToken.clear();
        m_lastUsername.clear();

        // 화면 초기화 — 항상 홈 탭(0)에서 시작
        m_mainWindow->switchToHome();
        m_loginWidget->reset();   // 비밀번호 클리어, 에러 숨김
        m_loginWidget->setConnected(m_client->isConnected());
        m_mainWindow->hide();
        m_loginWidget->show();
        qDebug() << "[App] 로그아웃 완료";
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

        // ── 디버그: 전송 키포인트 요약 로그 ──────────
        qDebug() << "[App] REQ_INFER 전송: word_id=" << wordId
                 << " frames=" << keypoints.size();
        if (!keypoints.isEmpty()) {
            auto logFrame = [](const QJsonObject &f, const QString &label) {
                QJsonArray lh = f["left_hand"].toArray();
                QJsonArray rh = f["right_hand"].toArray();
                auto wrist = [](const QJsonArray &h) -> QString {
                    if (h.isEmpty()) return "(없음)";
                    QJsonArray w = h[0].toArray();
                    return QString("(%1, %2)")
                        .arg(w[0].toDouble(), 0, 'f', 3)
                        .arg(w[1].toDouble(), 0, 'f', 3);
                };
                qDebug() << "  " << label
                         << "  L손목:" << wrist(lh)
                         << "  R손목:" << wrist(rh);
            };
            logFrame(keypoints.first().toObject(), "첫프레임 ");
            logFrame(keypoints.last().toObject(),  "끝프레임 ");
        }
        // ─────────────────────────────────────────────
    });

    // ── 학습 완료 → 홈 탭 ────────────────────────────
    connect(m_mainWindow->studyWidget(), &StudyWidget::studyFinished,
            this, [this]{
        qDebug() << "[App] 학습 완료 → 홈 탭으로 이동";
        m_mainWindow->switchToHome();
    });

    // ── 학습 완료 → 테스트 시작 ──────────────────────
    connect(m_mainWindow->studyWidget(), &StudyWidget::testRequested,
            this, [this](const QList<StudyWidget::WordInfo> &words){
        qDebug() << "[App] 테스트 시작, 단어 수:" << words.size();

        // StudyWidget::WordInfo → TestWidget::WordInfo 변환
        QList<TestWidget::WordInfo> testWords;
        for (const auto &w : words)
            testWords.append({w.id, w.word, w.meaning, w.videoCdnUrl});

        m_mainWindow->testWidget()->setWordList(testWords);
        m_mainWindow->showTestTab();
    });

    // ── 테스트 완료/종료 → 홈 탭 ─────────────────────
    connect(m_mainWindow->testWidget(), &TestWidget::testFinished,
            this, [this](int correct, int total){
        qDebug() << "[App] 테스트 완료:" << correct << "/" << total;
        // TODO: 서버에 테스트 결과 전송 (추후 구현)
    });
    connect(m_mainWindow->testWidget(), &TestWidget::testAborted,
            this, [this]{
        qDebug() << "[App] 테스트 중단 → 홈 탭";
        m_mainWindow->switchToHome();
    });

    // ── 테스트 키포인트 전송 ──────────────────────────
    connect(m_mainWindow->testWidget(), &TestWidget::keypointReady,
            this, [this](int wordId, bool isDominantLeft,
                         const QJsonArray &keypoints) {
        Q_UNUSED(isDominantLeft)
        m_client->sendMessage({
            {"type",             "REQ_INFER"},
            {"session_token",    m_sessionToken},
            {"word_id",          wordId},
            {"keypoint_version", "v1"},
            {"total_frames",     keypoints.size()},
            {"frames",           keypoints}
        });
        qDebug() << "[App] 테스트 REQ_INFER 전송: word_id=" << wordId
                 << " frames=" << keypoints.size();
        if (!keypoints.isEmpty()) {
            auto wrist = [](const QJsonArray &h) -> QString {
                if (h.isEmpty()) return "(없음)";
                QJsonArray w = h[0].toArray();
                return QString("(%1,%2)").arg(w[0].toDouble(),0,'f',3).arg(w[1].toDouble(),0,'f',3);
            };
            auto f0 = keypoints.first().toObject();
            auto fn = keypoints.last().toObject();
            qDebug() << "  첫프레임 L:" << wrist(f0["left_hand"].toArray()) << " R:" << wrist(f0["right_hand"].toArray());
            qDebug() << "  끝프레임 L:" << wrist(fn["left_hand"].toArray()) << " R:" << wrist(fn["right_hand"].toArray());
        }
    });

    // ── 복습 완료 → 홈 탭 ────────────────────────────
    connect(m_mainWindow->reviewWidget(), &ReviewWidget::reviewFinished,
            this, [this]{
        qDebug() << "[App] 복습 완료 → 홈 탭으로 이동";
        m_mainWindow->switchToHome();
    });

    // ── 복습 키포인트 전송 ────────────────────────────
    connect(m_mainWindow->reviewWidget(), &ReviewWidget::keypointReady,
            this, [this](int wordId, bool isDominantLeft,
                         const QJsonArray &keypoints) {
        Q_UNUSED(isDominantLeft)
        m_client->sendMessage({
            {"type",             "REQ_INFER"},
            {"session_token",    m_sessionToken},
            {"word_id",          wordId},
            {"keypoint_version", "v1"},
            {"total_frames",     keypoints.size()},
            {"frames",           keypoints}
        });
        qDebug() << "[App] 복습 REQ_INFER 전송: word_id=" << wordId
                 << " frames=" << keypoints.size();
        if (!keypoints.isEmpty()) {
            auto wrist = [](const QJsonArray &h) -> QString {
                if (h.isEmpty()) return "(없음)";
                QJsonArray w = h[0].toArray();
                return QString("(%1,%2)").arg(w[0].toDouble(),0,'f',3).arg(w[1].toDouble(),0,'f',3);
            };
            auto f0 = keypoints.first().toObject();
            auto fn = keypoints.last().toObject();
            qDebug() << "  첫프레임 L:" << wrist(f0["left_hand"].toArray()) << " R:" << wrist(f0["right_hand"].toArray());
            qDebug() << "  끝프레임 L:" << wrist(fn["left_hand"].toArray()) << " R:" << wrist(fn["right_hand"].toArray());
        }
    });

    // ── 홈/네비 복습 버튼 클릭 → 즉시 화면 전환 후 서버 요청
    connect(m_mainWindow, &MainWindow::reviewModeRequested,
            this, [this]{
        qDebug() << "[App] 복습 화면 진입";

        // 서버 연결 여부와 무관하게 즉시 복습 화면으로 전환
        m_mainWindow->switchToReview();

        if (!m_client->isConnected() || m_sessionToken.isEmpty()) {
            // 디버그 모드 또는 오프라인: 안내 메시지 표시
            m_mainWindow->reviewWidget()->showNoWordsMessage(
                "서버에 연결되지 않았습니다.\n연결 후 다시 시도해 주세요.");
            return;
        }

        m_client->sendMessage({
            {"type",          "REQ_REVIEW_WORDS"},
            {"session_token", m_sessionToken}
        });
        qDebug() << "[App] REQ_REVIEW_WORDS 전송";
    });

    // ── userBtn → 설정 탭 전환 ──────────────────────
    connect(m_mainWindow, &MainWindow::settingsRequested, this, [this]{
        qDebug() << "[App] 설정 탭 진입";
        // 필요 시 서버에서 최신 설정값 요청 가능 (현재는 로그인 시 세팅된 값 사용)
    });

    // ── 사전: 정방향/역방향 검색 요청 ───────────────
    connect(m_mainWindow->dictWidget(), &DictWidget::forwardSearchRequested,
            this, [this](const QString &query){
        if (!m_client->isConnected()) {
            m_mainWindow->dictWidget()->showSearchError("서버에 연결되지 않았습니다.");
            return;
        }
        m_client->sendMessage({
            {"type",          "REQ_DICT_SEARCH"},
            {"session_token", m_sessionToken},
            {"query",         query},
            {"language",      "KSL"}
        });
        qDebug() << "[App] REQ_DICT_SEARCH:" << query;
    });

    connect(m_mainWindow->dictWidget(), &DictWidget::reverseSearchRequested,
            this, [this](const QJsonArray &keypoints){
        if (!m_client->isConnected()) {
            m_mainWindow->dictWidget()->showSearchError("서버에 연결되지 않았습니다.");
            return;
        }
        m_client->sendMessage({
            {"type",             "REQ_DICT_REVERSE"},
            {"session_token",    m_sessionToken},
            {"keypoint_version", "v1"},
            {"total_frames",     keypoints.size()},
            {"frames",           keypoints}
        });
        qDebug() << "[App] REQ_DICT_REVERSE, frames=" << keypoints.size();
    });

    // ── 설정: 각 항목 저장 요청 ──────────────────────
    auto *sw = m_mainWindow->settingsWidget();

    connect(sw, &SettingsWidget::dailyGoalChangeRequested,
            this, [this](int goal){
        m_client->sendMessage({
            {"type",          "REQ_SET_DAILY_GOAL"},
            {"session_token", m_sessionToken},
            {"daily_goal",    goal}
        });
    });
    connect(sw, &SettingsWidget::dominantHandChangeRequested,
            this, [this](bool isDominantLeft){
        m_client->sendMessage({
            {"type",          "REQ_SET_DOMINANT_HAND"},
            {"session_token", m_sessionToken},
            {"dominant_hand", isDominantLeft ? QString("left") : QString("right")}
        });
        // keypoint_server에도 즉시 반영
        m_mainWindow->keypointClient()->setDominantHand(isDominantLeft);
    });
    connect(sw, &SettingsWidget::deafChangeRequested,
            this, [this](bool isDeaf){
        m_client->sendMessage({
            {"type",          "REQ_SET_DEAF"},
            {"session_token", m_sessionToken},
            {"is_deaf",       isDeaf}
        });
    });
    connect(sw, &SettingsWidget::passwordChangeRequested,
            this, [this](const QString &curHash, const QString &newHash){
        m_client->sendMessage({
            {"type",             "REQ_CHANGE_PASSWORD"},
            {"session_token",    m_sessionToken},
            {"current_password", curHash},
            {"new_password",     newHash}
        });
    });
    connect(sw, &SettingsWidget::consentChangeRequested,
            this, [this](bool consent){
        m_client->sendMessage({
            {"type",               "REQ_SET_CONSENT"},
            {"session_token",      m_sessionToken},
            {"keypoint_consent",   consent}
        });
    });
    connect(sw, &SettingsWidget::withdrawRequested,
            this, [this](const QString &pwHash){
        m_client->sendMessage({
            {"type",          "REQ_WITHDRAW"},
            {"session_token", m_sessionToken},
            {"password",      pwHash}
        });
    });

    qDebug() << "[App] constructor done";
}

void AppController::cleanup()
{
    // MainWindow 소멸자가 호출되지 않는 경우(강제 종료, 크래시 등)를 대비
    if (m_mainWindow)
        m_mainWindow->stopKeypointServer();
}

void AppController::start()
{
    qDebug() << "[App] start()";
    m_loginWidget->resize(1024, 680);
    m_loginWidget->show();
    m_client->connectToServer(SERVER_HOST, SERVER_PORT);
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
            // 설정 화면 초기값 세팅
            // RES_LOGIN에 포함된 필드만 반영하고 나머지는 기본값 사용
            // 서버팀 요청사항: RES_LOGIN에 is_deaf, keypoint_consent, daily_goal 추가
            bool isDeaf      = msg.contains("is_deaf")
                               ? msg["is_deaf"].toBool()
                               : false;
            bool consent     = msg.contains("keypoint_consent")
                               ? msg["keypoint_consent"].toBool()
                               : true;
            int  dailyGoal   = msg.contains("daily_goal")
                               ? msg["daily_goal"].toInt()
                               : 15;

            m_mainWindow->settingsWidget()->setInitialValues(
                dailyGoal, isDominantLeft, isDeaf, consent);
            m_mainWindow->setTodayProgress(0, dailyGoal);

            // VideoPlayer 세션 세팅
            m_mainWindow->studyWidget()->videoPlayer()->setSession(
                SERVER_HOST, m_sessionToken);
            m_mainWindow->reviewWidget()->videoPlayer()->setSession(
                SERVER_HOST, m_sessionToken);
            m_mainWindow->testWidget()->videoPlayer()->setSession(
                SERVER_HOST, m_sessionToken);
            m_mainWindow->dictWidget()->setSession(
                SERVER_HOST, m_sessionToken);

            m_mainWindow->resize(1024, 680);
            m_mainWindow->show();
            m_loginWidget->hide();

            qDebug() << "[App] REQ_DAILY_WORDS 전송, token:"
                     << m_sessionToken.left(8) << "...";
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
    // 토큰은 요청 시점에 이미 클리어됨 — 여기서는 확인 로그만
    else if (type == "RES_LOGOUT") {
        qDebug() << "[App] RES_LOGOUT 수신 (서버 세션 종료 확인)";
    }

    // ── 202: 오늘의 단어 응답 ─────────────────────────
    else if (type == "RES_DAILY_WORDS") {
        QList<StudyWidget::WordInfo> words;
        for (const auto &v : msg["words"].toArray()) {
            QJsonObject w = v.toObject();
            QString cdnUrl = w.value("video_cdn_url").toString();
            qDebug() << "[App] word=" << w["word"].toString()
                     << "video_cdn_url=" << cdnUrl;
            words.append({
                w["id"].toInt(),
                w["word"].toString(),
                w.value("meaning").toString(),
                w.value("difficulty").toInt(1),
                cdnUrl
            });
        }

        // daily_goal: RES_LOGIN에서 이미 세팅됐으므로 덮어쓰지 않음
        // RES_DAILY_WORDS에도 포함된 경우에만 갱신 (서버 응답 우선)
        if (msg.contains("daily_goal")) {
            int goal = msg["daily_goal"].toInt();
            m_mainWindow->setDailyGoal(goal);
            m_mainWindow->settingsWidget()->updateDailyGoal(goal);
            qDebug() << "[App] daily_goal 갱신:" << goal;
        }

        // StudyWidget progress bar 최대값 = daily_goal (words.size() 아님)
        m_mainWindow->studyWidget()->setDailyGoal(
            m_mainWindow->settingsWidget()->currentDailyGoal());
        m_mainWindow->studyWidget()->setWordList(words);
        qDebug() << "[App] 단어 목록 수신:" << words.size() << "개";
    }

    // ── 204: 추론 결과 응답 ───────────────────────────
    else if (type == "RES_INFER") {
        bool   isCorrect = msg["result"].toBool();
        double accuracy  = msg["accuracy"].toDouble();   // 0.0~1.0
        int    wordId    = msg["word_id"].toInt();

        // 판정 로직
        // - is_correct = false           → incorrect (✗)
        // - is_correct = true, < 0.8    → partial   (△)
        // - is_correct = true, >= 0.8   → correct   (○)
        QString verdict;
        if (!isCorrect)             verdict = "incorrect";
        else if (accuracy < 0.8)    verdict = "partial";
        else                        verdict = "correct";

        double accuracyPct = accuracy * 100.0;

        // 현재 활성 위젯에 따라 결과 전달
        QWidget *current = m_mainWindow->currentWidget();
        if (current == m_mainWindow->testWidget()) {
            m_mainWindow->testWidget()->showResult(
                isCorrect, accuracy, wordId);
        } else if (current == m_mainWindow->reviewWidget()) {
            m_mainWindow->reviewWidget()->showResult(verdict, accuracyPct, wordId);
        } else {
            m_mainWindow->studyWidget()->showResult(verdict, accuracyPct, wordId);
        }
        qDebug() << "[App] RES_INFER: result=" << isCorrect
                 << "accuracy=" << accuracyPct << "%"
                 << "verdict=" << verdict;
    }

    // ── 206: 복습 단어 응답 ───────────────────────────
    else if (type == "RES_REVIEW_WORDS") {
        QList<StudyWidget::WordInfo> words;
        for (const auto &v : msg["words"].toArray()) {
            QJsonObject w = v.toObject();
            QString cdnUrl = w.value("video_cdn_url").toString();
            qDebug() << "[App] word=" << w["word"].toString()
                     << "video_cdn_url=" << cdnUrl;
            words.append({
                w["id"].toInt(),
                w["word"].toString(),
                w.value("meaning").toString(),
                w.value("difficulty").toInt(1),
                cdnUrl
            });
        }
        qDebug() << "[App] 복습 단어 수신:" << words.size() << "개";

        // 화면 전환은 reviewModeRequested 시점에 이미 완료됨
        if (words.isEmpty()) {
            m_mainWindow->reviewWidget()->showNoWordsMessage(
                "오늘 복습할 단어가 없습니다.\n학습을 먼저 진행해 주세요.");
            qDebug() << "[App] 복습할 단어 없음 → 안내 메시지 표시";
            return;
        }

        // ReviewWidget::WordInfo로 변환
        QList<ReviewWidget::WordInfo> reviewWords;
        for (const auto &w : words)
            reviewWords.append({w.id, w.word, w.meaning, w.difficulty, w.videoCdnUrl});
        m_mainWindow->reviewWidget()->setWordList(reviewWords);
    }

    // ── 302: 정방향 사전 검색 응답 ──────────────────────
    else if (type == "RES_DICT_SEARCH") {
        qDebug() << "[App] RES_DICT_SEARCH 전체:" << msg;
        if (msg["status"].toString() == "ok") {
            QList<DictWidget::DictResult> results;

            // description/meaning 둘 다 체크 (서버 필드명 대응)
            auto getDesc = [](const QJsonObject &o) -> QString {
                if (o.contains("meaning") && !o["meaning"].toString().isEmpty())
                    return o["meaning"].toString();
                return o.value("description").toString();
            };

            // 서버가 배열로 보내는 경우
            if (msg.contains("results") && msg["results"].isArray()) {
                for (const auto &v : msg["results"].toArray()) {
                    QJsonObject r = v.toObject();
                    results.append({
                        r["word_id"].toInt(),
                        r["word"].toString(),
                        getDesc(r),
                        r.value("video_cdn_url").toString()
                    });
                }
            } else {
                // 단일 결과로 보내는 경우 (기존 명세 호환)
                results.append({
                    msg["word_id"].toInt(),
                    msg["word"].toString(),
                    getDesc(msg),
                    msg.value("video_cdn_url").toString()
                });
            }

            m_mainWindow->dictWidget()->showForwardResult(results);
            qDebug() << "[App] RES_DICT_SEARCH 결과:" << results.size() << "개";
        } else {
            m_mainWindow->dictWidget()->showSearchError("검색 결과가 없습니다.");
        }
    }

    // ── 304: 역방향 사전 검색 응답 ──────────────────────
    else if (type == "RES_DICT_REVERSE") {
        if (msg["status"].toString() == "ok") {
            m_mainWindow->dictWidget()->showReverseResult(
                msg["word"].toString(),
                msg.value("description").toString()
            );
        } else {
            m_mainWindow->dictWidget()->showSearchError("수화를 인식하지 못했습니다. 다시 시도해 주세요.");
        }
    }

    // ── 802: 목표 단어 수 변경 응답 ─────────────────────
    else if (type == "RES_SET_DAILY_GOAL") {
        if (msg["status"].toString() == "ok") {
            int goal = msg["daily_goal"].toInt();
            // done 값 보존 — setDailyGoal은 goal만 갱신
            m_mainWindow->setDailyGoal(goal);
            // StudyWidget progress bar 최대값도 갱신
            // (단어 목록은 다음 REQ_DAILY_WORDS 응답 시 자동 갱신됨)
            m_mainWindow->settingsWidget()->onSaveSuccess("goal");
            qDebug() << "[App] 목표 단어 수 변경:" << goal;
        } else {
            m_mainWindow->settingsWidget()->onSaveError("목표 단어 수 변경에 실패했습니다.");
        }
    }

    // ── 804: 우세손 변경 응답 ────────────────────────────
    else if (type == "RES_SET_DOMINANT_HAND") {
        if (msg["status"].toString() == "ok")
            m_mainWindow->settingsWidget()->onSaveSuccess("hand");
        else
            m_mainWindow->settingsWidget()->onSaveError("우세손 변경에 실패했습니다.");
    }

    // ── 806: 농인 여부 변경 응답 ─────────────────────────
    else if (type == "RES_SET_DEAF") {
        if (msg["status"].toString() == "ok")
            m_mainWindow->settingsWidget()->onSaveSuccess("deaf");
        else
            m_mainWindow->settingsWidget()->onSaveError("농인 여부 변경에 실패했습니다.");
    }

    // ── 808: 비밀번호 변경 응답 ──────────────────────────
    else if (type == "RES_CHANGE_PASSWORD") {
        if (msg["status"].toString() == "ok")
            m_mainWindow->settingsWidget()->onSaveSuccess("password");
        else
            m_mainWindow->settingsWidget()->onSaveError("현재 비밀번호가 올바르지 않습니다.");
    }

    // ── 810: 키포인트 동의 변경 응답 ─────────────────────
    else if (type == "RES_SET_CONSENT") {
        if (msg["status"].toString() == "ok")
            m_mainWindow->settingsWidget()->onSaveSuccess("consent");
        else
            m_mainWindow->settingsWidget()->onSaveError("동의 설정 변경에 실패했습니다.");
    }

    // ── 812: 회원 탈퇴 응답 ──────────────────────────────
    else if (type == "RES_WITHDRAW") {
        if (msg["status"].toString() == "ok") {
            m_mainWindow->settingsWidget()->onWithdrawSuccess();
            // 세션 종료 후 로그인 화면으로
            m_sessionToken.clear();
            m_mainWindow->hide();
            m_loginWidget->show();
            qDebug() << "[App] 회원 탈퇴 완료 → 로그인 화면";
        } else {
            m_mainWindow->settingsWidget()->onSaveError("비밀번호가 올바르지 않습니다.");
        }
    }

    // ── 703: 오류 응답 ────────────────────────────────
    else if (type == "RES_ERROR") {
        int    errCode = msg["error_code"].toInt();
        QString err    = msg["message"].toString();
        qWarning() << "[App] 서버 오류:" << errCode << err;

        // not_authenticated(1003): 세션이 없는 상태의 오류
        // → 로그인 화면이 보이면 거기에 표시, 아니면 무시
        if (errCode == 1003) {
            if (m_sessionToken.isEmpty()) {
                // 로그인 전 상태 — 로그인 화면에 표시
                m_loginWidget->showError("인증 오류가 발생했습니다. 다시 로그인해 주세요.");
            } else {
                // 로그인 상태에서 발생 — 세션 만료로 간주, 로그아웃 처리
                qWarning() << "[App] 세션 만료 → 강제 로그아웃";
                m_sessionToken.clear();
                m_lastUsername.clear();
                m_loginWidget->reset();
                m_loginWidget->setConnected(m_client->isConnected());
                m_loginWidget->showError("세션이 만료됐습니다. 다시 로그인해 주세요.");
                m_mainWindow->hide();
                m_loginWidget->show();
            }
            return;
        }

        // 그 외 오류: 현재 표시 중인 화면에 따라 분기
        if (m_sessionToken.isEmpty()) {
            m_loginWidget->showError("오류: " + err);
        } else {
            // 메인 화면 중 오류 — 설정 화면이 열려있으면 설정에 표시
            m_mainWindow->settingsWidget()->onSaveError("서버 오류: " + err);
            qWarning() << "[App] 메인 화면 서버 오류:" << err;
        }
    }

    else {
        qDebug() << "[App] 미처리 메시지 type:" << type;
    }
}


