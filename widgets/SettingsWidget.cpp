#include "SettingsWidget.h"
#include "ui_SettingsWidget.h"

#include <QButtonGroup>
#include <QCryptographicHash>
#include <QDebug>
#include <QMessageBox>
#include <QPushButton>

SettingsWidget::SettingsWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::SettingsWidget)
    , m_handGroup(new QButtonGroup(this))
{
    ui->setupUi(this);

    // 우세손 라디오 버튼 그룹
    m_handGroup->addButton(ui->rightHandBtn);
    m_handGroup->addButton(ui->leftHandBtn);
    m_handGroup->setExclusive(true);

    ui->saveBtn->setFocusPolicy(Qt::NoFocus);
    ui->withdrawBtn->setFocusPolicy(Qt::NoFocus);

    connect(ui->saveBtn,     &QPushButton::clicked,
            this,            &SettingsWidget::onSaveClicked);
    connect(ui->withdrawBtn, &QPushButton::clicked,
            this,            &SettingsWidget::onWithdrawClicked);
}

SettingsWidget::~SettingsWidget()
{
    delete ui;
}

// ─────────────────────────────────────────────────────────────
// setInitialValues — 로그인 후 서버 값으로 화면 초기화
// ─────────────────────────────────────────────────────────────
void SettingsWidget::setInitialValues(int dailyGoal,
                                      bool isDominantLeft,
                                      bool isDeaf,
                                      bool keypointConsent)
{
    // 현재값 세팅
    ui->goalSpinBox->setValue(dailyGoal);
    ui->leftHandBtn->setChecked(isDominantLeft);
    ui->rightHandBtn->setChecked(!isDominantLeft);
    ui->deafCheckBox->setChecked(isDeaf);
    ui->consentCheckBox->setChecked(keypointConsent);

    // 비밀번호 필드 초기화
    ui->curPwEdit->clear();
    ui->newPwEdit->clear();
    ui->confirmPwEdit->clear();
    ui->pwErrorLabel->hide();

    // 원래값 저장 (변경 감지용)
    m_origGoal         = dailyGoal;
    m_origDominantLeft = isDominantLeft;
    m_origDeaf         = isDeaf;
    m_origConsent      = keypointConsent;

    ui->statusLabel->hide();
    qDebug() << "[Settings] 초기값 세팅: goal=" << dailyGoal
             << "left=" << isDominantLeft
             << "deaf=" << isDeaf
             << "consent=" << keypointConsent;
}

// ─────────────────────────────────────────────────────────────
// onSaveClicked — 변경된 항목만 골라서 시그널 emit
// ─────────────────────────────────────────────────────────────
void SettingsWidget::onSaveClicked()
{
    ui->pwErrorLabel->hide();
    m_pendingCount = 0;

    int  newGoal         = ui->goalSpinBox->value();
    bool newDominantLeft = ui->leftHandBtn->isChecked();
    bool newDeaf         = ui->deafCheckBox->isChecked();
    bool newConsent      = ui->consentCheckBox->isChecked();

    QString curPw  = ui->curPwEdit->text();
    QString newPw  = ui->newPwEdit->text();
    QString confPw = ui->confirmPwEdit->text();

    // ── 비밀번호 변경 유효성 검사 ─────────────────────
    bool wantsPasswordChange = !curPw.isEmpty() || !newPw.isEmpty();
    if (wantsPasswordChange) {
        if (curPw.isEmpty()) {
            ui->pwErrorLabel->setText("현재 비밀번호를 입력해 주세요.");
            ui->pwErrorLabel->show();
            return;
        }
        if (newPw.length() < 6) {
            ui->pwErrorLabel->setText("새 비밀번호는 6자 이상이어야 합니다.");
            ui->pwErrorLabel->show();
            return;
        }
        if (newPw != confPw) {
            ui->pwErrorLabel->setText("새 비밀번호가 일치하지 않습니다.");
            ui->pwErrorLabel->show();
            return;
        }
    }

    // ── 변경된 항목만 시그널 emit ─────────────────────
    if (newGoal != m_origGoal) {
        m_pendingCount++;
        emit dailyGoalChangeRequested(newGoal);
    }
    if (newDominantLeft != m_origDominantLeft) {
        m_pendingCount++;
        emit dominantHandChangeRequested(newDominantLeft);
    }
    if (newDeaf != m_origDeaf) {
        m_pendingCount++;
        emit deafChangeRequested(newDeaf);
    }
    if (newConsent != m_origConsent) {
        m_pendingCount++;
        emit consentChangeRequested(newConsent);
    }
    if (wantsPasswordChange) {
        m_pendingCount++;
        emit passwordChangeRequested(hashPassword(curPw), hashPassword(newPw));
    }

    if (m_pendingCount == 0) {
        showStatus("변경된 항목이 없습니다.");
        return;
    }

    setLoading(true);
    qDebug() << "[Settings] 저장 요청 항목 수:" << m_pendingCount;
}

