#include "ReviewWidget.h"
#include "ui_StudyWidget.h"   // StudyWidget.ui 재사용

#include <QButtonGroup>
#include <QDebug>
#include <QKeyEvent>
#include <QHideEvent>
#include <QJsonArray>
#include <QVBoxLayout>

ReviewWidget::ReviewWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::StudyWidget)
    , m_stopTimer(new QTimer(this))
    , m_speedGroup(new QButtonGroup(this))
    , m_cooldownTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    ui->setupUi(this);

    // 헤더 텍스트 → "복습 모드"
    ui->wordCountLabel->setText("복습 모드");

    ui->prevBtn->setFocusPolicy(Qt::NoFocus);
    ui->nextBtn->setFocusPolicy(Qt::NoFocus);
    ui->skipBtn->setFocusPolicy(Qt::NoFocus);
    ui->speed025->setFocusPolicy(Qt::NoFocus);
    ui->speed050->setFocusPolicy(Qt::NoFocus);
    ui->speed100->setFocusPolicy(Qt::NoFocus);
    ui->speed150->setFocusPolicy(Qt::NoFocus);
    ui->speed200->setFocusPolicy(Qt::NoFocus);

    // resultCard 초기 숨김
    ui->resultCard->setStyleSheet("QWidget#resultCard { background: transparent; border: none; }"); ui->verdictLabel->setText(""); ui->confidenceLabel->setText("");

    // ── VideoPlayer 위젯 교체 (StudyWidget.ui의 QLabel videoPlayer 자리에 삽입) ──
    m_videoPlayer = new VideoPlayer(this);
    m_videoPlayer->setMinimumSize(ui->videoPlayer->minimumSize());
    m_videoPlayer->setFixedHeight(240);
    QLayout *videoLayout = ui->videoPlayer->parentWidget()->layout();
    if (auto *vbox = qobject_cast<QVBoxLayout *>(videoLayout)) {
        for (int i = 0; i < vbox->count(); ++i) {
            if (vbox->itemAt(i)->widget() == ui->videoPlayer) {
                vbox->insertWidget(i, m_videoPlayer);
                break;
            }
        }
    } else {
        videoLayout->removeWidget(ui->videoPlayer);
        videoLayout->addWidget(m_videoPlayer);
    }
    ui->videoPlayer->hide();

    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,        &ReviewWidget::onRecordingTimeout);

    // 쿨다운 타이머: 녹화 종료 후 공수 자세가 풀릴 때까지 재시작 방지
    m_cooldownTimer->setSingleShot(true);
    m_cooldownTimer->setInterval(1500);

    // 카운트다운 타이머
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout,
            this,             &ReviewWidget::onCountdownTick);

    m_speedGroup->addButton(ui->speed025);
    m_speedGroup->addButton(ui->speed050);
    m_speedGroup->addButton(ui->speed100);
    m_speedGroup->addButton(ui->speed150);
    m_speedGroup->addButton(ui->speed200);
    m_speedGroup->setExclusive(true);

    connect(m_speedGroup, &QButtonGroup::buttonClicked,
            this,         &ReviewWidget::onSpeedChanged);
    connect(ui->prevBtn,   &QPushButton::clicked,
            this,          &ReviewWidget::onPrevClicked);
    connect(ui->nextBtn,   &QPushButton::clicked,
            this,          &ReviewWidget::onNextClicked);
    connect(ui->skipBtn,   &QPushButton::clicked,
            this,          &ReviewWidget::onSkipClicked);
    ui->recordBtn->setFixedWidth(110);
    connect(ui->recordBtn, &QPushButton::clicked,
            this,          &ReviewWidget::onRecordBtnClicked);
}

