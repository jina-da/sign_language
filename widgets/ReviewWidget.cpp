#include "ReviewWidget.h"
#include "ui_StudyWidget.h"   // StudyWidget.ui 재사용

#include <QButtonGroup>
#include <QDebug>
#include <QJsonArray>

ReviewWidget::ReviewWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::StudyWidget)
    , m_stopTimer(new QTimer(this))
    , m_speedGroup(new QButtonGroup(this))
    , m_cooldownTimer(new QTimer(this))
{
    ui->setupUi(this);

    // 헤더 텍스트 → "복습 모드"
    ui->wordCountLabel->setText("복습 모드");

    ui->prevBtn->setFocusPolicy(Qt::NoFocus);
    ui->nextBtn->setFocusPolicy(Qt::NoFocus);
    ui->skipBtn->setFocusPolicy(Qt::NoFocus);
    ui->replayBtn->setFocusPolicy(Qt::NoFocus);
    ui->speed025->setFocusPolicy(Qt::NoFocus);
    ui->speed050->setFocusPolicy(Qt::NoFocus);
    ui->speed100->setFocusPolicy(Qt::NoFocus);
    ui->speed150->setFocusPolicy(Qt::NoFocus);
    ui->speed200->setFocusPolicy(Qt::NoFocus);

    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,        &ReviewWidget::onRecordingTimeout);

    // 쿨다운 타이머: 녹화 종료 후 공수 자세가 풀릴 때까지 재시작 방지
    m_cooldownTimer->setSingleShot(true);
    m_cooldownTimer->setInterval(1500);   // 1.5초간 재시작 차단

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
    connect(ui->replayBtn, &QPushButton::clicked,
            this,          &ReviewWidget::onReplayClicked);
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

    ui->resultCard->hide();
    ui->nextBtn->setEnabled(false);
    ui->prevBtn->setEnabled(false);
    ui->skipBtn->setEnabled(false);
    ui->statusLabel->setText(message);
    ui->recordingLabel->hide();

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

    ui->resultCard->hide();
    ui->nextBtn->setEnabled(false);
    ui->nextBtn->setText("다음 단어 →");
    ui->prevBtn->setEnabled(index > 0);

    m_isRecording    = false;
    m_keypointBuffer = QJsonArray();
    m_stopTimer->stop();

    ui->recordingLabel->hide();
    ui->statusLabel->setText("공수 자세를 취하면 자동으로 녹화가 시작됩니다");
    ui->videoPlayer->setText(QString("[%1] 영상 로딩 예정").arg(w.word));

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
void ReviewWidget::startRecording()
{
    if (m_isRecording) return;
    m_isRecording    = true;
    m_keypointBuffer = QJsonArray();
    m_recordingStartTime.start();

    ui->recordingLabel->show();
    ui->statusLabel->setText("녹화 중... 수화를 입력하고 공수 자세로 종료하세요.");
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
    m_cooldownTimer->start();   // 공수 자세 완전히 풀릴 때까지 대기
    ui->recordingLabel->hide();

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

void ReviewWidget::onRecordingTimeout() { stopRecording(); }

// ─────────────────────────────────────────────────────────────
// showResult
// ─────────────────────────────────────────────────────────────
void ReviewWidget::showResult(const QString &verdict,
                               double confidence,
                               int predictedWordId)
{
    Q_UNUSED(predictedWordId)

    ui->resultCard->show();
    ui->confidenceLabel->setText(
        QString("신뢰도: %1%").arg(confidence, 0, 'f', 1));

    applyVerdictStyle(verdict);

    bool isCorrect = (verdict == "correct");
    ui->nextBtn->setEnabled(true);
    ui->nextBtn->setText(isCorrect ? "다음 단어 →" : "다시 시도");

    if (isCorrect)
        ui->statusLabel->setText("정확합니다! 다음 단어로 이동하세요.");
    else if (verdict == "partial")
        ui->statusLabel->setText("거의 맞았습니다! 다시 시도하거나 넘어가세요.");
    else
        ui->statusLabel->setText("다시 시도해 보세요.");
}

// ─────────────────────────────────────────────────────────────
// applyVerdictStyle
// ─────────────────────────────────────────────────────────────
void ReviewWidget::applyVerdictStyle(const QString &verdict)
{
    if (verdict == "correct") {
        ui->verdictLabel->setText("✓ 정답!");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#D5F5E3; border-radius:12px; border:1px solid #82E0AA; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#1E8449;");
    } else if (verdict == "partial") {
        ui->verdictLabel->setText("△ 거의 맞았어요");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#FEF9E7; border-radius:12px; border:1px solid #F9E79F; }");
        ui->verdictLabel->setStyleSheet(
            "font-size:20px; font-weight:500; color:#B7950B;");
    } else {
        ui->verdictLabel->setText("✗ 다시 시도");
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
    ui->prevBtn->setEnabled(false);
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
