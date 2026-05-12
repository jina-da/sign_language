#pragma once

#include <QWidget>
#include <QMediaPlayer>
#include <QVideoWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDir>
#include <QUrl>
#include <QPushButton>
#include <QSlider>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

/**
 * VideoPlayer — 스트리밍 + 로컬 캐싱 영상 재생 위젯
 *
 * 동작 방식:
 *   - 로컬 캐시(videos/) 있음 → 즉시 로컬 재생
 *   - 없음 → 스트리밍 재생 + 백그라운드 다운로드 동시 진행
 *   - 다음 재생부터 로컬 캐시 사용
 *
 * 서버: FastAPI :8000
 * URL: http://{host}:8000/video/{filename}?session_token={token}
 */
class VideoPlayer : public QWidget
{
    Q_OBJECT

public:
    explicit VideoPlayer(QWidget *parent = nullptr);
    ~VideoPlayer();

    // 로그인 후 세션 정보 세팅 (필수)
    void setSession(const QString &serverHost, const QString &sessionToken);

    // 영상 재생 — video_cdn_url과 파일명을 받아 처리
    void play(const QString &videoCdnUrl, const QString &filename);

    // 재생 속도 설정 (0.25 / 0.5 / 1.0 / 1.5 / 2.0)
    void setSpeed(double speed);

    // 재생/일시정지 토글
    void togglePlayPause();

    // 현재 재생 중인 word_id
    int currentWordId() const { return m_currentWordId; }
    void setCurrentWordId(int id) { m_currentWordId = id; }

signals:
    void playbackStarted();
    void playbackFinished();
    void errorOccurred(const QString &message);

private slots:
    void onDownloadFinished(QNetworkReply *reply);
    void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
    void onPlaybackStateChanged(QMediaPlayer::PlaybackState state);
    void onDurationChanged(qint64 duration);
    void onPositionChanged(qint64 position);
    void onSliderMoved(int position);
    void updateTimeLabel(qint64 position, qint64 duration);

private:
    void playLocal(const QString &localPath);
    void playStream(const QString &url);
    void downloadBackground(const QString &url, const QString &filename);   // buildUrl 처리된 URL
    QString localPath(const QString &filename) const;
    QString buildUrl(const QString &videoCdnUrl) const;

    QMediaPlayer           *m_player;
    QVideoWidget           *m_videoWidget;
    QNetworkAccessManager  *m_networkManager;

    // 컨트롤 UI
    QPushButton            *m_playBtn;
    QSlider                *m_progressSlider;
    QLabel                 *m_timeLabel;

    QString m_serverHost;
    QString m_sessionToken;
    QString m_saveDir    = "videos";
    double  m_speed      = 1.0;
    int     m_currentWordId = -1;

    // 다운로드 중인 파일명 추적 (중복 다운로드 방지)
    QSet<QString> m_downloading;
};
