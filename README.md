# xiaozhi-esp32-sysu · 小智 AI × 轮足机器人

本仓库是小智 AI 语音助手固件与轮足机器人控制程序的联合工程：
小智 AI 负责「听懂人话」，轮足机器人负责「执行动作」，二者通过 UART 串口连接。

本文档是仓库总入口，写给 AI 工具（Claude Code / Cursor 等）与后续维护者，
用来快速分清两个子工程、各自的构建方式，以及它们之间如何协作。

---

## 目录结构

```
xiaozhi-esp32-sysu/
├── 1.xiaozhi/            小智 AI 语音助手固件（ESP-IDF，C/C++）
├── 2.foot_wheel_robot/   轮足机器人控制程序（PlatformIO + Arduino，ESP32）
└── README.md             本文件（仓库总览）
```

## 子工程一：1.xiaozhi —— 小智 AI 语音助手

- **定位**：语音交互入口。唤醒、识别、对话后，通过 MCP 工具或串口对外输出控制指令。
- **技术栈**：ESP-IDF（最低 v6.0.1，推荐 v6.1），C/C++。
- **构建**（在 `1.xiaozhi/` 目录内）：

  ```sh
  source /path/to/esp-idf/export.sh
  python3 scripts/build.py --list-boards
  python3 scripts/build.py <board-directory> --name <variant-name>
  ```

- **自述文档**：
  - `1.xiaozhi/README.md`（英文）/ `README_zh.md`（中文）/ `README_ja.md`（日文）
  - `1.xiaozhi/AGENTS.md`（AI 开发规范，改代码前必读）
  - `1.xiaozhi/docs/`（板卡、协议等详细文档）

## 子工程二：2.foot_wheel_robot —— 轮足机器人

- **定位**：机器人的平衡控制与动作执行（前进/后退/转向/转圈/高度/坐下/起立/跳跃）。
- **技术栈**：PlatformIO + Arduino 框架，芯片 ESP32（board: `esp32dev`）。
- **构建**（在 `2.foot_wheel_robot/` 目录内）：

  ```sh
  pio run            # 编译
  pio run -t upload  # 烧录
  ```

- **自述文档**：
  - `2.foot_wheel_robot/README.md`（工程历史与改动记录）
  - `2.foot_wheel_robot/README-串口控制.md`（给控制板用的串口协议，**必读**）
- **核心源码**（都在 `2.foot_wheel_robot/src/`）：
  - `main.cpp`：平衡控制、运动、跳跃、坐下/起立等主逻辑
  - `SerialCommand.cpp` / `SerialCommand.h`：串口指令解析（对外暴露的控制接口）
  - `RobotState.h`：舵机参数与 `Wrobot` 运动状态结构体（两文件共用）

## 二者如何协作（串口）

小智 AI（控制板）通过 UART 串口发英文关键词指令，机器人接收后执行动作并回 `OK` / `ERR`。

| 控制板（1.xiaozhi） | 机器人（2.foot_wheel_robot） | 说明 |
| :---: | :---: | :--- |
| TX | IO4（RX） | 机器人接收指令 |
| RX | IO15（TX） | 机器人返回应答 |
| GND | GND | **必须共地** |

- 串口参数：**115200 8N1**，指令以换行结尾。
- 握手：机器人上电初始化后主动发 `READY`（每 2s 重发）→ 控制板回 `HELLO` → 机器人回 `OK` 并**自动起立**。
- 完整指令表（`FORWARD` / `BACK` / `LEFT` / `RIGHT` / `SPIN` / `HEIGHT` / `JUMP` / `STOP` …）见 `2.foot_wheel_robot/README-串口控制.md`。

## ⚠️ 给 AI 工具的关键提醒：两套构建系统，不要混用

两个子工程互不依赖、各自编译烧录，唯一交集是「串口协议」。

| | 1.xiaozhi | 2.foot_wheel_robot |
| :--- | :--- | :--- |
| 构建系统 | ESP-IDF（`idf.py` / `scripts/build.py`） | PlatformIO（`pio run`） |
| 框架 | ESP-IDF C/C++ | Arduino |
| 芯片 | 多平台（S3 / P4 / C3 / C5 / C6 等） | ESP32（`esp32dev`） |
| 主入口 | `main/application.cpp` 等 | `src/main.cpp` |
| 代码风格 | Google C++（见 `1.xiaozhi/AGENTS.md`） | 见 `2.foot_wheel_robot/README.md` |

- 改 `1.xiaozhi` 时：遵守其 `AGENTS.md` 规则——board 抽象、状态机、不要阻塞主循环/音频任务等。
- 改 `2.foot_wheel_robot` 时：平衡与动作在 `main.cpp`，新增对外指令在 `SerialCommand.cpp`，改完同步更新 `README-串口控制.md`。

## 常见改动速查

| 想做什么 | 改哪里 |
| :--- | :--- |
| 机器人新增一种动作（如新跳跃方向） | `2.foot_wheel_robot/src/SerialCommand.cpp`，并同步更新 `README-串口控制.md` |
| 小智 AI 侧触发串口指令 | `1.xiaozhi` 的 MCP 工具 / 串口输出逻辑 |
| 调整平衡、高度、跳跃参数 | `2.foot_wheel_robot/src/main.cpp` |
| 新增小智 AI 开发板 | `1.xiaozhi` 的 board 链（见其 `AGENTS.md`） |
