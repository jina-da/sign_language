#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QKeyEvent>
#include <QList>
#include "VideoPlayer.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DictWidget; }
QT_END_NAMESPACE

class DictWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DictWidget(QWidget *parent = nullptr);
    ~DictWidget();

    struct DictResult {
        int     wordId;
        QString word;
        QString description;
        QString videoCdnUrl;
    };

    // 로그인 후 세션 정보 세팅 — 이후 생성되는 VideoPlayer에 적용됨
    void setSession(const QString &host, const QString &token)
    { m_serverHost = host; m_sessionToken = token; }

    void onCameraFrame(const QImage &frame);
    void onKeypointFrame(const QJsonObject &keypoint);

    void showForwardResult(const QList<DictResult> &results);
    void showReverseResult(const QString &word, const QString &description);
    void showSearchError(const QString &message);

signals:
    void forwardSearchRequested(const QString &query);
    void reverseSearchRequested(const QJsonArray &keypoints);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onTabForwardClicked();
    void onTabReverseClicked();
    void onBackToListClicked();
    void onSearchBtnClicked();
    void onDebounceTimeout();
    void onRecordingTimeout();
    void onRecordBtnClicked();
    void onCountdownTick();

private:
    void switchTab(int index);
    void clearResults();
    void showListPage();
    void showDetailPage(const DictResult &result);
    void startRecording();
    void stopRecording();

    Ui::DictWidget *ui;

    QTimer        *m_stopTimer;
    QTimer        *m_cooldownTimer;
    QTimer        *m_countdownTimer;
    int            m_countdown = 0;
    QElapsedTimer  m_recordingStartTime;
    bool           m_isRecording = false;
    QJsonArray     m_keypointBuffer;

    QString        m_serverHost;
    QString        m_sessionToken;
    int            m_currentTab = 0;

    QList<VideoPlayer*>  m_resultPlayers;
    QList<DictResult>    m_currentResults;
};
