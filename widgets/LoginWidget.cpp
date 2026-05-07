#include "LoginWidget.h"
#include "AppStyle.h"
#include "ui_LoginWidget.h"

#include <QCryptographicHash>
#include <QSettings>

LoginWidget::LoginWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::LoginWidget)
{
    ui->setupUi(this);

    QSettings s("SignLearn", "Client");
    if (s.contains("saved_username")) {
        ui->usernameEdit->setText(s.value("saved_username").toString());
        ui->saveIdBox->setChecked(true);
    }

    connect(ui->loginBtn,    &QPushButton::clicked,
            this,            &LoginWidget::onLoginClicked);
    connect(ui->registerBtn, &QPushButton::clicked,
            this,            &LoginWidget::registerRequested);
    connect(ui->eyeBtn,      &QPushButton::clicked,
            this,            &LoginWidget::onTogglePassword);
    connect(ui->passwordEdit, &QLineEdit::returnPressed,
            this,             &LoginWidget::onLoginClicked);
    connect(ui->usernameEdit, &QLineEdit::returnPressed,
            this, [this]{ ui->passwordEdit->setFocus(); });
    connect(ui->debugBtn, &QPushButton::clicked,
            this,         &LoginWidget::debugLogin);    // 디버그 버튼

    // 초기 연결 상태
    setConnected(false);
}

LoginWidget::~LoginWidget()
{
    delete ui;
}

// ── 동적 상태 업데이트 ────────────────────────────────────────

void LoginWidget::setConnected(bool connected)
{
    if (connected) {
        ui->statusDot->setStyleSheet(
            QString("background:%1; border-radius:4px;").arg(AppStyle::C_GREEN_MID));
        ui->statusLabel->setText("서버 연결됨");
    } else {
        ui->statusDot->setStyleSheet("background:#E24B4A; border-radius:4px;");
        ui->statusLabel->setText("서버 연결 끊김 — 백그라운드에서 재연결 중");
    }
}

void LoginWidget::showError(const QString &message)
{
    ui->errorLabel->setText(message);
    ui->errorLabel->show();
    setLoading(false);
}

void LoginWidget::setLoading(bool loading)
{
    ui->loginBtn->setEnabled(!loading);
    ui->loginBtn->setText(loading ? "로그인 중..." : "로그인");
}

void LoginWidget::onLoginClicked()
{
    ui->errorLabel->hide();

    QString username = ui->usernameEdit->text().trimmed();
    QString password = ui->passwordEdit->text();

    if (username.isEmpty()) {
        showError("아이디를 입력해 주세요.");
        ui->usernameEdit->setFocus();
        return;
    }
    if (password.isEmpty()) {
        showError("비밀번호를 입력해 주세요.");
        ui->passwordEdit->setFocus();
        return;
    }

    QSettings s("SignLearn", "Client");
    if (ui->saveIdBox->isChecked())
        s.setValue("saved_username", username);
    else
        s.remove("saved_username");

    setLoading(true);
    // emit loginRequested(username, hashPassword(password));
    emit loginRequested(username, password);
}

void LoginWidget::onTogglePassword()
{
    bool hidden = (ui->passwordEdit->echoMode() == QLineEdit::Password);
    ui->passwordEdit->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
    ui->eyeBtn->setText(hidden ? "🙈" : "👁");
}

QString LoginWidget::hashPassword(const QString &plain) const
{
    return QCryptographicHash::hash(
        plain.toUtf8(), QCryptographicHash::Sha256).toHex();
}
