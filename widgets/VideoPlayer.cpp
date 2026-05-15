#include "VideoPlayer.h"

#include <QDebug>
#include <QEvent>
#include <QFile>
#include <QDir>
#include <QUrl>
#include <QNetworkRequest>
#include <QSet>
#include <QTime>

VideoPlayer::VideoPlayer(QWidget *parent)
    : QWidget(parent)
    , m_player(new QMediaPlayer(this))
    , m_videoWidget(new QVideoWidget(this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_playBtn(new QPushButton("▶", this))
    , m_loopBtn(new QPushButton("반복", this))
    , m_progressSlider(new QSlider(Qt::Horizontal, this))
    , m_timeLabel(new QLabel("0:00 / 0:00", this))
{
    // ── 레이아웃 ──────────────────────────────────────
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // 영상 영역
    root->addWidget(m_videoWidget, 1);

    // 컨트롤 바
    auto *controlBar = new QWidget(this);
    controlBar->setFixedHeight(36);
    controlBar->setStyleSheet(
        "background: rgba(0,0,0,0.6); border-radius: 0 0 10px 10px;");

    auto *ctrlLayout = new QHBoxLayout(controlBar);
    ctrlLayout->setContentsMargins(8, 4, 8, 4);
    ctrlLayout->setSpacing(8);

    // 재생/일시정지 버튼
    m_playBtn->setFixedSize(26, 26);
    m_playBtn->setStyleSheet(
        "QPushButton { background: transparent; color: white;"
        "  border: none; font-size: 14px; }"
        "QPushButton:hover { color: #97C459; }");
    m_playBtn->setEnabled(false);

    // 반복 재생 버튼 — 컨트롤 바와 동일한 스타일, 토글 시 배경으로 활성 표시
    m_loopBtn->setFixedHeight(22);
    m_loopBtn->setCheckable(true);
    m_loopBtn->setStyleSheet(
        "QPushButton {"
        "  background: transparent;"
        "  color: #aaa;"
        "  border: 1px solid #666;"
        "  border-radius: 3px;"
        "  font-size: 11px;"
        "  padding: 0 6px;"
        "}"
        "QPushButton:hover { color: white; border-color: #aaa; }"
        "QPushButton:checked {"
        "  background: #97C459;"
        "  color: #1a1a2e;"
        "  border-color: #97C459;"
        "  font-weight: bold;"
        "}");

    // progress bar (슬라이더)
    m_progressSlider->setRange(0, 0);
    m_progressSlider->setStyleSheet(
        "QSlider::groove:horizontal { background:#555; height:4px; border-radius:2px; }"
        "QSlider::sub-page:horizontal { background:#97C459; height:4px; border-radius:2px; }"
        "QSlider::handle:horizontal { background:white; width:10px; height:10px;"
        "  margin:-3px 0; border-radius:5px; }");

    // 시간 레이블
    m_timeLabel->setStyleSheet("color: white; font-size: 11px;");
    m_timeLabel->setFixedWidth(90);

    ctrlLayout->addWidget(m_playBtn);
    ctrlLayout->addWidget(m_progressSlider, 1);
    ctrlLayout->addWidget(m_timeLabel);
    ctrlLayout->addWidget(m_loopBtn);

    root->addWidget(controlBar);
    setLayout(root);

    // ── QMediaPlayer 설정 ────────────────────────────
    m_player->setVideoOutput(m_videoWidget);

    // 영상 부분 클릭 → 재생/일시정지
    m_videoWidget->installEventFilter(this);

    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this,     &VideoPlayer::onMediaStatusChanged);
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this,     &VideoPlayer::onPlaybackStateChanged);
    connect(m_player, &QMediaPlayer::durationChanged,
            this,     &VideoPlayer::onDurationChanged);
    connect(m_player, &QMediaPlayer::positionChanged,
            this,     &VideoPlayer::onPositionChanged);

    // ── 컨트롤 연결 ──────────────────────────────────
    connect(m_playBtn, &QPushButton::clicked,
            this,      &VideoPlayer::togglePlayPause);
    connect(m_progressSlider, &QSlider::sliderMoved,
            this,             &VideoPlayer::onSliderMoved);
    connect(m_loopBtn, &QPushButton::toggled,
            this, [this](bool checked) {
        m_loopEnabled = checked;
        qDebug() << "[Video] 반복 재생:" << (checked ? "ON" : "OFF");
    });

    // ── 다운로드 완료 ─────────────────────────────────
    connect(m_networkManager, &QNetworkAccessManager::finished,
            this,             &VideoPlayer::onDownloadFinished);

    // 로컬 캐시 폴더 생성
    QDir().mkpath(m_saveDir);

    setStyleSheet("background: #1a1a2e; border-radius: 10px;");
    m_videoWidget->setStyleSheet("background: #1a1a2e;");
}

