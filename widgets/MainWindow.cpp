#include "MainWindow.h"
#include "AppStyle.h"
#include "ui_MainWindow.h"
#include "ReviewWidget.h"
#include "DictWidget.h"
#include "SettingsWidget.h"

#include <QDebug>
#include <QMenu>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>
#include <QMessageBox>

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

    // connectionChanged: 카메라 연결 상태를 각 위젯에 전달
    connect(m_kpClient, &KeypointClient::connectionChanged,
            m_studyWidget,  &StudyWidget::setCameraConnected);
    connect(m_kpClient, &KeypointClient::connectionChanged,
            m_reviewWidget, &ReviewWidget::setCameraConnected);
    connect(m_kpClient, &KeypointClient::connectionChanged,
            m_dictWidget,   &DictWidget::setCameraConnected);
    connect(m_kpClient, &KeypointClient::connectionChanged,
            m_testWidget,   &TestWidget::setCameraConnected);

    // keypointReady: contentStack 현재 위젯에만 전달
    connect(m_kpClient, &KeypointClient::keypointReady,
            this, [this](const QJsonObject &kp){
        QWidget *cur = ui->contentStack->currentWidget();
        if      (cur == m_studyWidget)  m_studyWidget->onKeypointFrame(kp);
        else if (cur == m_reviewWidget) m_reviewWidget->onKeypointFrame(kp);
        else if (cur == m_dictWidget)   m_dictWidget->onKeypointFrame(kp);
        else if (cur == m_testWidget)   m_testWidget->onKeypointFrame(kp);
    });

    // keypoint_server 자동 실행
    // connectToServer는 Python stdout의 "[Server]" 확인 후 호출 (startKeypointServer 내부)
    startKeypointServer();

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
    stopKeypointServer();
    delete ui;
}

