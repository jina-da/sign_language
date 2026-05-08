#include "StudyWidget.h"
#include "ui_StudyWidget.h"

#include <QButtonGroup>
#include <QDebug>
#include <QJsonArray>
#include <QMessageBox>
#include <QPushButton>

StudyWidget::StudyWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::StudyWidget)
    , m_stopTimer(new QTimer(this))
    , m_speedGroup(new QButtonGroup(this))
{
    ui->setupUi(this);

    // 버튼들이 스페이스바로 클릭되지 않도록 포커스 정책 제거
    ui->prevBtn->setFocusPolicy(Qt::NoFocus);
    ui->nextBtn->setFocusPolicy(Qt::NoFocus);
    ui->skipBtn->setFocusPolicy(Qt::NoFocus);
    ui->replayBtn->setFocusPolicy(Qt::NoFocus);
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
    connect(ui->replayBtn, &QPushButton::clicked,
            this,          &StudyWidget::onReplayClicked);
}

StudyWidget::~StudyWidget()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// setWordList
// ─────────────────────────────────────────────────────────────
void StudyWidget::setWordList(const QList<WordInfo> &words)
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

    ui->resultCard->hide();
    ui->nextBtn->setEnabled(false);
    ui->nextBtn->setText("다음 단어 →");

    // 이전 단어 버튼: 첫 번째 단어이면 비활성화
    ui->prevBtn->setEnabled(index > 0);

    m_isRecording    = false;
    m_keypointBuffer = QJsonArray();
    m_stopTimer->stop();

    ui->recordingLabel->hide();
    ui->statusLabel->setText(
        "공수 자세를 취하면 자동으로 녹화가 시작됩니다");

    ui->videoPlayer->setText(
        QString("[%1] 영상 로딩 예정").arg(w.word));

    qDebug() << "[Study] 단어 로드:" << w.word
             << "(" << index+1 << "/" << m_words.size() << ")";
}

// ─────────────────────────────────────────────────────────────
// onCameraFrame — 카메라 프레임 표시
// ─────────────────────────────────────────────────────────────
void StudyWidget::onCameraFrame(const QImage &frame)
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
void StudyWidget::onKeypointFrame(const QJsonObject &keypoint)
{
    bool isGongsu = keypoint["is_gongsu"].toBool();

    if (!m_isRecording) {
        if (isGongsu) {
            startRecording();
        }
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

    if (hasHand)
        m_stopTimer->start();
}

// ─────────────────────────────────────────────────────────────
// startRecording
// ─────────────────────────────────────────────────────────────
void StudyWidget::startRecording()
{
    if (m_isRecording) return;

    m_isRecording    = true;
    m_keypointBuffer = QJsonArray();
    m_recordingStartTime.start();

    ui->recordingLabel->show();
    ui->statusLabel->setText("녹화 중... 수화를 입력하고 공수 자세로 종료하세요.");

    qDebug() << "[Study] 녹화 시작";
}

// ─────────────────────────────────────────────────────────────
// stopRecording
// ─────────────────────────────────────────────────────────────
void StudyWidget::stopRecording()
{
    if (!m_isRecording) return;
    m_isRecording = false;
    m_stopTimer->stop();

    ui->recordingLabel->hide();

    int frameCount = m_keypointBuffer.size();
    qDebug() << "[Study] 녹화 종료, 누적 프레임:" << frameCount;

    if (m_words.isEmpty()) {
        qWarning() << "[Study] 단어 목록 없음 - 전송 취소";
        ui->statusLabel->setText("단어 목록을 불러온 후 다시 시도해 주세요.");
        return;
    }

    if (frameCount < 3) {
        qDebug() << "[Study] 프레임 부족 → 더미 키포인트 전송";
        sendDummyKeypoint();
        return;
    }

    ui->statusLabel->setText("인식 중...");
    const WordInfo &w = m_words[m_currentIndex];
    emit keypointReady(w.id, false, m_keypointBuffer);
}

// ─────────────────────────────────────────────────────────────
// sendDummyKeypoint
// ─────────────────────────────────────────────────────────────
void StudyWidget::sendDummyKeypoint()
{
    if (m_words.isEmpty()) {
        qWarning() << "[Study] 단어 목록 없음 - 더미 전송 취소";
        ui->statusLabel->setText("단어 목록을 불러온 후 다시 시도해 주세요.");
        return;
    }

    auto makeJoint = [](double x, double y, double c) {
        QJsonArray j; j.append(x); j.append(y); j.append(c); return j;
    };
    auto makeJoints = [&](int count) {
        QJsonArray arr;
        for (int i = 0; i < count; i++) arr.append(makeJoint(0.5, 0.5, 1.0));
        return arr;
    };

    QJsonArray frames;
    for (int f = 0; f < 10; f++) {
        QJsonObject frame;
        frame["frame_idx"]  = f;
        frame["left_hand"]  = makeJoints(21);
        frame["right_hand"] = makeJoints(21);
        frame["pose"]       = makeJoints(25);
        frames.append(frame);
    }

    ui->statusLabel->setText("인식 중... (더미 데이터)");
    emit keypointReady(m_words[m_currentIndex].id, false, frames);
}

// ─────────────────────────────────────────────────────────────
// onRecordingTimeout
// ─────────────────────────────────────────────────────────────
void StudyWidget::onRecordingTimeout()
{
    stopRecording();
}

// ─────────────────────────────────────────────────────────────
// showResult
// ─────────────────────────────────────────────────────────────
void StudyWidget::showResult(const QString &verdict,
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
        ui->statusLabel->setText("다시 시도해 보세요. [스페이스바]로 재녹화");
}

// ─────────────────────────────────────────────────────────────
// applyVerdictStyle
// ─────────────────────────────────────────────────────────────
void StudyWidget::applyVerdictStyle(const QString &verdict)
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
// updateProgress  ★ 버그 수정: m_currentIndex+1 로 변경
// ─────────────────────────────────────────────────────────────
void StudyWidget::updateProgress()
{
    int total   = m_words.size();
    int current = m_currentIndex + 1;

    // ★ 수정 전: setValue(m_currentIndex)  → 마지막 단어에서 90%만 참
    // ★ 수정 후: setValue(m_currentIndex + 1) → 정확히 현재 단어 번호만큼 참
    ui->studyProgress->setValue(current);
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(current).arg(total));
}

