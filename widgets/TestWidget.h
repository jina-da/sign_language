#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QButtonGroup>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>
#include <QKeyEvent>

QT_BEGIN_NAMESPACE
namespace Ui { class TestWidget; }
QT_END_NAMESPACE

class TestWidget : public QWidget
{
    Q_OBJECT

public:
    struct WordInfo {
        int     id;
        QString word;
        QString meaning;
    };

    explicit TestWidget(QWidget *parent = nullptr);
    ~TestWidget();

    void setWordList(const QList<WordInfo> &words);

    // KeypointClient로부터 연결되는 슬롯
    void onCameraFrame(const QImage &frame);
    void onKeypointFrame(const QJsonObject &keypoint);

    // AppController에서 RES_INFER 수신 시 호출
    // accuracy: 0.0~1.0 (AppController에서 *100 하지 않은 원본 값)
    void showResult(bool isCorrect, double accuracy, int wordId);

signals:
    void keypointReady(int wordId, bool isDominantLeft, const QJsonArray &keypoints);
    void testFinished(int correctCount, int totalCount);
    void testAborted();

    // 영상 재생 요청 (VideoPlayer 연동 예정)
    // wordId에 해당하는 영상을 speed 배속으로 재생
    void videoPlayRequested(int wordId, double speed);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onNextClicked();
    void onHomeClicked();
    void onReplayClicked();
    void onSpeedChanged();
    void onRecordingTimeout();
    void onRecordBtnClicked();
    void onCountdownTick();

private:
    void loadWord(int index);
    void startRecording();
    void stopRecording();
    void updateProgress();
    void showSummary();
    void triggerVideoPlay();   // 정답/오답 시 영상 자동재생 호출

    Ui::TestWidget  *ui;
    QButtonGroup    *m_speedGroup;

    QList<WordInfo> m_words;
    int    m_currentIndex = 0;
    int    m_correctCount = 0;
    bool   m_isRecording  = false;
    double m_playSpeed    = 1.0;

    QJsonArray    m_keypointBuffer;
    QTimer       *m_stopTimer;
    QTimer       *m_cooldownTimer;
    QTimer       *m_countdownTimer;
    int            m_countdown = 0;
    QElapsedTimer m_recordingStartTime;
};
