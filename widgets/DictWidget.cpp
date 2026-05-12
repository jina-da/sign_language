#include "DictWidget.h"
#include "ui_DictWidget.h"

#include <QDebug>
#include <QKeyEvent>
#include <QJsonArray>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QScrollArea>

DictWidget::DictWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::DictWidget)
    , m_stopTimer(new QTimer(this))
    , m_cooldownTimer(new QTimer(this))
    , m_countdownTimer(new QTimer(this))
{
    ui->setupUi(this);

    // ── 정지 감지 타이머 (역방향, StudyWidget과 동일) ─
    m_stopTimer->setSingleShot(true);
    m_stopTimer->setInterval(1500);
    connect(m_stopTimer, &QTimer::timeout,
            this,         &DictWidget::onRecordingTimeout);

    // 쿨다운 타이머: 녹화 종료 후 공수 자세가 풀릴 때까지 재시작 방지
    m_cooldownTimer->setSingleShot(true);
    m_cooldownTimer->setInterval(1500);

    // 카운트다운 타이머
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout,
            this,             &DictWidget::onCountdownTick);

    // ── 탭 버튼 ──────────────────────────────────────
    // 초기 상태: noResultPage(0번) 표시
    ui->forwardContentStack->setCurrentIndex(0);

    connect(ui->backToListBtn, &QPushButton::clicked,
            this,              &DictWidget::onBackToListClicked);
    ui->recordBtn->setFixedWidth(110);
    connect(ui->recordBtn, &QPushButton::clicked,
            this,              &DictWidget::onRecordBtnClicked);
    connect(ui->tabForwardBtn, &QPushButton::clicked,
            this,              &DictWidget::onTabForwardClicked);
    connect(ui->tabReverseBtn, &QPushButton::clicked,
            this,              &DictWidget::onTabReverseClicked);

    // ── 정방향 검색 ───────────────────────────────────
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

    // 정방향 탭 초기화
    if (index == 0) {
        clearResults();
            ui->noResultLabel->setText("검색어를 입력하면 결과가 표시됩니다");
    }

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

void DictWidget::onSearchBtnClicked()
{
    onDebounceTimeout();
}

void DictWidget::onDebounceTimeout()
{
    QString query = ui->searchEdit->text().trimmed();
    if (query.isEmpty()) return;

    ui->noResultLabel->setText("검색 중...");
    ui->forwardContentStack->setCurrentIndex(0);   // noResultPage로 전환

    emit forwardSearchRequested(query);
    qDebug() << "[Dict] 정방향 검색:" << query;
}

// ─────────────────────────────────────────────────────────────
// showForwardResult — RES_DICT_SEARCH 수신 후 호출
// ─────────────────────────────────────────────────────────────
void DictWidget::showForwardResult(const QList<DictResult> &results)
{
    clearResults();
    m_currentResults = results;

    if (results.isEmpty()) {
        ui->noResultLabel->setText("검색 결과가 없습니다.");
        ui->forwardContentStack->setCurrentIndex(0);
        return;
    }

    showListPage();
}

