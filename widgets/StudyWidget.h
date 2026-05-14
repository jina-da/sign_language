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

QT_BEGIN_NAMESPACE
namespace Ui { class StudyWidget; }
QT_END_NAMESPACE

class StudyWidget : public QWidget
{
    Q_OBJECT

public:
    struct WordInfo {
        int     id;
        QString word;
        QString meaning;
        int     difficulty;
        QString videoCdnUrl;   // FastAPI 스트리밍 URL
    };

    explicit StudyWidget(QWidget *parent = nullptr);
    ~StudyWidget();

    void setWordList(const QList<WordInfo> &words);
    void setDailyGoal(int goal);
    VideoPlayer* videoPlayer() const { return m_videoPlayer; }   // progress bar 최대값을 daily_goal로 설정
    void onCameraFrame(const QImage &frame);
    void onKeypointFrame(const QJsonObject &keypoint);
    void showResult(const QString &verdict,
                    double confidence,
                    int predictedWordId);

signals:
    void keypointReady(int wordId, bool isDominantLeft, const QJsonArray &keypoints);
    void studyFinished();
    void wordSkipped(int wordId);
    // 학습 완료 후 테스트 시작 요청 (단어 목록을 TestWidget에 전달)
    void testRequested(const QList<StudyWidget::WordInfo> &words);

private slots:
    void onPrevClicked();   // ← 이전 단어
    void onNextClicked();
    void onSkipClicked();
    void onRecordBtnClicked();   // 녹화 버튼 / 스페이스바
    void onCountdownTick();        // 카운트다운 타이머
    void onSpeedChanged();
    void onRecordingTimeout();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void loadWord(int index);
    void startRecording();
    void stopRecording();
    void updateProgress();
    void applyVerdictStyle(const QString &verdict);
    void showCompletionDialog();   // 모든 단어 완료 시 팝업

    Ui::StudyWidget *ui;
    VideoPlayer     *m_videoPlayer = nullptr;

    QList<WordInfo> m_words;
    int    m_dailyGoal    = 15;   // 서버에서 받은 목표 단어 수
    int    m_currentIndex = 0;
    bool   m_isRecording  = false;
    double m_playSpeed    = 1.0;

    QJsonArray    m_keypointBuffer;
    QJsonObject   m_prevKeypoint;       // 직전 프레임 (움직임 감지용)
    bool          m_hasPrevKeypoint = false;
    QTimer       *m_stopTimer;
    QTimer       *m_countdownTimer;   // 3초 카운트다운
    int            m_countdown = 0;   // 남은 카운트 (3→2→1→0)
    QTimer       *m_cooldownTimer;  // 녹화 종료 후 재시작 방지 (1.5초)
    QButtonGroup *m_speedGroup;

    QElapsedTimer m_recordingStartTime;
};
