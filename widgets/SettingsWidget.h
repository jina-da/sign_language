#pragma once

#include <QWidget>
#include <QButtonGroup>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsWidget; }
QT_END_NAMESPACE

class SettingsWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsWidget(QWidget *parent = nullptr);
    ~SettingsWidget();

    // 로그인 성공 후 서버에서 받아온 초기값 세팅
    void setInitialValues(int dailyGoal,
                          bool isDominantLeft,
                          bool isDeaf,
                          bool keypointConsent);

    // 서버 응답 처리 (AppController에서 호출)
    void onSaveSuccess(const QString &settingType);
    void onSaveError(const QString &message);
    void onWithdrawSuccess();
    void updateDailyGoal(int goal);   // RES_DAILY_WORDS 수신 시 goal 수치 갱신
    int  currentDailyGoal() const { return m_origGoal; }   // 현재 적용된 goal 반환

signals:
    // 저장 버튼 → 변경된 항목들을 AppController로 전달
    void dailyGoalChangeRequested(int goal);
    void dominantHandChangeRequested(bool isDominantLeft);
    void deafChangeRequested(bool isDeaf);
    void passwordChangeRequested(const QString &currentPwHash,
                                 const QString &newPwHash);
    void consentChangeRequested(bool consent);
    void withdrawRequested(const QString &passwordHash);

private slots:
    void onSaveClicked();
    void onWithdrawClicked();

private:
    QString hashPassword(const QString &plain) const;
    void setLoading(bool loading);
    void showStatus(const QString &message, bool isError = false);

    Ui::SettingsWidget *ui;
    QButtonGroup       *m_handGroup;

    // 초기값 저장 (변경 여부 감지용)
    int  m_origGoal          = 15;
    bool m_origDominantLeft  = false;
    bool m_origDeaf          = false;
    bool m_origConsent       = true;

    // 저장 진행 중 응답 대기 카운터
    int m_pendingCount = 0;
};
