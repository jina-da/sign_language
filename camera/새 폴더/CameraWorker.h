#pragma once

#include <QObject>
#include <QImage>
#include <atomic>

/**
 * CameraWorker — 별도 스레드에서 웹캠 프레임을 캡처
 *
 * 사용법:
 *   QThread *thread = new QThread;
 *   CameraWorker *worker = new CameraWorker;
 *   worker->moveToThread(thread);
 *   connect(thread, &QThread::started, worker, &CameraWorker::start);
 *   connect(worker, &CameraWorker::frameReady, studyWidget, &StudyWidget::onCameraFrame);
 *   thread->start();
 *
 * 중지:
 *   worker->stop();
 *   thread->quit();
 *   thread->wait();
 */
class CameraWorker : public QObject
{
    Q_OBJECT

public:
    explicit CameraWorker(int deviceIndex = 0, QObject *parent = nullptr);

public slots:
    // QThread::started 시그널에 연결 — 캡처 루프 시작
    void start();

    // 외부에서 호출해서 루프 종료
    void stop();

signals:
    // 새 프레임마다 발생 (메인 스레드의 StudyWidget::onCameraFrame으로 전달)
    void frameReady(const QImage &frame);

    // 카메라 열기 실패 시 발생
    void cameraError(const QString &message);

private:
    int m_deviceIndex;
    std::atomic<bool> m_running{false};
};