VideoPlayer::~VideoPlayer()
{
    m_player->stop();
}

// ─────────────────────────────────────────────────────────────
// setSession
// ─────────────────────────────────────────────────────────────
void VideoPlayer::setSession(const QString &serverHost,
                              const QString &sessionToken)
{
    m_serverHost   = serverHost;
    m_sessionToken = sessionToken;
    qDebug() << "[Video] 세션 세팅: host=" << serverHost;
}

// ─────────────────────────────────────────────────────────────
// play — 소스 세팅만 하고 재생은 하지 않음 (수동 재생)
// 버퍼가 준비되면 컨트롤 활성화
// ─────────────────────────────────────────────────────────────
void VideoPlayer::play(const QString &videoCdnUrl, const QString &filename)
{
    if (videoCdnUrl.isEmpty() || filename.isEmpty()) {
        qWarning() << "[Video] URL 또는 파일명이 비어있습니다.";
        emit errorOccurred("영상 정보가 없습니다.");
        return;
    }

    // 이전 재생 중지
    m_player->stop();
    m_playBtn->setEnabled(false);
    m_playBtn->setText("▶");
    m_progressSlider->setValue(0);
    m_timeLabel->setText("0:00 / 0:00");
    QString local = localPath(filename);

    if (QFile::exists(local)) {
        qDebug() << "[Video] 로컬 소스 세팅:" << local;
        m_player->setSource(QUrl::fromLocalFile(local));
    } else {
        QString builtUrl = buildUrl(videoCdnUrl);
        qDebug() << "[Video] 스트리밍 소스 세팅:" << builtUrl;
        m_player->setSource(QUrl(builtUrl));
        downloadBackground(builtUrl, filename);
    }
    // 실제 재생은 사용자가 ▶ 버튼을 누를 때 시작
}

// ─────────────────────────────────────────────────────────────
// setSpeed
// ─────────────────────────────────────────────────────────────
void VideoPlayer::setSpeed(double speed)
{
    m_speed = speed;
    m_player->setPlaybackRate(speed);
    qDebug() << "[Video] 재생 속도:" << speed;
}

