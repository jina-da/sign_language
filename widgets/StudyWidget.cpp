#include "StudyWidget.h"
#include "ui_StudyWidget.h"

#include <QButtonGroup>
#include <QDebug>
#include <QKeyEvent>
#include <QHideEvent>
#include <QJsonArray>
#include <QMediaPlayer>

StudyWidget::StudyWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::StudyWidget)
    , m_stopTimer(new QTimer(this))
    , m_speedGroup(new QButtonGroup(this))
    , m_cooldownTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    ui->setupUi(this);

    // videoPlayer QLabel → VideoPlayer 위젯으로 교체
    m_videoPlayer = new VideoPlayer(this);
    m_videoPlayer->setMinimumSize(ui->videoPlayer->minimumSize());

    // QLabel을 레이아웃에서 제거하고 VideoPlayer 삽입
    QLayout *videoLayout = ui->videoPlayer->parentWidget()->layout();
    if (videoLayout) {
        int idx = -1;
        for (int i = 0; i < videoLayout->count(); i++) {
            if (videoLayout->itemAt(i)->widget() == ui->videoPlayer) {
                idx = i;
                break;
            }
        }
        if (idx >= 0) {
            videoLayout->removeWidget(ui->videoPlayer);
            ui->videoPlayer->hide();
            // QVBoxLayout에 같은 위치에 삽입
            if (auto *vbox = qobject_cast<QVBoxLayout*>(videoLayout)) {
                vbox->insertWidget(idx, m_videoPlayer);
            } else {
                videoLayout->addWidget(m_videoPlayer);
            }
        }
    }

    // VideoPlayer 고정 높이 (cameraView와 동일하게)
    m_videoPlayer->setFixedHeight(240);

    // resultCard 초기 숨김 (btnRow 안에 배치됨)
    ui->resultCard->setStyleSheet("QWidget#resultCard { background: transparent; border: none; }"); ui->verdictLabel->setText(""); ui->confidenceLabel->setText("");

    // 재생 완료 시 다시 보기 버튼 활성화
    connect(m_videoPlayer, &VideoPlayer::playbackFinished,
            this, [this]{
        qDebug() << "[Study] 영상 재생 완료";
    });

    // 버튼들이 스페이스바로 클릭되지 않도록 포커스 정책 제거
    ui->prevBtn->setFocusPolicy(Qt::NoFocus);
    ui->nextBtn->setFocusPolicy(Qt::NoFocus);
    ui->skipBtn->setFocusPolicy(Qt::NoFocus);
    ui->speed025->setFocusPolicy(Qt::NoFocus);
    ui->speed050->setFocusPolicy(Qt::NoFocus);
    ui->speed100->setFocusPolicy(Qt::NoFocus);
    ui->speed150->setFocusPolicy(Qt::NoFocus);
    ui->speed200->setFocusPolicy(Qt::NoFocus);

    // 1.5초 정지 감지 타이머
    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,        &StudyWidget::onRecordingTimeout);

    // 쿨다운 타이머
    m_cooldownTimer->setSingleShot(true);
    m_cooldownTimer->setInterval(1500);

    // 카운트다운 타이머 (1초 간격)
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout,
            this,             &StudyWidget::onCountdownTick);

    // 속도 버튼 그룹
    m_speedGroup->addButton(ui->speed025);
    m_speedGroup->addButton(ui->speed050);
    m_speedGroup->addButton(ui->speed100);
    m_speedGroup->addButton(ui->speed150);
    m_speedGroup->addButton(ui->speed200);
    m_speedGroup->setExclusive(true);

    connect(m_speedGroup, &QButtonGroup::buttonClicked,
            this,         &StudyWidget::onSpeedChanged);

    connect(ui->prevBtn,   &QPushButton::clicked,
            this,          &StudyWidget::onPrevClicked);
    connect(ui->nextBtn,   &QPushButton::clicked,
            this,          &StudyWidget::onNextClicked);
    connect(ui->skipBtn,   &QPushButton::clicked,
            this,          &StudyWidget::onSkipClicked);
    ui->recordBtn->setFixedWidth(110);
    connect(ui->recordBtn, &QPushButton::clicked,
            this,          &StudyWidget::onRecordBtnClicked);
}

StudyWidget::~StudyWidget()
{
    delete ui;
}

// setDailyGoal — progress bar 최대값을 daily_goal로 설정
// AppController에서 RES_LOGIN/RES_DAILY_WORDS 수신 후 호출
void StudyWidget::setDailyGoal(int goal)
{
    m_dailyGoal = goal;
    ui->studyProgress->setMaximum(goal);
    qDebug() << "[Study] daily_goal 설정:" << goal;
}

