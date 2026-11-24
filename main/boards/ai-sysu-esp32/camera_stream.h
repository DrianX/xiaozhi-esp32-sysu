#ifndef _CAMERA_STREAM_H_
#define _CAMERA_STREAM_H_

#include <atomic>
#include <string>

#include <esp_http_server.h>

class EspVideo;

// 本地 MJPEG 视频流：电脑浏览器打开 http://<设备IP>/ 即可看到摄像头画面。
class CameraStreamServer {
public:
    explicit CameraStreamServer(EspVideo* camera);
    ~CameraStreamServer();

    bool Start();
    void Stop();
    std::string GetUrl() const;

private:
    static esp_err_t RootHandler(httpd_req_t* req);
    static esp_err_t StreamHandler(httpd_req_t* req);

    EspVideo* camera_;
    httpd_handle_t server_ = nullptr;
    std::atomic<bool> streaming_{false};
};

#endif  // _CAMERA_STREAM_H_