// ─────────────────────────────────────────────────────────────
// onWithdrawClicked — 회원 탈퇴 확인 다이얼로그
// ─────────────────────────────────────────────────────────────
void SettingsWidget::onWithdrawClicked()
{
    QMessageBox confirm(this);
    confirm.setWindowTitle("회원 탈퇴");
    confirm.setText("정말로 탈퇴하시겠습니까?\n모든 학습 데이터가 삭제되며 복구할 수 없습니다.");
    confirm.setIcon(QMessageBox::Warning);

    QPushButton *confirmBtn = confirm.addButton("탈퇴하기", QMessageBox::DestructiveRole);
    confirm.addButton("취소", QMessageBox::RejectRole);
    confirm.exec();

    if (confirm.clickedButton() != confirmBtn) return;

    // 비밀번호 확인 다이얼로그
    // 현재 비밀번호 필드가 비어있으면 재입력 요구
    QString pw = ui->curPwEdit->text();
    if (pw.isEmpty()) {
        ui->pwErrorLabel->setText("탈퇴를 위해 현재 비밀번호를 입력해 주세요.");
        ui->pwErrorLabel->show();
        ui->curPwEdit->setFocus();
        return;
    }

    emit withdrawRequested(hashPassword(pw));
    setLoading(true);
    qDebug() << "[Settings] 회원 탈퇴 요청";
}

// ─────────────────────────────────────────────────────────────
// onSaveSuccess — 서버 응답 성공 처리
// settingType: "goal" | "hand" | "deaf" | "password" | "consent"
// ─────────────────────────────────────────────────────────────
void SettingsWidget::onSaveSuccess(const QString &settingType)
{
    // 원래값 업데이트
    if (settingType == "goal")
        m_origGoal = ui->goalSpinBox->value();
    else if (settingType == "hand")
        m_origDominantLeft = ui->leftHandBtn->isChecked();
    else if (settingType == "deaf")
        m_origDeaf = ui->deafCheckBox->isChecked();
    else if (settingType == "consent")
        m_origConsent = ui->consentCheckBox->isChecked();
    else if (settingType == "password") {
        ui->curPwEdit->clear();
        ui->newPwEdit->clear();
        ui->confirmPwEdit->clear();
    }

    m_pendingCount--;
    if (m_pendingCount <= 0) {
        m_pendingCount = 0;
        setLoading(false);
        showStatus("✓ 설정이 저장됐습니다.");
    }
}

// ─────────────────────────────────────────────────────────────
// onSaveError — 서버 오류 처리
// ─────────────────────────────────────────────────────────────
void SettingsWidget::onSaveError(const QString &message)
{
    m_pendingCount = 0;
    setLoading(false);
    showStatus(message, true);
    qWarning() << "[Settings] 저장 오류:" << message;
}

// ─────────────────────────────────────────────────────────────
// onWithdrawSuccess — 탈퇴 완료 (AppController가 로그아웃 처리)
// ─────────────────────────────────────────────────────────────
void SettingsWidget::updateDailyGoal(int goal)
{
    // 서버에서 받은 실제 goal로 SpinBox와 원래값 모두 갱신
    ui->goalSpinBox->setValue(goal);
    m_origGoal = goal;
    qDebug() << "[Settings] daily_goal 갱신:" << goal;
}

void SettingsWidget::onWithdrawSuccess()
{
    setLoading(false);
    qDebug() << "[Settings] 회원 탈퇴 완료";
}

// ─────────────────────────────────────────────────────────────
// 내부 헬퍼
// ─────────────────────────────────────────────────────────────
QString SettingsWidget::hashPassword(const QString &plain) const
{
    return QCryptographicHash::hash(
        plain.toUtf8(), QCryptographicHash::Sha256).toHex();
}

void SettingsWidget::setLoading(bool loading)
{
    ui->saveBtn->setEnabled(!loading);
    ui->saveBtn->setText(loading ? "저장 중..." : "저장");
    ui->withdrawBtn->setEnabled(!loading);
}

void SettingsWidget::showStatus(const QString &message, bool isError)
{
    ui->statusLabel->setText(message);
    ui->statusLabel->setStyleSheet(
        isError ? "font-size:12px; color:#A93226;"
                : "font-size:12px; color:#639922;");
    ui->statusLabel->show();
}
