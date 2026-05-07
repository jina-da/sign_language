#include "StudyWidget.h"
#include "ui_StudyWidget.h"

#include <QButtonGroup>
#include <QDebug>

StudyWidget::StudyWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::StudyWidget)
    , m_stopTimer(new QTimer(this))
    , m_speedGroup(new QButtonGroup(this))
{
    ui->setupUi(this);

    // ── 1.5초 정지 감지 타이머 ───────────────────────
    // MediaPipe에서 손이 정지됐다고 판단되면 이 타이머를 (재)시작.
    // 1500ms 뒤에 onRecordingTimeout() 호출 → 녹화 종료 + 전송
    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,        &StudyWidget::onRecordingTimeout);

    // ── 속도 버튼 단일 선택 그룹 ─────────────────────
    m_speedGroup->addButton(ui->speed025);
    m_speedGroup->addButton(ui->speed050);
    m_speedGroup->addButton(ui->speed100);
    m_speedGroup->addButton(ui->speed150);
    m_speedGroup->addButton(ui->speed200);
    m_speedGroup->setExclusive(true);

    connect(m_speedGroup, &QButtonGroup::buttonClicked,
            this,         &StudyWidget::onSpeedChanged);

    // ── 버튼 시그널 ───────────────────────────────────
    connect(ui->nextBtn,  &QPushButton::clicked,
            this,         &StudyWidget::onNextClicked);
    connect(ui->skipBtn,  &QPushButton::clicked,
            this,         &StudyWidget::onSkipClicked);
    connect(ui->replayBtn,&QPushButton::clicked,
            this,         &StudyWidget::onReplayClicked);
}

StudyWidget::~StudyWidget()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// setWordList — 서버에서 받은 단어 목록 세팅
// AppController에서 word_list_result 수신 후 호출
// ─────────────────────────────────────────────────────────────
void StudyWidget::setWordList(const QList<WordInfo> &words)
{
    m_words        = words;
    m_currentIndex = 0;
    ui->studyProgress->setMaximum(words.size());
    loadWord(0);
}

// ─────────────────────────────────────────────────────────────
// loadWord — 현재 단어를 화면에 표시
// ─────────────────────────────────────────────────────────────
void StudyWidget::loadWord(int index)
{
    if (index >= m_words.size()) {
        emit studyFinished();
        return;
    }

    const WordInfo &w = m_words[index];

    // 단어 정보 표시
    ui->wordLabel->setText(w.word);
    ui->categoryLabel->setText(w.category);

    // 진도 업데이트
    updateProgress();

    // 결과 카드 숨기기, 다음 버튼 비활성화
    ui->resultCard->hide();
    ui->nextBtn->setEnabled(false);

    // 녹화 상태 초기화
    m_isRecording = false;
    m_keypointBuffer = QJsonArray();
    m_stopTimer->stop();

    ui->recordingLabel->hide();
    ui->statusLabel->setText("공수 자세를 취하면 자동으로 시작됩니다");

    // TODO: 영상 재생 (SYN 아바타 영상)
    // VideoPlayer를 연동하면 아래처럼 호출:
    // m_videoPlayer->load(w.videoPath);
    // m_videoPlayer->setSpeed(m_playSpeed);
    // m_videoPlayer->play();
    ui->videoPlayer->setText(
        QString("[%1] 영상 로딩 예정\n(VideoPlayer 연동 후 자동 재생)").arg(w.word));

    qDebug() << "[Study] 단어 로드:" << w.word << "(" << index+1 << "/" << m_words.size() << ")";
}

