#include "DictWidget.h"
#include "ui_DictWidget.h"

#include <QDebug>
#include <QJsonArray>

DictWidget::DictWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DictWidget)
    , m_debounceTimer(new QTimer(this))
    , m_stopTimer(new QTimer(this))
    , m_cooldownTimer(new QTimer(this))
{
    ui->setupUi(this);

    // ── 디바운스 타이머: 300ms 후 검색 실행 ─────────
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(300);
    connect(m_debounceTimer, &QTimer::timeout,
            this,            &DictWidget::onDebounceTimeout);

    // ── 정지 감지 타이머 (역방향, StudyWidget과 동일) ─
    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,         &DictWidget::onRecordingTimeout);

    // 쿨다운 타이머: 녹화 종료 후 공수 자세가 풀릴 때까지 재시작 방지
    m_cooldownTimer->setSingleShot(true);
    m_cooldownTimer->setInterval(1500);   // 1.5초간 재시작 차단

    // ── 탭 버튼 ──────────────────────────────────────
    connect(ui->tabForwardBtn, &QPushButton::clicked,
            this,              &DictWidget::onTabForwardClicked);
    connect(ui->tabReverseBtn, &QPushButton::clicked,
            this,              &DictWidget::onTabReverseClicked);

    // ── 정방향 검색 ───────────────────────────────────
    connect(ui->searchEdit, &QLineEdit::textChanged,
            this,           &DictWidget::onSearchTextChanged);
    connect(ui->searchBtn,  &QPushButton::clicked,
            this,           &DictWidget::onSearchBtnClicked);
    connect(ui->searchEdit, &QLineEdit::returnPressed,
            this,           &DictWidget::onSearchBtnClicked);

    // 초기 탭: 정방향
    switchTab(0);
}

DictWidget::~DictWidget()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// switchTab — 탭 전환 및 버튼 스타일 갱신
// ─────────────────────────────────────────────────────────────
void DictWidget::switchTab(int index)
{
    m_currentTab = index;
    ui->dictStack->setCurrentIndex(index);

    // 활성 탭: 초록 강조 / 비활성 탭: 회색
    const QString active   = "font-size:13px; color:#3B6D11; font-weight:500;"
                             "border:none; border-bottom:2px solid #3B6D11;"
                             "padding:0 20px; background:transparent;";
    const QString inactive = "font-size:13px; color:#5F5E5A;"
                             "border:none; border-bottom:2px solid transparent;"
                             "padding:0 20px; background:transparent;";

    ui->tabForwardBtn->setStyleSheet(index == 0 ? active : inactive);
    ui->tabReverseBtn->setStyleSheet(index == 1 ? active : inactive);

    // 역방향 탭 진입 시 녹화 상태 초기화
    if (index == 1) {
        m_isRecording    = false;
        m_keypointBuffer = QJsonArray();
        m_stopTimer->stop();
        ui->recordingLabel->hide();
        ui->statusLabel->setText("공수 자세를 취하면 자동으로 시작됩니다");
        ui->reverseResultCard->hide();
    }

    qDebug() << "[Dict] 탭 전환:" << (index == 0 ? "정방향" : "역방향");
}

void DictWidget::onTabForwardClicked() { switchTab(0); }
void DictWidget::onTabReverseClicked() { switchTab(1); }

// ─────────────────────────────────────────────────────────────
// 정방향: 텍스트 변경 → 디바운스 타이머 리셋
// ─────────────────────────────────────────────────────────────
void DictWidget::onSearchTextChanged(const QString &text)
{
    if (text.trimmed().isEmpty()) {
        m_debounceTimer->stop();
        ui->resultCard->hide();
        ui->noResultLabel->setText("검색어를 입력하면 결과가 표시됩니다");
        ui->noResultLabel->show();
        return;
    }
    // 입력할 때마다 타이머 재시작 → 마지막 입력 후 300ms 뒤 검색
    m_debounceTimer->start();
}

void DictWidget::onSearchBtnClicked()
{
    m_debounceTimer->stop();
    onDebounceTimeout();
}

