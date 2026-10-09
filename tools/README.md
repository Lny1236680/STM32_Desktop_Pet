# 桌宠 WiFi AI 模块 —— 使用说明

完整的 AI 对话链路 (2026-08-02 更新: 中文回复 + TTS 语音播放):

```
你说话 ─SU-03T(0x51)→ STM32 桌宠 ─$AI:问─► ESP32-S3 网关 ─HTTP─► 服务器(FastAPI) ─► DeepSeek
              USART3(PB10/PB11 @9600)  (Arduino固件+ESP8266Audio)     /chat   /tts
OLED 随机表情 ◄─ 收到$AI:中文回复 ─ STM32 ◄─$AI:中文─ ESP32-S3 ◄── 中文回复
AI 语音播放 ◄── 喇叭 ─ MAX98357A功放 ─I2S─ ESP32-S3 ◄── /tts 中文mp3 (edge-tts)
```

> 2026-07-31: WiFi 从软串口(PB6/PB7+TIM4) 迁移到 **USART3 硬件串口(PB10/PB11)**。
> 2026-08-02: ESP32 升级为 **ESP32-S3**，新增 **TTS 中文语音播放**；OLED 不再显示文字，改显示**随机表情**。
> 使用前需**拔掉蓝牙模块**(H11 二选一)。

## 各阶段联调方式

| 阶段 | 工具 | 说明 |
|------|------|------|
| 1. STM32 软串口自测 | `tools/ai_debug_server.py` | PC 用 USB-TTL 直接接 STM32 PB6/PB7，模拟 ESP32 回复（不联网） |
| 2. ESP32 + 服务器 | `tools/esp32/esp32_ai_pet/` + `tools/server/server.py` | 真实联网链路 |

## 一、STM32 侧（已就绪）

- 代码：`HardWare/WiFi.c`（USART3 硬件串口 PB10/PB11 @9600）
- 触发：语音 / 蓝牙命令 `0x51` → 发送 `$AI:Hello` → 进入 AI 对话模式（Face_Mode=7）
- 回复显示：收到 `$AI:中文回复` 后 OLED 显示**随机表情**（Face_Config.c, AI_FacePick 伪随机选择）
- 超时：20 秒无回复自动退出（DeepSeek+TTS 回复较慢已放宽）
- 已通过 Keil 编译（0 Error / 0 Warning），`Objects/Project.hex` 烧录即可

## 二、PC 联调（USB-TTL 直接连 STM32，先验证软串口）

**第一步：烧录固件**
- Keil 打开 `Project.uvprojx`，接好 ST-Link/烧录器，点 "Download"（或按 F8）
- 烧录目标：`Objects/Project.hex`

**第二步：接线**（USB-TTL 模块，必须是 3.3V 电平！5V 会烧 MCU）

> 2026-07-31 更新: WiFi 已迁移到 USART3(PB10/PB11)。联调时接这两个脚。

```
USB-TTL  RX  → STM32 PB10 (USART3 TX)   ← 交叉!
USB-TTL  TX  → STM32 PB11 (USART3 RX)   ← 交叉!
USB-TTL GND  → STM32 GND
```

PB10/PB11 位置: 底板 **H11** 接口(蓝牙/WIFI模块座) — 拔掉蓝牙模块后, USB-TTL 的 RX/TX/GND 接 H11 的 PB10/PB11/GND。

**第三步：运行调试脚本**

```bash
# Windows 下, 需要安装 pyserial:
pip install pyserial
python tools/ai_debug_server.py COM3
```

对着桌宠语音说"AI对话"（或蓝牙发 0x51），OLED 进入 AI 模式显示 Listening，
脚本收到 `$AI:Hello` 后会自动回复（英文即可），OLED 显示**随机表情**（不再显示文字）。

**预期结果**
- 触发后 OLED 显示 `AI Mode` + `Listening...`
- 脚本打印 `[STM32] >>> Hello`，自动回复后 OLED 显示桌宠随机表情（快乐/狂热/打招呼…）
- 20 秒无回复，OLED 自动退出 AI 模式

**故障排查**
| 现象 | 原因 | 处理 |
|------|------|------|
| OLED 一直 Listening 不回复 | 接线交叉错 / USB-TTL 是 5V | 检查 RX↔TX 交叉、共地、电平 |
| 脚本收不到数据 | PB10 没接对 / 串口号错 | 确认 COM 号、试 `--list` 查看 |
| 显示乱码 | USB-TTL 电平不匹配 | 用 3.3V 供电的模块 |
| 没进 AI 模式 | 语音命令没触发 | 蓝牙发 0x51 或手动触发 |

