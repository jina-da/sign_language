#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QButtonGroup>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>

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
    };

    explicit StudyWidget(QWidget *parent = nullptr);
    ~StudyWidget();

    void setWordList(const QList<WordInfo> &words);
    void onCameraFrame(const QImage &frame);
    void onKeypointFrame(const QJsonObject &keypoint);
    void showResult(const QString &verdict,
                    double confidence,
                    int predictedWordId);

signals:
    void keypointReady(int wordId, bool isDominantLeft, const QJsonArray &keypoints);
    void studyFinished();
    void wordSkipped(int wordId);

private slots:
    void onPrevClicked();   // ← 이전 단어
    void onNextClicked();
    void onSkipClicked();
    void onReplayClicked();
    void onSpeedChanged();
    void onRecordingTimeout();

private:
    void loadWord(int index);
    void startRecording();
    void stopRecording();
    void updateProgress();
    void applyVerdictStyle(const QString &verdict);
    void sendDummyKeypoint();
    void showCompletionDialog();   // 모든 단어 완료 시 팝업

    Ui::StudyWidget *ui;

    QList<WordInfo> m_words;
    int    m_currentIndex = 0;
    bool   m_isRecording  = false;
    double m_playSpeed    = 1.0;

    QJsonArray    m_keypointBuffer;
    QTimer       *m_stopTimer;
    QButtonGroup *m_speedGroup;

    QElapsedTimer m_recordingStartTime;
};
