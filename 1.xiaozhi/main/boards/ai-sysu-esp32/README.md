# AI SYSU ESP32

自定义硬件板（ai-sysu-esp32），基于 `bread-compact-wifi-lcd` 移植而来。

## 硬件

- 主控：ESP32-S3
- 网络：Wi-Fi
- 显示：SPI ST7789 240x320 IPS（默认 LCD 配置）
- 音频：I2S 直连（无外部 codec），Simplex 模式
- 按键：BOOT 按键
- LED：板载 LED

## 状态

当前 IO 引脚暂与 `bread-compact-wifi-lcd` 保持一致，待根据实际原理图修改
`config.h` 中的引脚定义。

## 构建

使用本目录下的 `build.sh` 一键编译并合并固件（需在 Git Bash 中运行，
首次使用前先 `cd $IDF_PATH && ./install.sh esp32s3` 安装工具链）：

```bash
./main/boards/ai-sysu-esp32/build.sh
```

生成的单文件合并固件为 `build/merged-binary.bin`。

也可以使用项目的统一构建入口：

```bash
source /path/to/esp-idf/export.sh
python scripts/build.py ai-sysu-esp32
```