ReviewWidget::~ReviewWidget()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// showNoWordsMessage — 복습 단어 없을 때 안내
// ─────────────────────────────────────────────────────────────
void ReviewWidget::showNoWordsMessage(const QString &message)
{
    ui->studyProgress->setMaximum(1);
    ui->studyProgress->setValue(0);
    ui->wordIndexLabel->setText("0 / 0");

    ui->wordLabel->setText("📭");
    ui->meaningLabel->setText(message);
    ui->videoPlayer->setText("");

    ui->resultCard->setStyleSheet("QWidget#resultCard { background: transparent; border: none; }"); ui->verdictLabel->setText(""); ui->confidenceLabel->setText("");
    ui->nextBtn->setEnabled(false);
    ui->prevBtn->setEnabled(false);
    ui->skipBtn->setEnabled(false);
    ui->statusLabel->setText(message);
    ui->recordingLabel->setText("");

    qDebug() << "[Review] 단어 없음:" << message;
}

// ─────────────────────────────────────────────────────────────
// setWordList
// ─────────────────────────────────────────────────────────────
void ReviewWidget::setWordList(const QList<WordInfo> &words)
{
    m_words        = words;
    m_currentIndex = 0;
    ui->studyProgress->setMaximum(words.size());
    ui->skipBtn->setEnabled(true);
    loadWord(0);
}

