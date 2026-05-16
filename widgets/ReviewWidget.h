#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QButtonGroup>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QKeyEvent>
#include "VideoPlayer.h"

// ReviewWidget은 StudyWidget과 동일한 .ui 파일을 공유한다.
// uic가 생성하는 클래스명이 Ui::StudyWidget 이므로 namespace를 StudyWidget으로 선언한다.
QT_BEGIN_NAMESPACE
namespace Ui { class StudyWidget; }
QT_END_NAMESPACE

/**
 * ReviewWidget — 복습 모드 화면
 *
 * StudyWidget과 UI 구조가 동일하여 StudyWidget.ui를 재사용한다.
 * 학습 모드와의 차이점:
 *   - 헤더 "복습 모드" 표시
 *   - 완료 시 nextBtn → "완료" (테스트 없음)
 *   - testRequested 시그널 없음
 *   - reviewFinished 시그널 emit
 */
class ReviewWidget : public QWidget
{
    Q_OBJECT

public:
    // StudyWidget::WordInfo와 동일한 구조 (별도 정의로 의존성 분리)
    struct WordInfo {
        int     id;
        QString word;
        QString meaning;
        int     difficulty;
        QString videoCdnUrl;
    };

    explicit ReviewWidget(QWidget *parent = nullptr);
    ~ReviewWidget();

    void setWordList(const QList<WordInfo> &words);
    void showNoWordsMessage(const QString &message);

    void onCameraFrame(const QImage &frame);
    void setCameraConnected(bool connected) { m_cameraConnected = connected; }
    void onKeypointFrame(const QJsonObject &keypoint);
    void showResult(const QString &verdict,
                    double confidence,
                    int predictedWordId);
    VideoPlayer *videoPlayer() const { return m_videoPlayer; }

signals:
    void keypointReady(int wordId, bool isDominantLeft, const QJsonArray &keypoints);
    void reviewFinished();
    void wordSkipped(int wordId);

private slots:
    void onPrevClicked();
    void onNextClicked();
    void onSkipClicked();
    void onReplayClicked();
    void onSpeedChanged();
    void onRecordingTimeout();
    void onRecordBtnClicked();
    void onCountdownTick();

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void loadWord(int index);
    void startRecording();
    void stopRecording();
    void updateProgress();
    void applyVerdictStyle(const QString &verdict);
    void showCompletionMessage();

    Ui::StudyWidget *ui;   // StudyWidget.ui 재사용

    VideoPlayer  *m_videoPlayer = nullptr;

    QList<WordInfo> m_words;
    int    m_currentIndex = 0;
    bool   m_isRecording     = false;
    bool   m_cameraConnected = false;
    double m_playSpeed    = 1.0;

    QJsonArray    m_keypointBuffer;
    QJsonObject   m_prevKeypoint;
    bool          m_hasPrevKeypoint = false;
    QTimer       *m_stopTimer;
    QTimer       *m_cooldownTimer;
    QTimer       *m_countdownTimer;
    int            m_countdown = 0;
    QButtonGroup *m_speedGroup;

    QElapsedTimer m_recordingStartTime;
};
