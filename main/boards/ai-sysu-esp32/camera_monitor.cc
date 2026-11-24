#include "config.h"

#if AI_SYSU_ENABLE_CAMERA && (AI_SYSU_ENABLE_CAMERA_STREAM || AI_SYSU_ENABLE_CAMERA_PREVIEW) && defined(CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE)

#include "camera_monitor.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>

#include "board.h"
#include "display.h"
#include "esp_video.h"
#include "jpg/jpeg_to_image.h"
#include "lvgl_image.h"
#include "mcp_server.h"

static const char* TAG = "CameraMonitor";

CameraMonitor::CameraMonitor(EspVideo* camera) : camera_(camera) {
#if AI_SYSU_ENABLE_CAMERA_STREAM
    stream_ = new CameraStreamServer(camera_);
#endif
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
    xTaskCreate(PreviewTaskEntry, "cam_preview", 10240, this, tskIDLE_PRIORITY + 2, nullptr);
#endif
    RegisterMcpTools();
    ESP_LOGI(TAG, "Camera monitor initialized");
}

CameraMonitor::~CameraMonitor() {
    Stop();
#if AI_SYSU_ENABLE_CAMERA_STREAM
    delete stream_;
    stream_ = nullptr;
#endif
}

bool CameraMonitor::Start() {
    bool started = false;
#if AI_SYSU_ENABLE_CAMERA_STREAM
    if (stream_ != nullptr && stream_->Start()) {
        started = true;
    }
#endif
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
    preview_enabled_ = true;
    started = true;
#endif
    return started;
}

void CameraMonitor::Stop() {
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
    preview_enabled_ = false;
#endif
#if AI_SYSU_ENABLE_CAMERA_STREAM
    if (stream_ != nullptr) {
        stream_->Stop();
    }
#endif
}

std::string CameraMonitor::GetUrl() const {
#if AI_SYSU_ENABLE_CAMERA_STREAM
    if (stream_ != nullptr) {
        return stream_->GetUrl();
    }
#endif
    return {};
}

void CameraMonitor::PreviewTaskEntry(void* arg) {
    static_cast<CameraMonitor*>(arg)->PreviewLoop();
}

bool CameraMonitor::RenderPreviewFrame() {
    if (camera_ == nullptr) {
        return false;
    }
    return camera_->CaptureJpeg([this](const uint8_t* data, size_t len) {
        uint8_t* rgb = nullptr;
        size_t rgb_len = 0;
        size_t width = 0;
        size_t height = 0;
        size_t stride = 0;
        if (jpeg_to_image(const_cast<uint8_t*>(data), len, &rgb, &rgb_len, &width, &height,
                          &stride) != ESP_OK) {
            return false;
        }
        auto display = Board::GetInstance().GetDisplay();
        if (display == nullptr) {
            heap_caps_free(rgb);
            return false;
        }
        display->SetPreviewImage(std::make_unique<LvglAllocatedImage>(
            rgb, rgb_len, static_cast<int>(width), static_cast<int>(height),
            static_cast<int>(stride), LV_COLOR_FORMAT_RGB565));
        return true;
    });
}

void CameraMonitor::PreviewLoop() {
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(CAMERA_PREVIEW_FRAME_INTERVAL_MS);
#endif
    while (true) {
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
        if (preview_enabled_) {
            if (!RenderPreviewFrame()) {
                // 摄像头未就绪时避免刷屏
                vTaskDelay(pdMS_TO_TICKS(500));
                last_wake = xTaskGetTickCount();
                continue;
            }
            // 按固定周期调度：实际帧率 = min(目标帧率, 解码/显示耗时允许的帧率)
            vTaskDelayUntil(&last_wake, period);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
            last_wake = xTaskGetTickCount();
        }
#else
        vTaskDelay(pdMS_TO_TICKS(1000));
#endif
    }
}

void CameraMonitor::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.camera.start_monitor",
        "开始实时摄像头监控。根据固件配置，画面会显示在设备屏幕和/或返回电脑浏览器可打开的 URL；"
        "用户说“开始监控”时调用。",
        PropertyList(), [this](const PropertyList&) -> ToolResult {
            if (!Start()) {
                return std::unexpected("camera not ready or WiFi not connected");
            }

            std::string url = GetUrl();
            bool screen = false;
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
            screen = true;
#endif
            // 只有网页模式时，把访问地址显示在屏幕上；屏幕预览模式不覆盖画面
            if (!screen && !url.empty()) {
                if (auto display = Board::GetInstance().GetDisplay(); display != nullptr) {
                    display->ShowNotification(url.c_str(), 5000);
                }
            }

            std::string json = "{\"screen\":";
            json += screen ? "true" : "false";
            json += ",\"web\":\"";
            json += url;
            json += "\"}";
            return json;
        });

    mcp_server.AddTool("self.camera.stop_monitor", "停止实时摄像头监控", PropertyList(),
                       [this](const PropertyList&) -> ReturnValue {
                           Stop();
                           return true;
                       });

    mcp_server.AddTool("self.camera.get_monitor_url", "获取实时监控状态和网页访问地址",
                       PropertyList(), [this](const PropertyList&) -> ReturnValue {
                           bool screen = false;
#if AI_SYSU_ENABLE_CAMERA_PREVIEW
                           screen = true;
#endif
                           std::string json = "{\"screen\":";
                           json += screen ? "true" : "false";
                           json += ",\"web\":\"";
                           json += GetUrl();
                           json += "\"}";
                           return json;
                       });
}

#endif  // AI_SYSU_ENABLE_CAMERA && (STREAM || PREVIEW) && CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE
