#ifndef _CAMERA_MONITOR_H_
#define _CAMERA_MONITOR_H_

#include "config.h"

#include <atomic>
#include <string>

#if AI_SYSU_ENABLE_CAMERA_STREAM
#include "camera_stream.h"
#endif

class EspVideo;

// 实时摄像头监控：可同时输出到本机 LCD 和电脑网页（MJPEG HTTP）。
class CameraMonitor {
public:
    explicit CameraMonitor(EspVideo* camera);
    ~CameraMonitor();

    bool Start();
    void Stop();
    std::string GetUrl() const;
    void RegisterMcpTools();

private:
    static void PreviewTaskEntry(void* arg);
    void PreviewLoop();
    bool RenderPreviewFrame();

    EspVideo* camera_;
#if AI_SYSU_ENABLE_CAMERA_STREAM
    CameraStreamServer* stream_ = nullptr;
#endif
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
    std::atomic<bool> preview_enabled_{false};
#endif
};

#endif  // _CAMERA_MONITOR_H_
