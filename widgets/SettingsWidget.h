#pragma once

#include <QWidget>
#include <QButtonGroup>

QT_BEGIN_NAMESPACE
namespace Ui { class SettingsWidget; }
QT_END_NAMESPACE

/**
 * SettingsWidget — 개인 설정 화면
 *
 * 저장 방식: 각 항목을 수정한 뒤 "저장" 버튼을 누르면
 * 변경된 항목만 골라서 서버에 전송한다.
 *
 * 전송 메시지 목록:
 *   REQ_SET_DAILY_GOAL   (801) — 하루 목표 단어 수
 *   REQ_SET_DOMINANT_HAND(803) — 우세손
 *   REQ_SET_DEAF         (805) — 농인 여부
 *   REQ_CHANGE_PASSWORD  (807) — 비밀번호 변경
 *   REQ_SET_CONSENT      (809) — 키포인트 수집 동의
 *   REQ_WITHDRAW         (811) — 회원 탈퇴
 */
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