// ─────────────────────────────────────────────────────────────
// showListPage — 1페이지: 단어+뜻 목록 카드 (클릭 시 상세로)
// ─────────────────────────────────────────────────────────────
void DictWidget::showListPage()
{
    QLayout *listLayout = ui->listScrollContent->layout();
    QLayoutItem *c;
    while ((c = listLayout->takeAt(0)) != nullptr) {
        if (c->widget()) c->widget()->deleteLater();
        delete c;
    }

    for (int i = 0; i < m_currentResults.size(); i++) {
        const DictResult &r = m_currentResults[i];

        QWidget *card = new QWidget(ui->listScrollContent);
        card->setFixedHeight(80);
        card->setStyleSheet(
            "QWidget { background: white; border-radius: 12px;"
            "  border: 1px solid #C0DD97; }"
            "QWidget:hover { background: #EAF3DE; border-color: #639922; }");
        card->setCursor(Qt::PointingHandCursor);

        auto *innerLayout = new QHBoxLayout(card);
        innerLayout->setContentsMargins(20, 12, 20, 12);
        innerLayout->setSpacing(12);

        auto *wordLbl = new QLabel(r.word, card);
        wordLbl->setStyleSheet(
            "font-size:18px; font-weight:500; color:#27500A; border:none;");
        wordLbl->setMinimumWidth(80);

        auto *divider = new QLabel("|", card);
        divider->setStyleSheet("font-size:16px; color:#C0DD97; border:none;");

        auto *descLbl = new QLabel(r.description, card);
        descLbl->setStyleSheet("font-size:13px; color:#5F5E5A; border:none;");

        auto *arrow = new QLabel("›", card);
        arrow->setStyleSheet("font-size:22px; color:#639922; border:none;");

        innerLayout->addWidget(wordLbl);
        innerLayout->addWidget(divider);
        innerLayout->addWidget(descLbl, 1);
        innerLayout->addWidget(arrow);

        // 클릭 감지: QWidget은 clicked 시그널이 없으므로 이벤트 필터 대신
        // 투명 QPushButton을 위에 올림
        QPushButton *clickOverlay = new QPushButton(card);
        clickOverlay->setGeometry(0, 0, 9999, 80);
        clickOverlay->setStyleSheet("background:transparent; border:none;");
        clickOverlay->raise();
        connect(clickOverlay, &QPushButton::clicked, this, [this, i]{
            showDetailPage(m_currentResults[i]);
        });

        listLayout->addWidget(card);
    }

    // 카드 아래 남은 공간을 stretch로 채움 → 카드가 항상 위에서 시작
    auto *stretch = new QSpacerItem(0, 0,
        QSizePolicy::Minimum, QSizePolicy::Expanding);
    listLayout->addItem(stretch);

    ui->forwardContentStack->setCurrentIndex(1);
    qDebug() << "[Dict] 목록 페이지:" << m_currentResults.size() << "개";
}

// ─────────────────────────────────────────────────────────────
// showDetailPage — 2페이지: 단어+뜻+영상
// ─────────────────────────────────────────────────────────────
void DictWidget::showDetailPage(const DictResult &result)
{
    for (VideoPlayer *vp : m_resultPlayers) vp->deleteLater();
    m_resultPlayers.clear();

    QLayout *scrollLayout = ui->resultScrollContent->layout();
    QLayoutItem *c;
    while ((c = scrollLayout->takeAt(0)) != nullptr) {
        if (c->widget()) c->widget()->deleteLater();
        delete c;
    }

    QWidget *card = new QWidget(ui->resultScrollContent);
    card->setStyleSheet(
        "QWidget { background: white; border-radius: 12px;"
        "  border: 1px solid #C0DD97; }");
    card->setMinimumHeight(360);   // VideoPlayer(270) + 컨트롤바(36) + 패딩(28) + 여유

    auto *cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(16, 14, 16, 14);
    cardLayout->setSpacing(16);

    auto *infoCol = new QVBoxLayout;
    infoCol->setSpacing(8);

    auto *wordLbl = new QLabel(result.word, card);
    wordLbl->setStyleSheet(
        "font-size:28px; font-weight:500; color:#27500A; border:none;");

    auto *descLbl = new QLabel(result.description, card);
    descLbl->setWordWrap(true);
    descLbl->setStyleSheet("font-size:14px; color:#5F5E5A; border:none;");

    infoCol->addWidget(wordLbl);
    infoCol->addWidget(descLbl);
    infoCol->addStretch();
    cardLayout->addLayout(infoCol, 1);

    if (!result.videoCdnUrl.isEmpty()) {
        VideoPlayer *vp = new VideoPlayer(card);
        vp->setSession(m_serverHost, m_sessionToken);
        vp->setFixedSize(360, 270);
        QString filename = result.videoCdnUrl.split("/").last()
                                             .split("?").first();
        vp->play(result.videoCdnUrl, filename);
        cardLayout->addWidget(vp);
        m_resultPlayers.append(vp);
    }

    scrollLayout->addWidget(card);
    ui->forwardContentStack->setCurrentIndex(2);
    qDebug() << "[Dict] 상세 페이지:" << result.word;
}


