#pragma once

#include <QWidget>
#include <QList>
#include <QPushButton>
#include "StudyWidget.h"
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

    void setUserInfo(const QString &username, bool isDominantLeft);
    void setConnected(bool connected);
    void setTodayProgress(int done, int goal);
    void setReviewCount(int count);

    StudyWidget*    studyWidget()    const { return m_studyWidget; }
    KeypointClient* keypointClient() const { return m_kpClient; }

signals:
    void logoutRequested();
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
    StudyWidget     *m_studyWidget = nullptr;
    KeypointClient  *m_kpClient    = nullptr;

    QString m_username;
    int m_todayDone = 0;
    int m_todayGoal = 15;
};
