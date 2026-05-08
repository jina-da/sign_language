#include "MainWindow.h"
#include "AppStyle.h"
#include "ui_MainWindow.h"

#include <QDebug>

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

    // 학습 탭에 StudyWidget 삽입
    m_studyWidget = new StudyWidget(this);
    ui->contentStack->removeWidget(ui->studyTab);
    ui->contentStack->insertWidget(1, m_studyWidget);

    // KeypointClient → StudyWidget 연결
    connect(m_kpClient, &KeypointClient::frameReady,
            m_studyWidget, &StudyWidget::onCameraFrame);
    connect(m_kpClient, &KeypointClient::keypointReady,
            m_studyWidget, &StudyWidget::onKeypointFrame);

    // keypoint_server에 연결 시작 (항상 시도, 없으면 재연결 반복)
    m_kpClient->connectToServer("127.0.0.1");

    switchTab(0);
    setConnected(false);

    qDebug() << "[Main] constructor done";
}

MainWindow::~MainWindow()
{
    m_kpClient->disconnectFromServer();
    delete ui;
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
                this, [this, idx]{ switchTab(idx); });
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
    ui->userLabel->setText("👤  " + username);
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

void MainWindow::setReviewCount(int count) { Q_UNUSED(count) }
