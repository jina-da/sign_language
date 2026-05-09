#include "MainWindow.h"
#include "AppStyle.h"
#include "ui_MainWindow.h"
#include "ReviewWidget.h"
#include "DictWidget.h"
#include "SettingsWidget.h"

#include <QDebug>
#include <QMenu>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::MainWindow)
    , m_kpClient(new KeypointClient(this))
{
    qDebug() << "[Main] setupUi start";
    ui->setupUi(this);
    qDebug() << "[Main] setupUi done";

    setupNavButtons();
    setupModeCards();

    // ── 학습 탭(인덱스 1)에 StudyWidget 삽입 ────────
    m_studyWidget = new StudyWidget(this);
    ui->contentStack->removeWidget(ui->studyTab);
    ui->contentStack->insertWidget(1, m_studyWidget);

    // ── 복습 탭(인덱스 2)에 ReviewWidget 삽입 ────────
    m_reviewWidget = new ReviewWidget(this);
    ui->contentStack->removeWidget(ui->reviewTab);
    ui->contentStack->insertWidget(2, m_reviewWidget);

    // ── 사전 탭(인덱스 3)에 DictWidget 삽입 ──────────
    m_dictWidget = new DictWidget(this);
    ui->contentStack->removeWidget(ui->dictTab);
    ui->contentStack->insertWidget(3, m_dictWidget);

    // ── 설정 탭(인덱스 5)에 SettingsWidget 삽입 ─────
    m_settingsWidget = new SettingsWidget(this);
    ui->contentStack->removeWidget(ui->settingsTab);
    ui->contentStack->insertWidget(5, m_settingsWidget);

    // ── 테스트 탭(네비 없음)으로 TestWidget 추가 ─────
    m_testWidget = new TestWidget(this);
    ui->contentStack->addWidget(m_testWidget);

    // ── KeypointClient → 프레임/키포인트: 현재 보이는 위젯만 처리 ──
    // frameReady: 카메라 영상은 모두 연결 (각 위젯이 isVisible로 필터링)
    connect(m_kpClient, &KeypointClient::frameReady,
            m_studyWidget, &StudyWidget::onCameraFrame);
    connect(m_kpClient, &KeypointClient::frameReady,
            m_reviewWidget, &ReviewWidget::onCameraFrame);
    connect(m_kpClient, &KeypointClient::frameReady,
            m_dictWidget, &DictWidget::onCameraFrame);
    connect(m_kpClient, &KeypointClient::frameReady,
            m_testWidget, &TestWidget::onCameraFrame);

    // keypointReady: contentStack 현재 위젯에만 전달
    connect(m_kpClient, &KeypointClient::keypointReady,
            this, [this](const QJsonObject &kp){
        QWidget *cur = ui->contentStack->currentWidget();
        if      (cur == m_studyWidget)  m_studyWidget->onKeypointFrame(kp);
        else if (cur == m_reviewWidget) m_reviewWidget->onKeypointFrame(kp);
        else if (cur == m_dictWidget)   m_dictWidget->onKeypointFrame(kp);
        else if (cur == m_testWidget)   m_testWidget->onKeypointFrame(kp);
    });

    // keypoint_server에 연결 시작
    m_kpClient->connectToServer("127.0.0.1");

    // ── userBtn 드롭다운 메뉴 ─────────────────────────
    connect(ui->userBtn, &QPushButton::clicked,
            this, [this]{
        QMenu menu(this);

        QAction *settingsAction = menu.addAction("⚙  설정");
        menu.addSeparator();
        QAction *logoutAction   = menu.addAction("🚪  로그아웃");

        // userBtn 하단 기준으로 메뉴 표시
        QPoint pos = ui->userBtn->mapToGlobal(
            QPoint(0, ui->userBtn->height()));
        QAction *selected = menu.exec(pos);

        if (selected == settingsAction) {
            switchTab(5);
            emit settingsRequested();
        } else if (selected == logoutAction) {
            emit logoutRequested();
        }
    });

    switchTab(0);
    setConnected(false);

    qDebug() << "[Main] constructor done";
}

