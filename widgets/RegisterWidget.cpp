#include "RegisterWidget.h"
#include "ui_RegisterWidget.h"

#include <QCryptographicHash>

RegisterWidget::RegisterWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::RegisterWidget)
{
    ui->setupUi(this);

    connect(ui->registerBtn, &QPushButton::clicked,
            this,            &RegisterWidget::onRegisterClicked);
    connect(ui->backBtn,     &QPushButton::clicked,
            this,            &RegisterWidget::backToLogin);
    connect(ui->passwordConfirmEdit, &QLineEdit::returnPressed,
            this,                    &RegisterWidget::onRegisterClicked);
}

RegisterWidget::~RegisterWidget()
{
    delete ui;
}

void RegisterWidget::showError(const QString &message)
{
    ui->errorLabel->setText(message);
    ui->errorLabel->show();
    setLoading(false);
}

void RegisterWidget::setLoading(bool loading)
{
    ui->registerBtn->setEnabled(!loading);
    ui->registerBtn->setText(loading ? "처리 중..." : "회원가입");
}

void RegisterWidget::onRegisterClicked()
{
    ui->errorLabel->hide();

    QString username = ui->usernameEdit->text().trimmed();
    QString password = ui->passwordEdit->text();
    QString confirm  = ui->passwordConfirmEdit->text();

    // 입력 검증
    if (username.length() < 4) {
        showError("아이디는 4자 이상이어야 합니다.");
        ui->usernameEdit->setFocus();
        return;
    }
    if (password.length() < 6) {
        showError("비밀번호는 6자 이상이어야 합니다.");
        ui->passwordEdit->setFocus();
        return;
    }
    if (password != confirm) {
        showError("비밀번호가 일치하지 않습니다.");
        ui->passwordConfirmEdit->setFocus();
        return;
    }

    bool isDominantLeft = ui->leftHandBtn->isChecked();
    bool isDeaf         = ui->deafBox->isChecked();
    bool consent        = ui->consentBox->isChecked();

    setLoading(true);
    emit registerRequested(username, hashPassword(password),
                           isDeaf, isDominantLeft, consent);
}

QString RegisterWidget::hashPassword(const QString &plain) const
{
    return QCryptographicHash::hash(
        plain.toUtf8(), QCryptographicHash::Sha256).toHex();
}