void StudyWidget::setWordList(const QList<WordInfo> &words)
{
    m_words        = words;
    m_currentIndex = 0;
    // progress bar 최대값은 daily_goal 기준 (words.size() 아님)
    ui->studyProgress->setMaximum(m_dailyGoal);
    ui->skipBtn->setEnabled(true);
    loadWord(0);
}

void StudyWidget::loadWord(int index)
{
    if (m_words.isEmpty() || index >= m_words.size()) {
        emit studyFinished();
        ui->skipBtn->setEnabled(false);
        ui->nextBtn->setEnabled(false);
        ui->prevBtn->setEnabled(false);
        ui->statusLabel->setText("학습이 완료됐습니다!");
        return;
    }

    const WordInfo &w = m_words[index];
    ui->wordLabel->setText(w.word);
    ui->meaningLabel->setText(w.meaning);
    updateProgress();

    // 결과 카드 숨김
    ui->resultCard->setStyleSheet("QWidget#resultCard { background: transparent; border: none; }"); ui->verdictLabel->setText(""); ui->confidenceLabel->setText("");

    bool isLastWord = (index == m_words.size() - 1);
    if (isLastWord) {
        ui->nextBtn->setEnabled(true);
        ui->nextBtn->setText("테스트 시작 →");
    } else {
        ui->nextBtn->setEnabled(false);
        ui->nextBtn->setText("다음 단어 →");
    }

    // 이전 단어 버튼: 첫 번째 단어이면 비활성화
    ui->prevBtn->setEnabled(index > 0);

    // 녹화/카운트다운/쿨다운 상태 완전 초기화
    m_isRecording    = false;
    m_keypointBuffer = QJsonArray();
    m_hasPrevKeypoint = false;
    m_stopTimer->stop();
    m_countdownTimer->stop();
    m_cooldownTimer->stop();
    m_countdown = 0;

    ui->recordingLabel->setText("");
    ui->countdownLabel->setText("");
    ui->recordBtn->setText("● 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
    ui->statusLabel->setText(
        "녹화 버튼을 눌러 시작하세요");

    // 영상 로드
    if (!w.videoCdnUrl.isEmpty()) {
        QString filename = w.videoCdnUrl.split("/").last().split("?").first();
        m_videoPlayer->setCurrentWordId(w.id);
        m_videoPlayer->play(w.videoCdnUrl, filename);
        }

    qDebug() << "[Study] 단어 로드:" << w.word
             << "(" << index+1 << "/" << m_words.size() << ")";
}

void StudyWidget::onCameraFrame(const QImage &frame)
{
    ui->cameraView->setPixmap(
        QPixmap::fromImage(frame).scaled(
            ui->cameraView->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

void StudyWidget::onKeypointFrame(const QJsonObject &keypoint)
{
    // 버튼 방식으로 전환 — 녹화 중일 때만 처리
    if (!m_isRecording) return;

    m_keypointBuffer.append(keypoint);

    static constexpr double MOTION_THRESHOLD = 19.2; // 픽셀 기준 (1920x1080 해상도, 약 1%)

    auto wrist = [](const QJsonObject &kp, const QString &side) -> QPointF {
        const QJsonArray hand = kp[side].toArray();
        if (hand.isEmpty()) return {};
        const QJsonArray w = hand[0].toArray();
        return { w[0].toDouble(), w[1].toDouble() };
    };

    bool moving = false;
    if (m_hasPrevKeypoint) {
        QPointF lCur  = wrist(keypoint,      "left_hand");
        QPointF lPrev = wrist(m_prevKeypoint, "left_hand");
        QPointF rCur  = wrist(keypoint,      "right_hand");
        QPointF rPrev = wrist(m_prevKeypoint, "right_hand");

        // 손이 실제로 감지된 경우(0,0 이 아닌 경우)만 비교
        auto nonZero = [](QPointF p) { return p.x() != 0.0 || p.y() != 0.0; };

        if (nonZero(lCur) && nonZero(lPrev)) {
            double dx = lCur.x() - lPrev.x();
            double dy = lCur.y() - lPrev.y();
            if (qAbs(dx) + qAbs(dy) >= MOTION_THRESHOLD) moving = true;
        }
        if (!moving && nonZero(rCur) && nonZero(rPrev)) {
            double dx = rCur.x() - rPrev.x();
            double dy = rCur.y() - rPrev.y();
            if (qAbs(dx) + qAbs(dy) >= MOTION_THRESHOLD) moving = true;
        }
    }

    m_prevKeypoint    = keypoint;
    m_hasPrevKeypoint = true;

    if (moving) {
        // 움직임이 감지되는 동안 타이머를 계속 리셋
        m_stopTimer->start();
    } else {
        // 정지 상태: 타이머가 멈춰 있으면 시작 (1.5초 후 자동 종료)
        if (!m_stopTimer->isActive())
            m_stopTimer->start();
    }
}

void StudyWidget::startRecording()
{
    if (m_isRecording) return;

    m_isRecording       = true;
    m_keypointBuffer    = QJsonArray();
    m_hasPrevKeypoint   = false;   // 이전 프레임 초기화
    m_recordingStartTime.start();

    ui->recordingLabel->setStyleSheet("font-size:13px; color:#E24B4A; font-weight:500;");
    ui->recordingLabel->setText("● 녹화 중");
    ui->recordBtn->setText("■ 중단");
    ui->recordBtn->setStyleSheet("QPushButton { background: #E24B4A; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
    ui->statusLabel->setText("녹화 중... 수화를 입력하세요.\n움직임이 멈추면 자동 종료됩니다.");

    // 타이머는 첫 키포인트 프레임이 들어올 때부터 시작 (onKeypointFrame에서 처리)
    // 여기서 즉시 시작하면 키포인트 서버 초기화 지연으로 오작동 가능

    qDebug() << "[Study] 녹화 시작";
}

void StudyWidget::stopRecording()
{
    if (!m_isRecording) return;
    m_isRecording = false;
    m_stopTimer->stop();
    ui->recordingLabel->setText("");
    ui->recordBtn->setText("● 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");

    int frameCount = m_keypointBuffer.size();
    qDebug() << "[Study] 녹화 종료, 누적 프레임:" << frameCount;

    if (m_words.isEmpty()) {
        qWarning() << "[Study] 단어 목록 없음 - 전송 취소";
        ui->statusLabel->setText("단어 목록을 불러온 후 다시 시도해 주세요.");
        return;
    }

    if (frameCount < 3) {
        ui->statusLabel->setText("입력이 너무 짧습니다. 다시 시도해 주세요.");
        return;
    }

    ui->statusLabel->setText("인식 중...");
    const WordInfo &w = m_words[m_currentIndex];
    emit keypointReady(w.id, false, m_keypointBuffer);
}

void StudyWidget::onRecordingTimeout()
{
    stopRecording();
}

void StudyWidget::showResult(const QString &verdict,
                              double confidence,
                              int predictedWordId)
{
    Q_UNUSED(predictedWordId)

    ui->confidenceLabel->setVisible(false);   // 신뢰도 숨김
    applyVerdictStyle(verdict);

    ui->nextBtn->setEnabled(true);
    bool isLastWord = (m_currentIndex == m_words.size() - 1);
    ui->nextBtn->setText(isLastWord ? "테스트 시작 →" : "다음 단어 →");

    if (verdict == "correct")
        ui->statusLabel->setText("정확합니다! 다음 단어로 이동하세요.");
    else if (verdict == "partial")
        ui->statusLabel->setText("맞았지만 정확도가 낮습니다. 다시 시도해 보세요.");
    else
        ui->statusLabel->setText("다시 시도해 보세요.");
}

void StudyWidget::applyVerdictStyle(const QString &verdict)
{
    if (verdict == "correct") {
        ui->verdictLabel->setText("○ 정답!");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#D5F5E3; border-radius:12px; border:1px solid #82E0AA; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#1E8449;");
    } else if (verdict == "partial") {
        ui->verdictLabel->setText("△ 다시 시도");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#FEF9E7; border-radius:12px; border:1px solid #F9E79F; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#B7950B;");
    } else {
        ui->verdictLabel->setText("✗ 오답입니다");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#FDEDEC; border-radius:12px; border:1px solid #F1948A; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#A93226;");
    }
}

void StudyWidget::updateProgress()
{
    int total   = m_words.size();
    int current = m_currentIndex + 1;

    ui->studyProgress->setValue(current);
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(current).arg(total));
}

// 이전 단어로 이동
void StudyWidget::onPrevClicked()
{
    if (m_currentIndex <= 0) return;
    m_currentIndex--;
    loadWord(m_currentIndex);
}

// 마지막 단어 후 완료 알림
void StudyWidget::onNextClicked()
{
    // 마지막 단어 완료 후 "테스트 시작 →" 버튼을 누른 경우
    if (ui->nextBtn->text() == "테스트 시작 →") {
        emit testRequested(m_words);
        return;
    }

    m_currentIndex++;

    if (m_currentIndex >= m_words.size()) {
        showCompletionDialog();
        return;
    }

    loadWord(m_currentIndex);
}

// 모든 단어 완료 시 알림
void StudyWidget::showCompletionDialog()
{
    // 진도 바 100% 로 채우기
    ui->studyProgress->setValue(m_words.size());
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(m_words.size()).arg(m_words.size()));

    ui->skipBtn->setEnabled(false);
    ui->nextBtn->setText("테스트 시작 →");
    ui->nextBtn->setEnabled(true);
    ui->statusLabel->setText("학습 완료! 테스트를 시작하세요.");
    qDebug() << "[Study] 학습 완료 → 테스트 시작 버튼 활성화";
}

void StudyWidget::onSkipClicked()
{
    if (m_words.isEmpty() || m_currentIndex >= m_words.size())
        return;

    emit wordSkipped(m_words[m_currentIndex].id);
    m_currentIndex++;

    if (m_currentIndex >= m_words.size()) {
        showCompletionDialog();
        return;
    }

    loadWord(m_currentIndex);
}

void StudyWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        // 영상 재생 중이면 스페이스바를 영상 재생/일시정지로 처리
        if (m_videoPlayer &&
            m_videoPlayer->player()->playbackState() == QMediaPlayer::PlayingState) {
            m_videoPlayer->togglePlayPause();
            return;
        }
        onRecordBtnClicked();
        return;
    }
    QWidget::keyPressEvent(event);
}

void StudyWidget::hideEvent(QHideEvent *event)
{
    // 탭 전환 등으로 위젯이 숨겨질 때 카운트다운/녹화 상태 초기화
    if (m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
        m_countdown = 0;
        ui->countdownLabel->setText("");
        ui->recordBtn->setText("● 녹화");
        ui->recordBtn->setStyleSheet(
            "QPushButton { background: #3B6D11; color: white; border: none;"
            " border-radius: 20px; font-size: 13px; font-weight: 500;"
            " padding: 8px 24px; min-width: 100px; }");
        ui->statusLabel->setText("녹화 버튼을 눌러 시작하세요");
    }
    if (m_isRecording) {
        m_stopTimer->stop();
        m_isRecording = false;
        m_keypointBuffer = QJsonArray();
        ui->recordingLabel->setText("");
        ui->recordBtn->setText("● 녹화");
        ui->recordBtn->setStyleSheet(
            "QPushButton { background: #3B6D11; color: white; border: none;"
            " border-radius: 20px; font-size: 13px; font-weight: 500;"
            " padding: 8px 24px; min-width: 100px; }");
    }
    m_cooldownTimer->stop();

    // 탭 이동 시 영상을 처음으로 되돌리고 일시정지
    if (m_videoPlayer)
        m_videoPlayer->resetToStart();

    QWidget::hideEvent(event);
}

void StudyWidget::onRecordBtnClicked()
{
    // 녹화/카운트다운 중이 아닐 때만 카메라 연결 체크
    if (!m_isRecording && !m_countdownTimer->isActive() && !m_cameraConnected) {
        ui->statusLabel->setText("카메라가 연결되지 않았습니다.\n잠시 후 다시 시도하세요.");
        return;
    }

    // 카운트다운 진행 중 취소
    if (m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
        m_countdown = 0;
        ui->countdownLabel->setText("");
        ui->recordingLabel->setText("");
        ui->statusLabel->setText("녹화가 취소됐습니다.");
        ui->recordBtn->setText("● 녹화");
        ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
        return;
    }

    if (m_isRecording) {
        // 녹화 중 → 중단 (전송 안 함)
        m_isRecording = false;
        m_stopTimer->stop();
        m_keypointBuffer = QJsonArray();
        ui->recordingLabel->setText("");
        ui->recordBtn->setText("● 녹화");
        ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
        ui->statusLabel->setText("녹화가 중단됐습니다.\n다시 시작하려면 버튼을 누르세요.");
        qDebug() << "[Study] 녹화 중단 (전송 안 함)";
        return;
    }

    // 카운트다운 시작
    m_countdown = 3;
    ui->countdownLabel->setText("");
    ui->recordingLabel->setText(QString::number(m_countdown));
    ui->recordingLabel->setStyleSheet("font-size:28px; font-weight:700; color:#3B6D11;");
    ui->statusLabel->setText("잠시 후 녹화가 시작됩니다...");
    ui->recordBtn->setText("■ 취소");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
    m_countdownTimer->start();
    qDebug() << "[Study] 카운트다운 시작";
}

void StudyWidget::onCountdownTick()
{
    m_countdown--;
    if (m_countdown > 0) {
        ui->recordingLabel->setText(QString::number(m_countdown));
    } else {
        // 카운트다운 완료 → 녹화 시작
        m_countdownTimer->stop();
        ui->recordingLabel->setText("");
        startRecording();
    }
}

void StudyWidget::onSpeedChanged()
{
    QMap<QPushButton*, double> speedMap = {
        {ui->speed025, 0.25},
        {ui->speed050, 0.50},
        {ui->speed100, 1.00},
        {ui->speed150, 1.50},
        {ui->speed200, 2.00},
    };
    auto *btn = qobject_cast<QPushButton*>(m_speedGroup->checkedButton());
    if (btn && speedMap.contains(btn)) {
        m_playSpeed = speedMap[btn];
        m_videoPlayer->setSpeed(m_playSpeed);
        qDebug() << "[Study] 재생 속도:" << m_playSpeed;
    }
}