MainWindow::~MainWindow()
{
    m_kpClient->disconnectFromServer();
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// showTestTab — 테스트 탭으로 전환 (네비 버튼 강조 없이)
// ─────────────────────────────────────────────────────────────
void MainWindow::showTestTab()
{
    // 네비 버튼은 모두 비활성 스타일로
    const QString inactive = QString(
        "font-size:13px; color:%1;"
        "border-bottom:2px solid transparent; padding:0 14px; background:transparent;"
    ).arg(AppStyle::C_TEXT_MID);
    for (auto *btn : m_navBtns)
        btn->setStyleSheet(inactive);

    // TestWidget이 추가된 인덱스로 전환
    ui->contentStack->setCurrentWidget(m_testWidget);
}

void MainWindow::setupNavButtons()
{
    m_navBtns = {
        ui->navHome, ui->navStudy, ui->navReview,
        ui->navDict, ui->navGame,  ui->navSettings
    };
    for (int i = 0; i < m_navBtns.size(); i++) {
        int idx = i;
        connect(m_navBtns[i], &QPushButton::clicked,
                this, [this, idx]{
            if (idx == 2) {
                // 복습 탭: REQ_REVIEW_WORDS 요청이 필요하므로 시그널만 emit
                // 실제 화면 전환은 AppController → RES_REVIEW_WORDS 수신 후 switchToReview()
                emit reviewModeRequested();
            } else {
                switchTab(idx);
            }
        });
    }
}

void MainWindow::setupModeCards()
{
    auto addClick = [&](QFrame *card, int tabIdx, void(MainWindow::*sig)()) {
        auto *btn = new QPushButton(card);
        btn->setGeometry(0, 0, 500, 200);
        btn->setFlat(true);
        btn->setStyleSheet("background:transparent; border:none;");
        btn->raise();
        connect(btn, &QPushButton::clicked, this, [this, tabIdx, sig]{
            switchTab(tabIdx);
            emit (this->*sig)();
        });
    };

    addClick(ui->studyCard,  1, &MainWindow::studyModeRequested);
    addClick(ui->reviewCard, 2, &MainWindow::reviewModeRequested);
    addClick(ui->dictCard,   3, &MainWindow::dictModeRequested);
    addClick(ui->gameCard,   4, &MainWindow::gameModeRequested);
}

void MainWindow::switchTab(int index)
{
    const QString active = QString(
        "font-size:13px; color:%1; font-weight:500;"
        "border-bottom:2px solid %1; padding:0 14px; background:transparent;"
    ).arg(AppStyle::C_GREEN_DARK);

    const QString inactive = QString(
        "font-size:13px; color:%1;"
        "border-bottom:2px solid transparent; padding:0 14px; background:transparent;"
    ).arg(AppStyle::C_TEXT_MID);

    for (int i = 0; i < m_navBtns.size(); i++)
        m_navBtns[i]->setStyleSheet(i == index ? active : inactive);

    ui->contentStack->setCurrentIndex(index);
}

void MainWindow::setUserInfo(const QString &username, bool)
{
    m_username = username;
    ui->helloLabel->setText("안녕하세요, " + username + " 님!");
    ui->userBtn->setText("👤  " + username);   // QLabel → QPushButton
}

void MainWindow::setConnected(bool connected)
{
    if (connected) {
        ui->connDot->setStyleSheet(
            QString("background:%1; border-radius:4px;").arg(AppStyle::C_GREEN_LIGHT));
        ui->connLabel->setText("서버 연결됨");
        ui->connLabel->setStyleSheet(
            QString("font-size:12px; color:%1;").arg(AppStyle::C_GREEN_LIGHT));
    } else {
        ui->connDot->setStyleSheet("background:#E24B4A; border-radius:4px;");
        ui->connLabel->setText("연결 끊김");
        ui->connLabel->setStyleSheet("font-size:12px; color:#E24B4A;");
    }
}

void MainWindow::setTodayProgress(int done, int goal)
{
    m_todayDone = done;
    m_todayGoal = goal;
    ui->progressBar->setRange(0, goal);
    ui->progressBar->setValue(done);
    ui->goalLabel->setText(QString("%1 / %2").arg(done).arg(goal));
    ui->progressText->setText(
        QString("단어 %1개 완료  ·  목표 %2개").arg(done).arg(goal));
}

void MainWindow::setDailyGoal(int goal)
{
    // done은 현재값 유지, goal만 갱신
    m_todayGoal = goal;
    ui->progressBar->setRange(0, goal);
    ui->goalLabel->setText(QString("%1 / %2").arg(m_todayDone).arg(goal));
    ui->progressText->setText(
        QString("단어 %1개 완료  ·  목표 %2개").arg(m_todayDone).arg(goal));
}

void MainWindow::setReviewCount(int count) { Q_UNUSED(count) }

QWidget* MainWindow::currentWidget() const
{
    return ui->contentStack->currentWidget();
}

void MainWindow::switchToHome()
{
    switchTab(0);
}

void MainWindow::switchToReview()
{
    // ReviewWidget이 인덱스 2에 정확히 삽입되어 있으므로 switchTab(2)로 충분
    switchTab(2);
}
