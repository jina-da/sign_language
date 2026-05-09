#pragma once

#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QImage>

QT_BEGIN_NAMESPACE
namespace Ui { class DictWidget; }
QT_END_NAMESPACE

/**
 * DictWidget — 수화 사전 화면
 *
 * 정방향 탭: 단어 입력 → 300ms 디바운스 → REQ_DICT_SEARCH → 영상+뜻 표시
 * 역방향 탭: 수화 입력 (공수 자세 감지) → REQ_DICT_REVERSE → 단어+뜻 표시
 */
class DictWidget : public QWidget
{
    Q_OBJECT

public:
    explicit DictWidget(QWidget *parent = nullptr);
    ~DictWidget();

    // KeypointClient 슬롯
    void onCameraFrame(const QImage &frame);
    void onKeypointFrame(const QJsonObject &keypoint);

    // AppController 응답 처리
    void showForwardResult(const QString &word,
                           const QString &description,
                           const QString &videoCdnUrl);
    void showReverseResult(const QString &word,
                           const QString &description);
    void showSearchError(const QString &message);

signals:
    // 정방향: 단어 → 영상
    void forwardSearchRequested(const QString &query);

    // 역방향: 수화 → 단어
    void reverseSearchRequested(const QJsonArray &keypoints);

private slots:
    void onTabForwardClicked();
    void onTabReverseClicked();
    void onSearchTextChanged(const QString &text);
    void onSearchBtnClicked();
    void onDebounceTimeout();
    void onRecordingTimeout();

private:
    void switchTab(int index);   // 0: 정방향, 1: 역방향
    void startRecording();
    void stopRecording();

    Ui::DictWidget *ui;

    // 디바운스 타이머 (정방향)
    QTimer *m_debounceTimer;

    // 공수 자세 감지 (역방향)
    QTimer       *m_stopTimer;
    QTimer       *m_cooldownTimer;  // 녹화 종료 후 재시작 방지 (1.5초)
    QElapsedTimer m_recordingStartTime;
    bool          m_isRecording  = false;
    QJsonArray    m_keypointBuffer;

    int m_currentTab = 0;   // 0: 정방향, 1: 역방향
};
