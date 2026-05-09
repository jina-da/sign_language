#include "TestWidget.h"
#include "ui_TestWidget.h"

#include <QDebug>
#include <QJsonArray>

TestWidget::TestWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::TestWidget)
    , m_speedGroup(new QButtonGroup(this))
    , m_stopTimer(new QTimer(this))
    , m_cooldownTimer(new QTimer(this))
{
    ui->setupUi(this);

    ui->nextBtn->setFocusPolicy(Qt::NoFocus);
    ui->homeBtn->setFocusPolicy(Qt::NoFocus);
    ui->replayBtn->setFocusPolicy(Qt::NoFocus);
    ui->speed025->setFocusPolicy(Qt::NoFocus);
    ui->speed050->setFocusPolicy(Qt::NoFocus);
    ui->speed100->setFocusPolicy(Qt::NoFocus);
    ui->speed150->setFocusPolicy(Qt::NoFocus);
    ui->speed200->setFocusPolicy(Qt::NoFocus);

    // 속도 버튼 그룹
    m_speedGroup->addButton(ui->speed025);
    m_speedGroup->addButton(ui->speed050);
    m_speedGroup->addButton(ui->speed100);
    m_speedGroup->addButton(ui->speed150);
    m_speedGroup->addButton(ui->speed200);
    m_speedGroup->setExclusive(true);
    connect(m_speedGroup, &QButtonGroup::buttonClicked,
            this,         &TestWidget::onSpeedChanged);

    // 1.5초 정지 감지 타이머 (StudyWidget과 동일)
    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,        &TestWidget::onRecordingTimeout);

    // 쿨다운 타이머: 녹화 종료 후 공수 자세가 풀릴 때까지 재시작 방지
    m_cooldownTimer->setSingleShot(true);
    m_cooldownTimer->setInterval(1500);   // 1.5초간 재시작 차단

    connect(ui->nextBtn,        &QPushButton::clicked,
            this,               &TestWidget::onNextClicked);
    connect(ui->homeBtn,        &QPushButton::clicked,
            this,               &TestWidget::onHomeClicked);
    connect(ui->summaryHomeBtn, &QPushButton::clicked,
            this,               &TestWidget::onHomeClicked);
    connect(ui->replayBtn,      &QPushButton::clicked,
            this,               &TestWidget::onReplayClicked);

    ui->pageStack->setCurrentWidget(ui->testPage);
}

TestWidget::~TestWidget()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// setWordList
// ─────────────────────────────────────────────────────────────
void TestWidget::setWordList(const QList<WordInfo> &words)
{
    m_words        = words;
    m_currentIndex = 0;
    m_correctCount = 0;

    ui->testProgress->setMaximum(words.size());
    ui->pageStack->setCurrentWidget(ui->testPage);
    loadWord(0);
}

// ─────────────────────────────────────────────────────────────
// loadWord
// ─────────────────────────────────────────────────────────────
void TestWidget::loadWord(int index)
{
    if (m_words.isEmpty() || index >= m_words.size()) {
        showSummary();
        return;
    }

    const WordInfo &w = m_words[index];

    // 뜻만 표시, 정답 단어는 결과 나올 때까지 숨김
    ui->meaningLabel->setText(w.meaning);
    ui->answerLabel->setText(w.word);
    ui->answerLabel->hide();

    // 영상 플레이어 초기화 (VideoPlayer 연동 전 텍스트로 표시)
    ui->videoPlayer->setText(
        QString("정답/오답 시 [%1] 영상이 재생됩니다").arg(w.word));

    updateProgress();

    ui->resultCard->hide();
    ui->nextBtn->setEnabled(false);
    ui->nextBtn->setText("다음 →");

    m_isRecording    = false;
    m_keypointBuffer = QJsonArray();
    m_stopTimer->stop();

    ui->recordingLabel->hide();
    ui->statusLabel->setText("공수 자세를 취하면 자동으로 녹화가 시작됩니다");

    qDebug() << "[Test] 문제 로드:" << w.meaning
             << "(" << index+1 << "/" << m_words.size() << ")";
}

// ─────────────────────────────────────────────────────────────
// onCameraFrame
// ─────────────────────────────────────────────────────────────
void TestWidget::onCameraFrame(const QImage &frame)
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
void TestWidget::onKeypointFrame(const QJsonObject &keypoint)
{
    bool isGongsu = keypoint["is_gongsu"].toBool();

    if (!m_isRecording) {
        // 쿨다운 중(공수 자세 종료 대기)이면 시작하지 않음
        if (isGongsu && !m_cooldownTimer->isActive())
            startRecording();
        return;
    }

    if (isGongsu && m_recordingStartTime.elapsed() > 1500) {
        stopRecording();
        return;
    }

    m_keypointBuffer.append(keypoint);

    bool hasHand = false;
    for (const auto &joint : keypoint["left_hand"].toArray()) {
        if (joint.toArray()[2].toDouble() > 0.3) { hasHand = true; break; }
    }
    if (!hasHand) {
        for (const auto &joint : keypoint["right_hand"].toArray()) {
            if (joint.toArray()[2].toDouble() > 0.3) { hasHand = true; break; }
        }
    }
    if (hasHand) m_stopTimer->start();
}

// ─────────────────────────────────────────────────────────────
// startRecording
// ─────────────────────────────────────────────────────────────
void TestWidget::startRecording()
{
    if (m_isRecording) return;
    m_isRecording    = true;
    m_keypointBuffer = QJsonArray();
    m_recordingStartTime.start();

    ui->recordingLabel->show();
    ui->statusLabel->setText("녹화 중... 수화를 입력하고 공수 자세로 종료하세요.");
    qDebug() << "[Test] 녹화 시작";
}