void DictWidget::onDebounceTimeout()
{
    QString query = ui->searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    ui->noResultLabel->setText("검색 중...");
    ui->noResultLabel->show();
    ui->resultCard->hide();

    emit forwardSearchRequested(query);
    qDebug() << "[Dict] 정방향 검색:" << query;
}

// ─────────────────────────────────────────────────────────────
// showForwardResult — RES_DICT_SEARCH 수신 후 호출
// ─────────────────────────────────────────────────────────────
void DictWidget::showForwardResult(const QString &word,
                                   const QString &description,
                                   const QString &videoCdnUrl)
{
    ui->noResultLabel->hide();
    ui->resultCard->show();

    ui->resultWordLabel->setText(word);
    ui->resultDescLabel->setText(description);

    // VideoPlayer 연동 전: CDN URL을 텍스트로 표시
    if (videoCdnUrl.isEmpty()) {
        ui->resultVideoLabel->setText("영상 준비 중...");
    } else {
        ui->resultVideoLabel->setText(
            QString("영상 URL:\n%1\n(VideoPlayer 연동 예정)").arg(videoCdnUrl));
    }

    qDebug() << "[Dict] 정방향 결과:" << word << "/" << description;
}

// ─────────────────────────────────────────────────────────────
// showReverseResult — RES_DICT_REVERSE 수신 후 호출
// ─────────────────────────────────────────────────────────────
void DictWidget::showReverseResult(const QString &word,
                                   const QString &description)
{
    ui->reverseResultCard->show();
    ui->reverseWordLabel->setText(word);
    ui->reverseDescLabel->setText(description);
    ui->statusLabel->setText("인식 완료! 다시 수화를 입력하면 새로 검색합니다.");
    qDebug() << "[Dict] 역방향 결과:" << word;
}

// ─────────────────────────────────────────────────────────────
// showSearchError
// ─────────────────────────────────────────────────────────────
void DictWidget::showSearchError(const QString &message)
{
    if (m_currentTab == 0) {
        ui->resultCard->hide();
        ui->noResultLabel->setText(message);
        ui->noResultLabel->show();
    } else {
        ui->statusLabel->setText(message);
    }
    qWarning() << "[Dict] 검색 오류:" << message;
}

// ─────────────────────────────────────────────────────────────
// onCameraFrame
// ─────────────────────────────────────────────────────────────
void DictWidget::onCameraFrame(const QImage &frame)
{
    // 역방향 탭에서만 카메라 표시
    if (m_currentTab != 1) return;
    ui->cameraView->setPixmap(
        QPixmap::fromImage(frame).scaled(
            ui->cameraView->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
}

// ─────────────────────────────────────────────────────────────
// onKeypointFrame — 역방향 탭에서만 처리
// ─────────────────────────────────────────────────────────────
void DictWidget::onKeypointFrame(const QJsonObject &keypoint)
{
    if (m_currentTab != 1) return;

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

    // 1.5초 정지 감지
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
void DictWidget::startRecording()
{
    if (m_isRecording) return;
    m_isRecording    = true;
    m_keypointBuffer = QJsonArray();
    m_recordingStartTime.start();

    ui->recordingLabel->show();
    ui->statusLabel->setText("녹화 중... 수화를 입력하고 공수 자세로 종료하세요.");
    ui->reverseResultCard->hide();
    qDebug() << "[Dict] 역방향 녹화 시작";
}

// ─────────────────────────────────────────────────────────────
// stopRecording
// ─────────────────────────────────────────────────────────────
void DictWidget::stopRecording()
{
    if (!m_isRecording) return;
    m_isRecording = false;
    m_stopTimer->stop();
    m_cooldownTimer->start();   // 공수 자세 완전히 풀릴 때까지 대기
    ui->recordingLabel->hide();

    int frameCount = m_keypointBuffer.size();
    qDebug() << "[Dict] 역방향 녹화 종료, 프레임:" << frameCount;

    if (frameCount < 3) {
        ui->statusLabel->setText("입력이 너무 짧습니다. 다시 시도해 주세요.");
        return;
    }

    ui->statusLabel->setText("인식 중...");
    emit reverseSearchRequested(m_keypointBuffer);
}

void DictWidget::onRecordingTimeout() { stopRecording(); }
