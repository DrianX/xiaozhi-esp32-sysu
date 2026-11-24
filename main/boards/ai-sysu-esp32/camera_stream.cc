#include "config.h"

#if AI_SYSU_ENABLE_CAMERA && AI_SYSU_ENABLE_CAMERA_STREAM && defined(CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE)

#include "camera_stream.h"

#include <cstdio>

#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <wifi_manager.h>

#include "esp_video.h"

static const char* TAG = "CameraStream";

CameraStreamServer::CameraStreamServer(EspVideo* camera) : camera_(camera) {}

CameraStreamServer::~CameraStreamServer() { Stop(); }

std::string CameraStreamServer::GetUrl() const {
    std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty()) {
        return {};
    }
    return "http://" + ip + "/";
}

esp_err_t CameraStreamServer::RootHandler(httpd_req_t* req) {
    static const char* html =
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>XiaoZhi Camera</title>"
        "<style>body{margin:0;background:#000;display:flex;align-items:center;"
        "justify-content:center;height:100vh}img{max-width:100%;max-height:100vh}</style>"
        "</head><body><img src=\"/stream\" alt=\"camera stream\"></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return httpd_resp_send(req, html, HTTPD_RESP_USE_STRLEN);
}

esp_err_t CameraStreamServer::StreamHandler(httpd_req_t* req) {
    auto* self = static_cast<CameraStreamServer*>(req->user_ctx);
    if (self == nullptr || self->camera_ == nullptr) {
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "camera not ready");
    }

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_hdr(req, "Connection", "close");

    self->streaming_ = true;
    while (self->streaming_) {
        bool sent = self->camera_->CaptureJpeg([req](const uint8_t* data, size_t len) {
            char header[96];
            int header_len =
                snprintf(header, sizeof(header),
                         "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                         static_cast<unsigned>(len));
            if (httpd_resp_send_chunk(req, header, header_len) != ESP_OK) {
                return false;
            }
            if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(data), len) != ESP_OK) {
                return false;
            }
            return httpd_resp_send_chunk(req, "\r\n", 2) == ESP_OK;
        });
        if (!sent) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(CAMERA_STREAM_FRAME_INTERVAL_MS));
    }

    self->streaming_ = false;
    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}

bool CameraStreamServer::Start() {
    if (server_ != nullptr) {
        return true;
    }
    if (camera_ == nullptr) {
        ESP_LOGE(TAG, "camera is not initialized");
        return false;
    }
    if (WifiManager::GetInstance().GetIpAddress().empty()) {
        ESP_LOGW(TAG, "WiFi is not connected, cannot start camera stream");
        return false;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 4;
    config.stack_size = 10240;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        server_ = nullptr;
        return false;
    }

    httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = RootHandler,
        .user_ctx = this,
    };
    httpd_uri_t stream = {
        .uri = "/stream",
        .method = HTTP_GET,
        .handler = StreamHandler,
        .user_ctx = this,
    };
    if (httpd_register_uri_handler(server_, &root) != ESP_OK ||
        httpd_register_uri_handler(server_, &stream) != ESP_OK) {
        ESP_LOGE(TAG, "register uri handler failed");
        httpd_stop(server_);
        server_ = nullptr;
        return false;
    }

    ESP_LOGW(TAG, "Camera monitor started: %s", GetUrl().c_str());
    return true;
}

void CameraStreamServer::Stop() {
    streaming_ = false;
    if (server_ != nullptr) {
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGW(TAG, "Camera monitor stopped");
    }
}

#endif  // AI_SYSU_ENABLE_CAMERA && AI_SYSU_ENABLE_CAMERA_STREAM && CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
