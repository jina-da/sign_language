#pragma once

#include <QWidget>
#include <QList>
#include <QMenu>
#include <QPushButton>
#include <QProcess>
#include <QScrollArea>
#include <QLabel>
#include "StudyWidget.h"
#include "ReviewWidget.h"
#include "DictWidget.h"
#include "SettingsWidget.h"
#include "TestWidget.h"
#include "camera/KeypointClient.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void stopKeypointServer();   // 외부에서 명시적 종료 가능 (AppController::cleanup 등)

    void setUserInfo(const QString &username, bool isDominantLeft);
    void setConnected(bool connected);
    void setTodayProgress(int done, int goal);
    void setDailyGoal(int goal);       // 목표 단어 수만 갱신 (done 보존)
    void setReviewCount(int count);    // 미구현 (서버 대기)

    // 홈 화면 카드 뱃지 갱신 (RES_LOGIN에서 호출)
    void setHomeBadges(int reviewPendingCount, int highScore);

    // 홈 하단 "오늘 학습한 단어" 목록 갱신 (RES_DAILY_WORDS에서 호출)
    struct LearnedWord { int wordId; QString word; QString meaning; };
    void setLearnedWords(const QList<LearnedWord> &words);

    StudyWidget*    studyWidget()    const { return m_studyWidget; }
    ReviewWidget*   reviewWidget()   const { return m_reviewWidget; }
    DictWidget*     dictWidget()     const { return m_dictWidget; }
    SettingsWidget* settingsWidget() const { return m_settingsWidget; }
    TestWidget*     testWidget()     const { return m_testWidget; }
    KeypointClient* keypointClient() const { return m_kpClient; }
    // 현재 contentStack에 표시 중인 위젯 반환
    QWidget*        currentWidget()  const;

    void showTestTab();      // 테스트 탭으로 전환
    void switchToHome();     // 홈 탭(0)으로 전환
    void switchToReview();   // 복습 탭(2)으로 전환 — StudyWidget 복습 모드 재사용

signals:
    void logoutRequested();
    void settingsRequested();
    void homeRequested();        // 홈 탭 진입 시 REQ_DAILY_WORDS 재전송 트리거
    void studyModeRequested();
    void reviewModeRequested();
    void gameModeRequested();
    void dictModeRequested();

private:
    void setupNavButtons();
    void setupModeCards();
    void switchTab(int index);

    Ui::MainWindow  *ui;
    QList<QPushButton*> m_navBtns;
    StudyWidget     *m_studyWidget  = nullptr;
    ReviewWidget    *m_reviewWidget  = nullptr;
    DictWidget      *m_dictWidget      = nullptr;
    SettingsWidget  *m_settingsWidget  = nullptr;
    TestWidget      *m_testWidget    = nullptr;
    KeypointClient  *m_kpClient    = nullptr;

    QString m_username;
    int m_todayDone = 0;
    int m_todayGoal = 15;

    QScrollArea *m_wordScrollArea = nullptr;  // wordCard 내 스크롤 영역
    QWidget     *m_wordListWidget = nullptr;  // 단어 행들의 컨테이너

    QProcess *m_keypointProcess = nullptr;
    bool      m_kpServerReady   = false;
    void startKeypointServer();
    void scheduleConnectFallback();  // Python 준비 신호 없을 때 10초 후 강제 연결
};