// ─────────────────────────────────────────────────────────────
// loadWord
// ─────────────────────────────────────────────────────────────
void ReviewWidget::loadWord(int index)
{
    if (m_words.isEmpty() || index >= m_words.size()) {
        showCompletionMessage();
        return;
    }

    const WordInfo &w = m_words[index];
    ui->wordLabel->setText(w.word);
    ui->meaningLabel->setText(w.meaning);
    updateProgress();

    ui->resultCard->setStyleSheet("QWidget#resultCard { background: transparent; border: none; }"); ui->verdictLabel->setText(""); ui->confidenceLabel->setText("");
    ui->nextBtn->setEnabled(false);
    ui->nextBtn->setText("다음 단어 →");
    ui->prevBtn->setEnabled(index > 0);

    m_isRecording     = false;
    m_keypointBuffer  = QJsonArray();
    m_hasPrevKeypoint = false;
    m_stopTimer->stop();
    m_countdownTimer->stop();
    m_cooldownTimer->stop();
    m_countdown = 0;

    ui->recordingLabel->setText("");
    ui->recordBtn->setText("● 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
    ui->statusLabel->setText("녹화 버튼을 눌러 시작하세요");

    // 영상 로드
    if (!w.videoCdnUrl.isEmpty()) {
        QString filename = w.videoCdnUrl.section('/', -1);
        m_videoPlayer->setCurrentWordId(w.id);
        m_videoPlayer->play(w.videoCdnUrl, filename);
    }

    qDebug() << "[Review] 단어 로드:" << w.word
             << "(" << index+1 << "/" << m_words.size() << ")";
}

// ─────────────────────────────────────────────────────────────
// onCameraFrame
// ─────────────────────────────────────────────────────────────
void ReviewWidget::onCameraFrame(const QImage &frame)
{
    ui->cameraView->setPixmap(
        QPixmap::fromImage(frame).scaled(
            ui->cameraView->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

// ─────────────────────────────────────────────────────────────
// onKeypointFrame
// ─────────────────────────────────────────────────────────────
void ReviewWidget::onKeypointFrame(const QJsonObject &keypoint)
{
    if (!m_isRecording) return;

    m_keypointBuffer.append(keypoint);

    // ── 움직임 감지: 이전 프레임과의 손목 좌표 변화량으로 판단 ──
    static constexpr double MOTION_THRESHOLD = 19.2; // 픽셀 기준 (1920x1080 해상도, 약 1%)

    auto wrist = [](const QJsonObject &kp, const QString &side) -> QPointF {
        const QJsonArray hand = kp[side].toArray();
        if (hand.isEmpty()) return {};
        const QJsonArray w = hand[0].toArray();
        return { w[0].toDouble(), w[1].toDouble() };
    };

    bool moving = false;
    if (m_hasPrevKeypoint) {
        auto nonZero = [](QPointF p){ return p.x() != 0.0 || p.y() != 0.0; };
        QPointF lC = wrist(keypoint, "left_hand"),  lP = wrist(m_prevKeypoint, "left_hand");
        QPointF rC = wrist(keypoint, "right_hand"), rP = wrist(m_prevKeypoint, "right_hand");
        if (nonZero(lC) && nonZero(lP) && qAbs(lC.x()-lP.x())+qAbs(lC.y()-lP.y()) >= MOTION_THRESHOLD) moving = true;
        if (!moving && nonZero(rC) && nonZero(rP) && qAbs(rC.x()-rP.x())+qAbs(rC.y()-rP.y()) >= MOTION_THRESHOLD) moving = true;
    }
    m_prevKeypoint    = keypoint;
    m_hasPrevKeypoint = true;

    if (moving)
        m_stopTimer->start();
    else if (!m_stopTimer->isActive())
        m_stopTimer->start();
}

// ─────────────────────────────────────────────────────────────
// startRecording
// ─────────────────────────────────────────────────────────────
void ReviewWidget::startRecording()
{
    if (m_isRecording) return;
    m_isRecording       = true;
    m_keypointBuffer    = QJsonArray();
    m_hasPrevKeypoint   = false;
    m_recordingStartTime.start();

    ui->recordingLabel->setStyleSheet("font-size:13px; color:#E24B4A; font-weight:500;");
    ui->recordingLabel->setText("● 녹화 중");
    ui->recordBtn->setText("■ 중단");
    ui->recordBtn->setStyleSheet("QPushButton { background: #E24B4A; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
    ui->statusLabel->setText("녹화 중... 수화를 입력하세요.\n움직임이 멈추면 자동 종료됩니다.");
    qDebug() << "[Review] 녹화 시작";
}

// ─────────────────────────────────────────────────────────────
// stopRecording
// ─────────────────────────────────────────────────────────────
void ReviewWidget::stopRecording()
{
    if (!m_isRecording) return;
    m_isRecording = false;
    m_stopTimer->stop();
    ui->recordingLabel->setText("");
    ui->recordBtn->setText("● 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");

    int frameCount = m_keypointBuffer.size();
    qDebug() << "[Review] 녹화 종료, 프레임:" << frameCount;

    if (m_words.isEmpty()) {
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


void ReviewWidget::hideEvent(QHideEvent *event)
{
    if (m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
        m_countdown = 0;
        ui->recordingLabel->setText("");
        ui->recordBtn->setText("● 녹화");
        ui->recordBtn->setStyleSheet(
            "QPushButton { background: #3B6D11; color: white; border: none;"
            " border-radius: 20px; font-size: 13px; font-weight: 500;"
            " padding: 8px 24px; min-width: 100px; }");
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

void ReviewWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        onRecordBtnClicked(); return;
    }
    QWidget::keyPressEvent(event);
}

void ReviewWidget::onRecordBtnClicked()
{
    if (m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
        m_countdown = 0;
        ui->recordingLabel->setStyleSheet("font-size:13px; color:#E24B4A; font-weight:500;");
        ui->recordingLabel->setText("");
        ui->statusLabel->setText("녹화가 취소됐습니다.");
        ui->recordBtn->setText("● 녹화");
        ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
        return;
    }
    if (m_isRecording) {
        m_isRecording = false; m_stopTimer->stop();
        m_keypointBuffer = QJsonArray();
        ui->recordingLabel->setText("");
        ui->recordBtn->setText("● 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
        ui->statusLabel->setText("녹화가 중단됐습니다.");
        qDebug() << "[Review] 녹화 중단";
        return;
    }
    m_countdown = 3;
    ui->recordingLabel->setText(QString::number(m_countdown));
    ui->recordingLabel->setStyleSheet("font-size:28px; font-weight:700; color:#3B6D11;");
    ui->statusLabel->setText("잠시 후 녹화가 시작됩니다...");
    ui->recordBtn->setText("■ 취소");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
    m_countdownTimer->start();
    qDebug() << "[Review] 카운트다운 시작";
}

void ReviewWidget::onCountdownTick()
{
    m_countdown--;
    if (m_countdown > 0) {
        ui->recordingLabel->setText(QString::number(m_countdown));
    } else {
        m_countdownTimer->stop();
        ui->recordingLabel->setText("");
        startRecording();
    }
}
void ReviewWidget::onRecordingTimeout() { stopRecording(); }

// ─────────────────────────────────────────────────────────────
// showResult
// ─────────────────────────────────────────────────────────────
void ReviewWidget::showResult(const QString &verdict,
                               double confidence,
                               int predictedWordId)
{
    Q_UNUSED(predictedWordId)

    ui->confidenceLabel->setVisible(false);   // 신뢰도 숨김
    applyVerdictStyle(verdict);

    ui->nextBtn->setEnabled(true);
    ui->nextBtn->setText("다음 단어 →");   // 항상 동일 텍스트

    if (verdict == "correct")
        ui->statusLabel->setText("정확합니다! 다음 단어로 이동하세요.");
    else if (verdict == "partial")
        ui->statusLabel->setText("맞았지만 정확도가 낮습니다. 다시 시도해 보세요.");
    else
        ui->statusLabel->setText("다시 시도해 보세요.");
}

// ─────────────────────────────────────────────────────────────
// applyVerdictStyle
// ─────────────────────────────────────────────────────────────
void ReviewWidget::applyVerdictStyle(const QString &verdict)
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

// ─────────────────────────────────────────────────────────────
// updateProgress
// ─────────────────────────────────────────────────────────────
void ReviewWidget::updateProgress()
{
    int total   = m_words.size();
    int current = m_currentIndex + 1;
    ui->studyProgress->setValue(current);
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(current).arg(total));
}

// ─────────────────────────────────────────────────────────────
// showCompletionMessage — 복습 완료 시 버튼을 "완료"로 변경
// ─────────────────────────────────────────────────────────────
void ReviewWidget::showCompletionMessage()
{
    ui->studyProgress->setValue(m_words.size());
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(m_words.size()).arg(m_words.size()));

    ui->skipBtn->setEnabled(false);
    ui->nextBtn->setText("완료");
    ui->nextBtn->setEnabled(true);
    ui->statusLabel->setText(
        QString("복습 완료! 단어 %1개를 모두 복습했습니다.")
        .arg(m_words.size()));

    qDebug() << "[Review] 복습 완료 → 완료 버튼 활성화";
}

// ─────────────────────────────────────────────────────────────
// 버튼 슬롯
// ─────────────────────────────────────────────────────────────
void ReviewWidget::onPrevClicked()
{
    if (m_currentIndex <= 0) return;
    m_currentIndex--;
    loadWord(m_currentIndex);
}

void ReviewWidget::onNextClicked()
{
    if (ui->nextBtn->text() == "다시 시도") {
        loadWord(m_currentIndex);
        return;
    }
    if (ui->nextBtn->text() == "완료") {
        emit reviewFinished();
        return;
    }

    m_currentIndex++;
    if (m_currentIndex >= m_words.size())
        showCompletionMessage();
    else
        loadWord(m_currentIndex);
}

void ReviewWidget::onSkipClicked()
{
    if (m_words.isEmpty() || m_currentIndex >= m_words.size()) return;

    emit wordSkipped(m_words[m_currentIndex].id);
    m_currentIndex++;

    if (m_currentIndex >= m_words.size())
        showCompletionMessage();
    else
        loadWord(m_currentIndex);
}

void ReviewWidget::onReplayClicked()
{
    qDebug() << "[Review] 영상 다시 보기 (VideoPlayer 연동 예정)";
}

void ReviewWidget::onSpeedChanged()
{
    QMap<QPushButton*, double> speedMap = {
        {ui->speed025, 0.25}, {ui->speed050, 0.50},
        {ui->speed100, 1.00}, {ui->speed150, 1.50},
        {ui->speed200, 2.00},
    };
    auto *btn = qobject_cast<QPushButton*>(m_speedGroup->checkedButton());
    if (btn && speedMap.contains(btn)) {
        m_playSpeed = speedMap[btn];
        qDebug() << "[Review] 재생 속도:" << m_playSpeed;
    }
}
