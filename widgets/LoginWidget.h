#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QCheckBox>

// uic가 자동 생성하는 헤더 (빌드 시 생성됨)
QT_BEGIN_NAMESPACE
namespace Ui { class LoginWidget; }
QT_END_NAMESPACE

class LoginWidget : public QWidget
{
    Q_OBJECT

public:
    explicit LoginWidget(QWidget *parent = nullptr);
    ~LoginWidget();

    void setConnected(bool connected);
    void showError(const QString &message);
    void setLoading(bool loading);

signals:
    void loginRequested(const QString &username, const QString &passwordHash);
    void registerRequested();
    void debugLogin();  // 디버그용: 바로 메인화면으로

private slots:
    void onLoginClicked();
    void onTogglePassword();

private:
    void applyStyles();
    QString hashPassword(const QString &plain) const;

    Ui::LoginWidget *ui;  // .ui 파일로 생성된 위젯 포인터
};
