# STM32 桌面宠物（WiFi AI 语音版）

一只会动、会卖萌、还会**开口说话**的桌面小宠物。

在原版桌宠（作者 **Sngels_wyh**，适配 v1.2.1 电路板）的基础上做了二次开发：
外接一块 **ESP32-S3** 作为联网网关，把桌宠接上了 **DeepSeek**，
于是它能听懂"AI 对话"这个命令词，然后**用中文语音把回答念出来**。

```
你说"AI对话" ─► SU-03T 语音模块 ─► STM32 桌宠
                                      │  $AI:Hello  (USART3 @9600)
                                      ▼
                              ESP32-S3 网关  ──HTTP──►  FastAPI 服务器  ──►  DeepSeek
                                      │                    /chat  /tts
                                      │  ◄── 中文回复 ──
                     ┌────────────────┴────────────────┐
                     ▼                                 ▼
            STM32: OLED 显示随机表情          喇叭: 念出中文回复
```

---

## 功能

**原版桌宠自带**
- 4 路舵机动作 + 1 路尾巴舵机
- 2 路 LED 呼吸灯
- OLED 表情显示（快乐 / 狂热 / 打招呼 / 眨眼等）
- SU-03T 离线语音命令词控制

**二次开发新增**
- 说"AI 对话" 触发，进入 AI 模式（OLED 显示 Listening）
- 中文回复 → OLED 显示**随机表情** + 喇叭**语音朗读**
- 20 秒无回复自动退出 AI 模式
- 蓝牙 / WiFi 双通道（H11 座二选一）

---

## 硬件

| 部件 | 型号 | 连接 |
|------|------|------|
| 主控 | STM32F103C8T6 | — |
| 显示 | OLED 1.3" **SH1106**（I2C） | PB8=SCL, PB9=SDA, 地址 0x78 |
| 舵机 | 4 路 | TIM2, PA0–PA3 |
| 尾巴舵机 | 1 路 | TIM3_CH1, PA6 |
| 呼吸灯 | 2 路 | TIM3_CH3=PB0, TIM3_CH4=PB1 |
| 语音模块 | SU-03T | USART1, PA9/PA10 @9600 |
| 电量检测 | 分压 → ADC | PA4 |
| 联网网关 | **ESP32-S3**（二次开发加装） | USART3, PB10=TX / PB11=RX @9600 |
| 功放 | **MAX98357A**（I2S） | ESP32 GPIO5/4/6 |
| 蓝牙 | HC-05 兼容（**已停用**，让位给 WiFi） | USART3 @115200 |

> ESP32-S3 通过底板 **H11** 座（原蓝牙/WiFi 模块座）飞线接入，**不需要改 PCB**。
> H11 针序：`1=3.3V, 2=PB10, 3=PB11, 4=GND`。

---

## 仓库结构

```
├── Project.uvprojx        Keil MDK 工程（STM32 主工程，标准外设库 SPL）
├── HardWare/              STM32 外设驱动（OLED / 舵机 / 呼吸灯 / WiFi / 语音…）
├── Library/               STM32F10x 标准外设库
├── Start/                 启动文件与 CMSIS
├── User/                  main.c / 中断 / 库配置
├── docs/
│   └── 开发记录.md         完整开发过程记录（硬件选型、踩坑、排障全过程）
└── tools/
    ├── README.md          ★ WiFi AI 链路的详细说明与接线图（要看细节从这进）
    ├── server/            FastAPI 中转服务器（/chat 接 DeepSeek，/tts 用 edge-tts 出中文语音）
    ├── esp32/             ESP32-S3 Arduino 固件 + 烧录步骤（含 v5/v6 历史备份）
    ├── pc_debug/          PC 端串口调试工具（C 源码，附 build.bat）
    └── ai_debug_server.py PC 联调脚本（USB-TTL 直连 STM32，不联网也能验协议）
```

---

## 快速开始

### 1. STM32 主工程
Keil uVision 5 打开 `Project.uvprojx` → 接好 ST-Link → 编译下载。
源码为**标准外设库（SPL）**，不是 HAL。

### 2. 中转服务器
```bash
cd tools/server
pip install -r requirements.txt
set DEEPSEEK_API_KEY=sk-你的Key          # Windows；Linux 用 export
uvicorn server:app --host 0.0.0.0 --port 8000
```
需要放行 8000 端口入站，且记得查本机 IP 填进 ESP32 固件的 `SERVER_BASE`。

### 3. ESP32-S3 固件
Arduino IDE + **ESP8266Audio** 库，打开 `tools/esp32/esp32_ai_pet/esp32_ai_pet.ino`，
填好配置区后按 "ESP32S3 Dev Module" 上传。

> **接线图、逐步骤操作、故障排查表 → 见 [`tools/README.md`](tools/README.md)**

---

## ⚠️ 使用前必读

1. **WiFi 凭据是占位符。** 固件配置区里是 `YOUR_WIFI_SSID` / `YOUR_WIFI_PASSWORD`，
   烧录前请填入你自己的 WiFi 名和密码（本仓库为公开库，真实凭据已移除）。
2. **蓝牙与 WiFi 二选一。** 两者都用 USART3 / H11 座，用 WiFi 时要**拔掉蓝牙模块**。
3. **`HardWare/PetAction.c` 是 GBK 编码**，不是 UTF-8。用编辑器改它时注意别把中文注释搞乱。
4. **OLED 不显示中文。** AI 的中文回复由喇叭念出来，屏幕只显示表情，因此不需要中文字库。
5. 服务器是常驻进程，电脑重启后不会自动拉起 —— 上手先 `curl http://<电脑IP>:8000/health` 探活。

---

## 开发记录

[`docs/开发记录.md`](docs/开发记录.md) 是完整的开发档案（519 行），记录了 ESP32 工具链搭建、
麦克风/供电/LittleFS 排障、网络从手机热点切到路由器等全过程。踩过的坑基本都在里面。

---

## 致谢

- 原版桌宠硬件与代码：**Sngels_wyh**（适配 v1.2.1 电路板）—— 本仓库是其二次开发版
- [STM32F10x 标准外设库](https://www.st.com/) — STMicroelectronics
- [FastAPI](https://fastapi.tiangolo.com/) / [edge-tts](https://github.com/rany2/edge-tts) — 服务器与语音合成
- [ESP8266Audio](https://github.com/earlephilhower/ESP8266Audio) — ESP32 I2S 音频播放
- DeepSeek — 对话生成
