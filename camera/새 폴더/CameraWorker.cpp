#include "CameraWorker.h"

#include <QDebug>
#include <QThread>
#include <opencv2/opencv.hpp>

CameraWorker::CameraWorker(int deviceIndex, QObject *parent)
    : QObject(parent)
    , m_deviceIndex(deviceIndex)
{
}

// ─────────────────────────────────────────────────────────────
// start() — 캡처 루프
// QThread::started 시그널에 연결해서 스레드 시작 시 자동 호출
// ─────────────────────────────────────────────────────────────
void CameraWorker::start()
{
    cv::VideoCapture cap(m_deviceIndex);

    if (!cap.isOpened()) {
        qWarning() << "[Camera] 카메라를 열 수 없습니다. 장치 인덱스:" << m_deviceIndex;
        emit cameraError("카메라를 열 수 없습니다.");
        return;
    }

    // 해상도 설정 (640x480)
    cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);

    qDebug() << "[Camera] 카메라 시작:"
             << cap.get(cv::CAP_PROP_FRAME_WIDTH) << "x"
             << cap.get(cv::CAP_PROP_FRAME_HEIGHT);

    m_running = true;
    cv::Mat frame;

    while (m_running) {
        cap.read(frame);

        if (frame.empty()) {
            qWarning() << "[Camera] 빈 프레임";
            continue;
        }

        // OpenCV BGR → Qt RGB 변환
        cv::Mat rgb;
        cv::cvtColor(frame, rgb, cv::COLOR_BGR2RGB);

        QImage img(
            rgb.data,
            rgb.cols,
            rgb.rows,
            static_cast<int>(rgb.step),
            QImage::Format_RGB888);

        // QImage는 외부 버퍼를 참조하므로 복사해서 emit
        emit frameReady(img.copy());

        // 약 30fps (33ms)
        QThread::msleep(33);
    }

    cap.release();
    qDebug() << "[Camera] 카메라 종료";
}

// ─────────────────────────────────────────────────────────────
// stop() — 캡처 루프 종료 요청
// ─────────────────────────────────────────────────────────────
void CameraWorker::stop()
{
    m_running = false;
}