void DictWidget::onBackToListClicked()
{
    // 2페이지 → 1페이지로 복귀
    // VideoPlayer 정리
    for (VideoPlayer *vp : m_resultPlayers) vp->deleteLater();
    m_resultPlayers.clear();

    ui->forwardContentStack->setCurrentIndex(1);
    qDebug() << "[Dict] 목록 페이지로 복귀";
}

void DictWidget::clearResults()
{
    // 이전 결과 VideoPlayer 삭제
    for (VideoPlayer *vp : m_resultPlayers)
        vp->deleteLater();
    m_resultPlayers.clear();

    // listScrollLayout 초기화
    if (ui->listScrollContent->layout()) {
        QLayout *ll = ui->listScrollContent->layout();
        QLayoutItem *lc;
        while ((lc = ll->takeAt(0)) != nullptr) {
            if (lc->widget()) lc->widget()->deleteLater();
            delete lc;
        }
    }
    // resultScrollLayout의 동적 위젯 모두 제거
    QLayout *layout = ui->resultScrollContent->layout();
    if (layout) {
        QLayoutItem *child;
        while ((child = layout->takeAt(0)) != nullptr) {
            if (child->widget())
                child->widget()->deleteLater();
            delete child;
        }
    }

    // ScrollArea 높이 제한 해제
    ui->resultScrollArea->setMinimumHeight(0);
    ui->resultScrollArea->setMaximumHeight(16777215);

    // noResultPage(0번)로 전환
    ui->forwardContentStack->setCurrentIndex(0);
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
        clearResults();
        ui->noResultLabel->setText(message);
        ui->forwardContentStack->setCurrentIndex(0);
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
    if (!m_isRecording) return;

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
    ui->recordingLabel->hide();
    ui->recordBtn->setText("⏺ 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");

    int frameCount = m_keypointBuffer.size();
    qDebug() << "[Dict] 역방향 녹화 종료, 프레임:" << frameCount;

    if (frameCount < 3) {
        ui->statusLabel->setText("입력이 너무 짧습니다. 다시 시도해 주세요.");
        return;
    }

    ui->statusLabel->setText("인식 중...");
    emit reverseSearchRequested(m_keypointBuffer);
}


void DictWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        onRecordBtnClicked(); return;
    }
    QWidget::keyPressEvent(event);
}

void DictWidget::onRecordBtnClicked()
{
    if (m_countdownTimer->isActive()) {
        m_countdownTimer->stop(); m_countdown = 0;
        ui->statusLabel->setText("녹화가 취소됐습니다.");
        ui->recordBtn->setText("⏺ 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
        return;
    }
    if (m_isRecording) {
        m_isRecording = false; m_stopTimer->stop();
        m_keypointBuffer = QJsonArray();
        ui->recordingLabel->hide();
        ui->recordBtn->setText("⏺ 녹화");
    ui->recordBtn->setStyleSheet("QPushButton { background: #3B6D11; color: white; border: none; border-radius: 20px; font-size: 13px; font-weight: 500; padding: 8px 24px; min-width: 100px; }");
        ui->statusLabel->setText("녹화가 중단됐습니다.");
        qDebug() << "[Dict] 녹화 중단";
        return;
    }
    m_countdown = 3;
    ui->statusLabel->setText("3초 후 녹화 시작...");
    ui->recordBtn->setText("■ 취소");
    m_countdownTimer->start();
    qDebug() << "[Dict] 카운트다운 시작";
}

void DictWidget::onCountdownTick()
{
    m_countdown--;
    ui->statusLabel->setText(QString("%1초 후 녹화 시작...").arg(m_countdown));
    if (m_countdown <= 0) {
        m_countdownTimer->stop();
        startRecording();
    }
}
void DictWidget::onRecordingTimeout() { stopRecording(); }