// ─────────────────────────────────────────────────────────────
// onPrevClicked  ★ 신규: 이전 단어로 이동
// ─────────────────────────────────────────────────────────────
void StudyWidget::onPrevClicked()
{
    if (m_currentIndex <= 0) return;
    m_currentIndex--;
    loadWord(m_currentIndex);
}

// ─────────────────────────────────────────────────────────────
// onNextClicked  ★ 수정: 마지막 단어 후 완료 팝업
// ─────────────────────────────────────────────────────────────
void StudyWidget::onNextClicked()
{
    if (ui->nextBtn->text() == "다시 시도") {
        loadWord(m_currentIndex);
        return;
    }

    m_currentIndex++;

    if (m_currentIndex >= m_words.size()) {
        showCompletionDialog();
        return;
    }

    loadWord(m_currentIndex);
}

// ─────────────────────────────────────────────────────────────
// showCompletionDialog  ★ 신규: 모든 단어 완료 시 팝업
// ─────────────────────────────────────────────────────────────
void StudyWidget::showCompletionDialog()
{
    // 진도 바 100% 로 채우기
    ui->studyProgress->setValue(m_words.size());
    ui->wordIndexLabel->setText(
        QString("%1 / %2").arg(m_words.size()).arg(m_words.size()));

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("학습 완료 🎉");
    msgBox.setText(
        QString("오늘의 단어 %1개를 모두 학습했습니다!\n"
                "테스트 화면은 추후 구현 예정입니다.")
        .arg(m_words.size()));
    msgBox.setIcon(QMessageBox::Information);

    QPushButton *homeBtn   = msgBox.addButton("홈으로",   QMessageBox::AcceptRole);
    QPushButton *reviewBtn = msgBox.addButton("처음부터", QMessageBox::RejectRole);
    Q_UNUSED(reviewBtn)

    msgBox.exec();

    if (msgBox.clickedButton() == homeBtn) {
        emit studyFinished();
    } else {
        m_currentIndex = 0;
        loadWord(0);
    }
}

// ─────────────────────────────────────────────────────────────
// onSkipClicked
// ─────────────────────────────────────────────────────────────
void StudyWidget::onSkipClicked()
{
    if (m_words.isEmpty() || m_currentIndex >= m_words.size())
        return;

    emit wordSkipped(m_words[m_currentIndex].id);
    m_currentIndex++;

    // 마지막 단어를 건너뛴 경우 → loadWord 내부에서 studyFinished emit 후 리턴되므로,
    // 완료 팝업은 onNextClicked 와 동일한 흐름인 showCompletionDialog()로 공통 처리
    if (m_currentIndex >= m_words.size()) {
        showCompletionDialog();
        return;
    }

    loadWord(m_currentIndex);
}

void StudyWidget::onReplayClicked()
{
    qDebug() << "[Study] 영상 다시 보기 (VideoPlayer 연동 예정)";
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
        qDebug() << "[Study] 재생 속도:" << m_playSpeed;
    }
}