// ─────────────────────────────────────────────────────────────
// onCameraFrame — 카메라 프레임 수신 (KeypointExtractor가 호출)
// 실제 공수 감지·관절 추출은 KeypointExtractor에서 처리.
// 이 함수는 UI 표시와 버퍼 누적만 담당.
// ─────────────────────────────────────────────────────────────
void StudyWidget::onCameraFrame(const QImage &frame)
{
    // 카메라 뷰에 프레임 표시
    ui->cameraView->setPixmap(
        QPixmap::fromImage(frame).scaled(
            ui->cameraView->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

// ─────────────────────────────────────────────────────────────
// startRecording — 공수 자세 감지 시 KeypointExtractor가 호출
// ─────────────────────────────────────────────────────────────
void StudyWidget::startRecording()
{
    if (m_isRecording) return;

    m_isRecording    = true;
    m_keypointBuffer = QJsonArray();

    ui->recordingLabel->show();
    ui->statusLabel->setText("수화를 입력하세요...");

    qDebug() << "[Study] 녹화 시작";
}

// ─────────────────────────────────────────────────────────────
// stopRecording — 1.5초 정지 타이머 만료 시 호출
// keypointReady 시그널로 AppController에 키포인트 전달
// ─────────────────────────────────────────────────────────────
void StudyWidget::stopRecording()
{
    if (!m_isRecording) return;
    m_isRecording = false;
    m_stopTimer->stop();

    ui->recordingLabel->hide();
    ui->statusLabel->setText("인식 중...");

    // 프레임 수 검증 (3~300 프레임)
    int frameCount = m_keypointBuffer.size();
    if (frameCount < 3 || frameCount > 300) {
        qWarning() << "[Study] 프레임 수 오류:" << frameCount;
        ui->statusLabel->setText("수화를 다시 입력해 주세요.");
        return;
    }

    const WordInfo &w = m_words[m_currentIndex];
    qDebug() << "[Study] 녹화 종료, 프레임:" << frameCount << "→ 전송";

    // AppController가 이 시그널을 받아 서버로 전송
    emit keypointReady(w.id, false, m_keypointBuffer);
}

// ─────────────────────────────────────────────────────────────
// onRecordingTimeout — 1.5초 정지 타이머 만료
// ─────────────────────────────────────────────────────────────
void StudyWidget::onRecordingTimeout()
{
    stopRecording();
}

// ─────────────────────────────────────────────────────────────
// showResult — 서버 keypoint_result 수신 후 AppController가 호출
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

    // 정답이면 다음 버튼 활성화
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
// applyVerdictStyle — 판정에 따라 결과 카드 색상 변경 (동적)
// ─────────────────────────────────────────────────────────────
void StudyWidget::applyVerdictStyle(const QString &verdict)
{
    if (verdict == "correct") {
        ui->verdictLabel->setText("✓ 정답!");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#D5F5E3; border-radius:12px; border:1px solid #82E0AA; }");
        ui->verdictLabel->setStyleSheet("font-size:20px; font-weight:500; color:#1E8449;");
    } else if (verdict == "partial") {
        ui->verdictLabel->setText("△ 거의 맞았어요");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#FEF9E7; border-radius:12px; border:1px solid #F9E79F; }");
        ui->verdictLabel->setStyleSheet("font-size:20px; font-weight:500; color:#B7950B;");
    } else {
        ui->verdictLabel->setText("✗ 다시 시도");
        ui->resultCard->setStyleSheet(
            "QWidget#resultCard { background:#FDEDEC; border-radius:12px; border:1px solid #F1948A; }");
        ui->verdictLabel->setStyleSheet("font-size:20px; font-weight:500; color:#A93226;");
    }
}

// ─────────────────────────────────────────────────────────────
// updateProgress — 상단 진도 바 업데이트
// ─────────────────────────────────────────────────────────────
void StudyWidget::updateProgress()
{
    int total   = m_words.size();
    int current = m_currentIndex + 1;
    ui->studyProgress->setValue(m_currentIndex);
    ui->wordIndexLabel->setText(QString("%1 / %2").arg(current).arg(total));
}

// ─────────────────────────────────────────────────────────────
// 버튼 슬롯
// ─────────────────────────────────────────────────────────────
void StudyWidget::onNextClicked()
{
    // "다시 시도" 상태면 같은 단어 재시도
    if (ui->nextBtn->text() == "다시 시도") {
        loadWord(m_currentIndex);
        return;
    }

    // 다음 단어
    m_currentIndex++;
    if (m_currentIndex >= m_words.size()) {
        emit studyFinished();
    } else {
        loadWord(m_currentIndex);
    }
}

void StudyWidget::onSkipClicked()
{
    const WordInfo &w = m_words[m_currentIndex];
    emit wordSkipped(w.id);
    m_currentIndex++;
    if (m_currentIndex >= m_words.size())
        emit studyFinished();
    else
        loadWord(m_currentIndex);
}

void StudyWidget::onReplayClicked()
{
    // TODO: VideoPlayer 연동 후 재생
    qDebug() << "[Study] 영상 다시 보기";
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
    auto *checked = qobject_cast<QPushButton*>(m_speedGroup->checkedButton());
    if (checked && speedMap.contains(checked)) {
        m_playSpeed = speedMap[checked];
        qDebug() << "[Study] 재생 속도:" << m_playSpeed;
        // TODO: VideoPlayer에 속도 적용
    }
}
