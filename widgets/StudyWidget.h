#pragma once

#include <QWidget>
#include <QTimer>
#include <QButtonGroup>
#include <QJsonArray>
#include <QJsonObject>

QT_BEGIN_NAMESPACE
namespace Ui { class StudyWidget; }
QT_END_NAMESPACE

/**
 * StudyWidget — 학습 모드 화면
 *
 * 흐름:
 *   1. setWordList()로 단어 목록 수신
 *   2. loadWord()로 현재 단어 표시 + 영상 재생
 *   3. 카메라 프레임 → onCameraFrame()으로 계속 수신
 *   4. 공수 자세 감지 → 녹화 시작
 *   5. 1.5초 정지 → 녹화 종료 → keypointReady 시그널
 *   6. keypoint_result 수신 → showResult()
 *   7. 다음 단어 또는 완료
 *
 * ※ MediaPipe 연동(카메라 캡처·관절 추출)은 별도 KeypointExtractor 클래스에서 담당.
 *    이 위젯은 UI 표시와 흐름 제어만 담당한다.
 */
class StudyWidget : public QWidget
{
    Q_OBJECT

public:
    struct WordInfo {
        int     id;
        QString word;
        QString category;
        int     difficulty;
    };

    explicit StudyWidget(QWidget *parent = nullptr);
    ~StudyWidget();

    // 서버에서 받은 단어 목록 세팅 (word_list_result)
    void setWordList(const QList<WordInfo> &words);

    // 카메라 프레임 수신 (KeypointExtractor → 여기로)
    void onCameraFrame(const QImage &frame);

    // 서버 인식 결과 수신 (keypoint_result)
    void showResult(const QString &verdict,
                    double confidence,
                    int predictedWordId);

signals:
    // 키포인트 전송 요청 → AppController → TcpClient
    void keypointReady(int wordId,
                       bool isDominantLeft,
                       const QJsonArray &keypoints);

    // 학습 완료 (모든 단어 소진)
    void studyFinished();

    // 건너뛰기
    void wordSkipped(int wordId);

private slots:
    void onNextClicked();
    void onSkipClicked();
    void onReplayClicked();
    void onSpeedChanged();
    void onRecordingTimeout(); // 1.5초 정지 타이머

private:
    void loadWord(int index);
    void startRecording();
    void stopRecording();
    void updateProgress();
    void applyVerdictStyle(const QString &verdict);

    Ui::StudyWidget *ui;

    QList<WordInfo> m_words;
    int  m_currentIndex  = 0;
    bool m_isRecording   = false;
    double m_playSpeed   = 1.0;

    // 키포인트 누적 버퍼 (녹화 중)
    QJsonArray m_keypointBuffer;

    // 1.5초 정지 감지 타이머
    QTimer *m_stopTimer;

    // 속도 버튼 그룹 (단일 선택)
    QButtonGroup *m_speedGroup;
};