void MainWindow::stopKeypointServer()
{
    if (m_keypointProcess && m_keypointProcess->state() != QProcess::NotRunning) {
        qint64 pid = m_keypointProcess->processId();
        if (pid > 0)
            QProcess::execute("taskkill", { "/F", "/T", "/PID", QString::number(pid) });
        m_keypointProcess->terminate();
        if (!m_keypointProcess->waitForFinished(2000))
            m_keypointProcess->kill();
        qDebug() << "[KP] keypoint_server 종료 완료";
    }
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

    // 탭 전환 시 현재 카메라 연결 상태를 해당 위젯에 즉시 전달
    bool camConnected = m_kpClient->isConnected();
    if (index == 1) m_studyWidget->setCameraConnected(camConnected);
    else if (index == 2) m_reviewWidget->setCameraConnected(camConnected);
    else if (index == 3) m_dictWidget->setCameraConnected(camConnected);
    else if (index == 6) m_testWidget->setCameraConnected(camConnected);
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

// ─────────────────────────────────────────────────────────────
// startKeypointServer
// 실행 파일 옆의 keypoint_server/keypoint_server.py 를 python으로 실행한다.
// 이미 실행 중이면 중복 실행하지 않는다.
// ─────────────────────────────────────────────────────────────
// startKeypointServer
// 우선순위:
//   1. keypoint_server.exe  — 배포용 (pyinstaller 빌드 결과물)
//   2. run_keypoint_server.bat — 개발용 (bat 안의 Python 절대경로 사용)
//   3. python3 / python     — 폴백 (PATH에 등록된 경우)
// ─────────────────────────────────────────────────────────────
void MainWindow::startKeypointServer()
{
    if (m_keypointProcess &&
        m_keypointProcess->state() != QProcess::NotRunning) {
        qDebug() << "[KP] keypoint_server 이미 실행 중";
        return;
    }

    // 이전 실행에서 정상 종료되지 않은 keypoint_server 프로세스 정리
    // python.exe가 keypoint_server.py를 실행하므로 포트 점유 기준으로 종료
    qDebug() << "[KP] 포트 7000 점유 프로세스 정리 중...";
    QProcess findPort;
    findPort.start("cmd", { "/c",
        "for /f \"tokens=5\" %a in "
        "('netstat -ano ^| findstr :7000 ^| findstr LISTENING') "
        "do taskkill /F /PID %a" });
    findPort.waitForFinished(3000);
    qDebug() << "[KP] 기존 프로세스 정리 완료";

    QString serverDir = QCoreApplication::applicationDirPath()
                        + "/keypoint_server";
    QString exePath   = serverDir + "/keypoint_server.exe";
    QString batPath   = serverDir + "/run_keypoint_server.bat";
    QString pyPath    = serverDir + "/keypoint_server.py";

    m_keypointProcess = new QProcess(this);
    m_keypointProcess->setWorkingDirectory(serverDir);

    connect(m_keypointProcess, &QProcess::readyReadStandardOutput, this, [this] {
        QString output = QString::fromUtf8(
            m_keypointProcess->readAllStandardOutput()).trimmed();
        qDebug() << "[KP-py]" << output;

        // Python 서버가 포트 바인딩 완료("[Server]" 출력)된 시점에 Qt 연결 시작
        // 이전에 연결을 시도한 적 없는 경우에만 (재연결은 KeypointClient가 자동 처리)
        if (!m_kpServerReady && output.contains("[Server]")) {
            m_kpServerReady = true;
            qDebug() << "[KP] Python 서버 준비 완료 → 연결 시작";
            m_kpClient->connectToServer("127.0.0.1");
        }
    });
    connect(m_keypointProcess, &QProcess::readyReadStandardError, this, [this] {
        qDebug() << "[KP-py ERR]"
                 << m_keypointProcess->readAllStandardError().trimmed();
    });
    connect(m_keypointProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus status) {
        if (status == QProcess::CrashExit || code != 0)
            qDebug() << "[KP] keypoint_server 비정상 종료 — code:" << code;
        else
            qDebug() << "[KP] keypoint_server 정상 종료";
    });

    // ── 1순위: keypoint_server.exe (배포용) ──────────────────
    if (QFileInfo::exists(exePath)) {
        m_keypointProcess->start(exePath, {});
        if (m_keypointProcess->waitForStarted(2000)) {
            qDebug() << "[KP] keypoint_server.exe 시작 (PID:"
                     << m_keypointProcess->processId() << ")";
            scheduleConnectFallback();
            return;
        }
        qDebug() << "[KP] .exe 시작 실패, 다음 방법 시도...";
    }

    // ── 2순위: run_keypoint_server.bat (개발용) ───────────────
    if (QFileInfo::exists(batPath)) {
        // cmd /c: 콘솔 창 없이 bat 실행
        m_keypointProcess->start("cmd", { "/c", batPath });
        if (m_keypointProcess->waitForStarted(2000)) {
            qDebug() << "[KP] bat 파일로 시작 (PID:"
                     << m_keypointProcess->processId() << ")";
            scheduleConnectFallback();
            return;
        }
        qDebug() << "[KP] bat 시작 실패, 다음 방법 시도...";
    }

    // ── 3순위: python3 / python (폴백) ───────────────────────
    if (QFileInfo::exists(pyPath)) {
        for (const QString &exe : { QString("python3"), QString("python") }) {
            m_keypointProcess->start(exe, { pyPath });
            if (m_keypointProcess->waitForStarted(1500)) {
                qDebug() << "[KP]" << exe << "로 시작 (PID:"
                         << m_keypointProcess->processId() << ")";
                scheduleConnectFallback();
                return;
            }
        }
    }

    qDebug() << "[KP] 모든 방법 실패 — keypoint_server를 수동으로 실행해 주세요.";
}

// ─────────────────────────────────────────────────────────────
// scheduleConnectFallback
// Python stdout에서 "[Server]" 감지 시 connectToServer가 호출되지만,
// stdout 버퍼링으로 신호가 늦거나 없을 경우를 위한 최대 10초 폴백.
// ─────────────────────────────────────────────────────────────
void MainWindow::scheduleConnectFallback()
{
    QTimer::singleShot(10000, this, [this] {
        if (!m_kpServerReady) {
            qDebug() << "[KP] 폴백: 10초 경과 → 강제 연결 시도";
            m_kpServerReady = true;
            m_kpClient->connectToServer("127.0.0.1");
        }
    });
}
