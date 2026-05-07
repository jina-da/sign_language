#pragma once

#include <QWidget>
#include <QList>
#include <QPushButton>
#include "StudyWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void setUserInfo(const QString &username, bool isDominantLeft);
    void setConnected(bool connected);
    void setTodayProgress(int done, int goal);
    void setReviewCount(int count);

    // StudyWidget 접근 (AppController에서 단어 목록 전달용)
    StudyWidget* studyWidget() const { return m_studyWidget; }

signals:
    void logoutRequested();
    void studyModeRequested();
    void reviewModeRequested();
    void gameModeRequested();
    void dictModeRequested();

private:
    void applyStyles();
    void switchTab(int index);
    void setupNavButtons();
    void setupModeCards();

    Ui::MainWindow *ui;
    QList<QPushButton*> m_navBtns;
    StudyWidget *m_studyWidget = nullptr;

    QString m_username;
    int m_todayDone = 0;
    int m_todayGoal = 15;
};