// ─────────────────────────────────────────────────────────────
// togglePlayPause
// ─────────────────────────────────────────────────────────────
bool VideoPlayer::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_videoWidget && event->type() == QEvent::MouseButtonPress) {
        if (m_playBtn->isEnabled())   // 버퍼 준비 완료 후에만 동작
            togglePlayPause();
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

void VideoPlayer::resetToStart()
{
    m_player->setPosition(0);
    m_player->pause();
    m_playBtn->setText("▶");
}

void VideoPlayer::togglePlayPause()
{
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else {
        m_player->setPlaybackRate(m_speed);
        m_player->play();
    }
}

// ─────────────────────────────────────────────────────────────
// onMediaStatusChanged
// 버퍼 준비 완료 → 재생 버튼 활성화 (자동 재생 없음)
// ─────────────────────────────────────────────────────────────
void VideoPlayer::onMediaStatusChanged(QMediaPlayer::MediaStatus status)
{
    switch (status) {
    case QMediaPlayer::LoadedMedia:
    case QMediaPlayer::BufferedMedia:
        // 소스 로드 완료 → 재생 버튼 활성화
        m_playBtn->setEnabled(true);
        qDebug() << "[Video] 버퍼 준비 완료 → 재생 버튼 활성화";
        break;
    case QMediaPlayer::EndOfMedia:
        if (m_loopEnabled) {
            // 반복 재생: 처음으로 되돌려 재생
            m_player->setPosition(0);
            m_player->play();
            qDebug() << "[Video] 반복 재생";
        } else {
            m_playBtn->setText("▶");
            emit playbackFinished();
            qDebug() << "[Video] 재생 완료";
        }
        break;
    case QMediaPlayer::InvalidMedia:
        qWarning() << "[Video] 재생 오류: 잘못된 미디어";
        emit errorOccurred("영상을 재생할 수 없습니다.");
        break;
    default:
        break;
    }
}

// ─────────────────────────────────────────────────────────────
// onPlaybackStateChanged — 버튼 텍스트 동기화
// ─────────────────────────────────────────────────────────────
void VideoPlayer::onPlaybackStateChanged(QMediaPlayer::PlaybackState state)
{
    if (state == QMediaPlayer::PlayingState)
        m_playBtn->setText("■");
    else
        m_playBtn->setText("▶");
}

// ─────────────────────────────────────────────────────────────
// onDurationChanged — 슬라이더 최대값 및 총 시간 갱신
// ─────────────────────────────────────────────────────────────
void VideoPlayer::onDurationChanged(qint64 duration)
{
    m_progressSlider->setRange(0, static_cast<int>(duration / 1000));
    updateTimeLabel(m_player->position(), duration);
}

// ─────────────────────────────────────────────────────────────
// onPositionChanged — 슬라이더 위치 및 시간 갱신
// ─────────────────────────────────────────────────────────────
void VideoPlayer::onPositionChanged(qint64 position)
{
    // 사용자가 슬라이더를 드래그 중일 때는 업데이트 안 함
    if (!m_progressSlider->isSliderDown())
        m_progressSlider->setValue(static_cast<int>(position / 1000));

    updateTimeLabel(position, m_player->duration());
}

// ─────────────────────────────────────────────────────────────
// onSliderMoved — 슬라이더 드래그 → seek
// ─────────────────────────────────────────────────────────────
void VideoPlayer::onSliderMoved(int position)
{
    m_player->setPosition(static_cast<qint64>(position) * 1000);
}

// ─────────────────────────────────────────────────────────────
// updateTimeLabel — "현재 / 전체" 형식으로 시간 표시
// ─────────────────────────────────────────────────────────────
void VideoPlayer::updateTimeLabel(qint64 position, qint64 duration)
{
    auto fmt = [](qint64 ms) -> QString {
        int s = static_cast<int>(ms / 1000);
        return QString("%1:%2")
            .arg(s / 60)
            .arg(s % 60, 2, 10, QChar('0'));
    };
    m_timeLabel->setText(fmt(position) + " / " + fmt(duration));
}

// ─────────────────────────────────────────────────────────────
// downloadBackground
// ─────────────────────────────────────────────────────────────
void VideoPlayer::downloadBackground(const QString &url,
                                      const QString &filename)
{
    if (m_downloading.contains(filename)) {
        qDebug() << "[Video] 이미 다운로드 중:" << filename;
        return;
    }

    m_downloading.insert(filename);

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::User, QVariant(filename));
    m_networkManager->get(request);
    qDebug() << "[Video] 백그라운드 다운로드 시작:" << filename;
}

// ─────────────────────────────────────────────────────────────
// onDownloadFinished
// ─────────────────────────────────────────────────────────────
void VideoPlayer::onDownloadFinished(QNetworkReply *reply)
{
    QString filename = reply->request()
                           .attribute(QNetworkRequest::User)
                           .toString();
    m_downloading.remove(filename);

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[Video] 다운로드 실패:" << filename
                   << reply->errorString();
        reply->deleteLater();
        return;
    }

    QString local = localPath(filename);
    QFile file(local);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(reply->readAll());
        file.close();
        qDebug() << "[Video] 로컬 저장 완료:" << local;
    } else {
        qWarning() << "[Video] 파일 저장 실패:" << local;
    }

    reply->deleteLater();
}

// ─────────────────────────────────────────────────────────────
// 헬퍼
// ─────────────────────────────────────────────────────────────
QString VideoPlayer::localPath(const QString &filename) const
{
    return m_saveDir + "/" + filename;
}

QString VideoPlayer::buildUrl(const QString &videoCdnUrl) const
{
    QString url = videoCdnUrl;
    url.replace("localhost", m_serverHost);
    url.replace("127.0.0.1", m_serverHost);

    if (!url.contains("session_token")) {
        url += (url.contains('?') ? "&" : "?");
        url += "session_token=" + m_sessionToken;
    }

    qDebug() << "[Video] 최종 URL:" << url;
    return url;
}