// ─────────────────────────────────────────────────────────────
// stopRecording
// ─────────────────────────────────────────────────────────────
void TestWidget::stopRecording()
{
    if (!m_isRecording) return;
    m_isRecording = false;
    m_stopTimer->stop();
    m_cooldownTimer->start();   // 공수 자세 완전히 풀릴 때까지 대기
    ui->recordingLabel->hide();

    int frameCount = m_keypointBuffer.size();
    qDebug() << "[Test] 녹화 종료, 프레임:" << frameCount;

    if (m_words.isEmpty()) return;

    if (frameCount < 3) {
        ui->statusLabel->setText("입력이 너무 짧습니다. 다시 시도해 주세요.");
        return;
    }

    ui->statusLabel->setText("인식 중...");
    const WordInfo &w = m_words[m_currentIndex];
    emit keypointReady(w.id, false, m_keypointBuffer);
}

void TestWidget::onRecordingTimeout()
{
    stopRecording();
}

// ─────────────────────────────────────────────────────────────
// showResult — AppController에서 RES_INFER 수신 시 호출
// ─────────────────────────────────────────────────────────────
void TestWidget::showResult(bool isCorrect, double accuracy, int wordId)
{
    Q_UNUSED(wordId)

    // 정답 단어 공개
    ui->answerLabel->show();

    // 결과 카드 표시
    ui->resultCard->show();
    ui->confidenceLabel->setText(
        QString("신뢰도: %1%").arg(accuracy * 100.0, 0, 'f', 1));

    if (isCorrect) {
        m_correctCount++;
        ui->verdictLabel->setText("✓ 정답!");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#D5F5E3; border-radius:12px; border:1px solid #82E0AA; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#1E8449;");
        ui->statusLabel->setText("정확합니다!");
    } else {
        ui->verdictLabel->setText("✗ 오답");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#FDEDEC; border-radius:12px; border:1px solid #F1948A; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#A93226;");
        ui->statusLabel->setText("아쉽네요. 정답 영상을 확인하세요.");
    }

    // 마지막 문제이면 버튼 텍스트 변경
    bool isLast = (m_currentIndex >= m_words.size() - 1);
    ui->nextBtn->setText(isLast ? "결과 보기" : "다음 →");
    ui->nextBtn->setEnabled(true);

    // ★ 정답/오답 무관하게 영상 자동재생
    triggerVideoPlay();

    qDebug() << "[Test] 결과:" << (isCorrect ? "정답" : "오답")
             << "신뢰도:" << accuracy * 100.0 << "%";
}

// ─────────────────────────────────────────────────────────────
// triggerVideoPlay — 정답/오답 시 영상 자동재생 요청
// VideoPlayer 미구현 상태에서는 videoPlayer 텍스트만 업데이트
// ─────────────────────────────────────────────────────────────
void TestWidget::triggerVideoPlay()
{
    if (m_words.isEmpty() || m_currentIndex >= m_words.size()) return;

    const WordInfo &w = m_words[m_currentIndex];

    // VideoPlayer 연동 전: 텍스트로 상태 표시
    ui->videoPlayer->setText(
        QString("[%1] 영상 재생 중... (VideoPlayer 연동 예정)").arg(w.word));

    // VideoPlayer 연동 후에는 이 시그널로 재생 요청
    emit videoPlayRequested(w.id, m_playSpeed);
}

// ─────────────────────────────────────────────────────────────
// onReplayClicked — 영상 다시 보기
// ─────────────────────────────────────────────────────────────
void TestWidget::onReplayClicked()
{
    triggerVideoPlay();
    qDebug() << "[Test] 영상 다시 보기";
}

// ─────────────────────────────────────────────────────────────
// onSpeedChanged
// ─────────────────────────────────────────────────────────────
void TestWidget::onSpeedChanged()
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
        qDebug() << "[Test] 재생 속도:" << m_playSpeed;
    }
}

// ─────────────────────────────────────────────────────────────
// onNextClicked
// ─────────────────────────────────────────────────────────────
void TestWidget::onNextClicked()
{
    m_currentIndex++;
    if (m_currentIndex >= m_words.size()) {
        showSummary();
    } else {
        loadWord(m_currentIndex);
    }
}

// ─────────────────────────────────────────────────────────────
// onHomeClicked
// ─────────────────────────────────────────────────────────────
void TestWidget::onHomeClicked()
{
    emit testAborted();
}

// ─────────────────────────────────────────────────────────────
// showSummary
// ─────────────────────────────────────────────────────────────
void TestWidget::showSummary()
{
    int total = m_words.size();
    double rate = total > 0 ? (m_correctCount * 100.0 / total) : 0.0;

    ui->summaryScore->setText(
        QString("%1 / %2").arg(m_correctCount).arg(total));
    ui->summaryDetail->setText(
        QString("정답률: %1%").arg(rate, 0, 'f', 1));

    if (rate >= 80.0)
        ui->summaryScore->setStyleSheet(
            "font-size:48px; font-weight:700; color:#1E8449;");
    else if (rate >= 50.0)
        ui->summaryScore->setStyleSheet(
            "font-size:48px; font-weight:700; color:#B7950B;");
    else
        ui->summaryScore->setStyleSheet(
            "font-size:48px; font-weight:700; color:#A93226;");

    ui->testProgress->setValue(total);
    ui->pageStack->setCurrentWidget(ui->summaryPage);

    emit testFinished(m_correctCount, total);
    qDebug() << "[Test] 완료 -" << m_correctCount << "/" << total;
}

// ─────────────────────────────────────────────────────────────
// updateProgress
// ─────────────────────────────────────────────────────────────
void TestWidget::updateProgress()
{
    int total   = m_words.size();
    int current = m_currentIndex + 1;
    ui->testProgress->setValue(current);
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(current).arg(total));
}