> 注：PC 联调只是验证 STM32→串口→回传 的协议通断。OLED 现在显示随机表情（不显示文字），
> 中文文字回复只在 ESP32 播语音时使用，PC 端可随意回一行 `$AI:xxx` 验证。真实链路见 [四、ESP32-S3 固件](#四esp32-s3-固件)。

## 三、云服务器（DeepSeek）

```bash
cd tools/server
pip install -r requirements.txt

# Windows:
set DEEPSEEK_API_KEY=sk-你的Key
uvicorn server:app --host 0.0.0.0 --port 8000

# 本地自测:
curl -X POST http://127.0.0.1:8000/chat -H "Content-Type: application/json" -d "{\"text\":\"Hello\"}"
```

注意：防火墙需放行 8000 端口（入站规则），ESP32 才能访问。查电脑 IP 用 `ipconfig`。

## 四、ESP32-S3 固件

> 2026-08-02 更新: 固件升级为 ESP32-S3 版本, 新增 **TTS 中文语音播放** (I2S → MAX98357A 功放 → 喇叭)。
> AI 中文回复: ①传回 STM32 (OLED 显示随机表情) ②由 ESP32 播放语音朗读出来。

1. Arduino IDE 安装 esp32 开发板包（附加开发板管理器网址：
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`）
2. 库管理器安装 **ESP8266Audio** (by earlephilhower)
3. 打开 `tools/esp32/esp32_ai_pet/esp32_ai_pet.ino`
4. 修改固件头部配置区：WiFi 账号密码、`SERVER_BASE`（填运行 server.py 的电脑 IP）
5. 开发板选 "ESP32S3 Dev Module"（或你的 S3 板型号），上传，串口监视器 115200 看日志

### ESP32-S3 ↔ STM32 接线（H11 飞线，不改 PCB）

```
H11 接口 (蓝牙/WIFI座)          ESP32-S3 (面包板)
┌─────────────────┐            ┌──────────────┐
│ ② PB10 (TX) ────┼─飞线─────→│ GPIO16 (RX2) │
│ ③ PB11 (RX) ────┼─飞线─────→│ GPIO17 (TX2) │
│ ④ GND    ───────┼─飞线─────→│ GND          │
│ ① 3.3V   ───────┼─飞线─────→│ 3V3 (可选)   │
└─────────────────┘            └──────────────┘
     拔蓝牙模块                         USB 独立供电
```

> H11 接口顺序: `1=3.3V, 2=PB10, 3=PB11, 4=GND`。ESP32 建议用 USB 独立供电（H11 只接 RX/TX/GND）。

### ESP32-S3 ↔ MAX98357A 功放 ↔ 喇叭（AI 语音播放）

```
ESP32-S3          MAX98357A          喇叭(8Ω)
GPIO5  ──BCLK──→  BCLK
GPIO4  ──LRC───→  LRCLK
GPIO6  ──DIN───→  DIN
GND    ──GND──→  GND
5V(USB)────VIN  (VIN 接 5V)
                  SPK+ ──→ 喇叭 +
                  SPK- ──→ 喇叭 -
```

> 喇叭从 SU-03T 的喇叭插座拔下来，改插到 MAX98357A 的 SPK 输出。I2S 引脚在固件顶部
> `I2S_BCLK_PIN / I2S_LRC_PIN / I2S_DIN_PIN` 可改，按你的接线调整。

## 五、TTS 中文语音（edge-tts，免费）

服务器 `/tts` 接口用微软免费的 edge-tts 生成中文语音，ESP32 拉取后播放：

```bash
cd tools/server
pip install -r requirements.txt
uvicorn server:app --host 0.0.0.0 --port 8000
```

自测（应该返回一个 mp3 文件）:
```
curl -X POST http://127.0.0.1:8000/tts -H "Content-Type: application/json" -d "{\"text\":\"你好呀主人\"}" -o reply.mp3
```

可选配置: 环境变量 `TTS_VOICE`（默认 zh-CN-XiaoxiaoNeural 女声）、`TTS_RATE`（语速）。

## 已知限制 / 后续计划

- **OLED 不做中文显示**：AI 回复时 OLED 显示桌宠**随机表情**（快乐/狂热/打招呼等），
  中文回复由 ESP32 播放语音读出，OLED 无需中文字库。
- **语音输入**：当前由 SU-03T 固定命令词触发 AI（如 0x51），把"你说的话内容"传给 AI 需要
  麦克风 + STT（ESP32 接 I2S 麦克风如 INMP441 或第三方 STT 服务）—— 后续可做。
- **多轮对话**：服务器已预留 `history` 参数，ESP32 固件可扩展携带上下文实现连续对话。
- **AI 回复长度**：服务器限制中文回复 ≤30 字，适配短句 TTS 播放。
