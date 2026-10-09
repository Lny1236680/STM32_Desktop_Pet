/**
 * 桌宠 AI 网关 —— ESP32-S3 固件 (Arduino)
 * =========================================
 * 职责: 桥接 STM32 桌宠 与 云服务器, 并播放 AI 中文回复语音
 *
 *   STM32 USART3(PB10/PB11 @9600) <--UART--> ESP32-S3 <--WiFi/HTTP--> 云服务器 <--> DeepSeek
 *                                                          /tts(edge-tts)
 *   ESP32-S3 --I2S--> MAX98357A 功放 --> 喇叭 (播放 AI 回复中文语音)
 *
 * 接线 (两板都是 3.3V 电平, 必须共地!):
 *   ESP32-S3 GPIO16 (UART2 RX)  <--  STM32 PB10 (USART3 TX)   [H11 第2脚]
 *   ESP32-S3 GPIO17 (UART2 TX)  -->  STM32 PB11 (USART3 RX)   [H11 第3脚]
 *   ESP32-S3 GND                 <->  STM32 GND                [H11 第4脚]
 *   (H11 第1脚 3.3V 可不接, ESP32 用 USB 独立供电)
 *
 *   喇叭: 从 SU-03T 的喇叭插座拔下, 接到 MAX98357A 的 SPK 输出
 *   MAX98357A:  VIN=5V, GND=GND, BCLK/LRC/DIN 接 ESP32 的 I2S 引脚(见下方配置)
 *
 * 协议 (行文本, 与 STM32 侧 WiFi.c 保持一致):
 *   STM32 -> ESP32:  $AI:用户问题\r\n
 *   ESP32 -> STM32:  $AI:AI回复文本\r\n   (STM32 收到后 OLED 显示随机表情)
 *
 * 依赖库 (Arduino 库管理器安装):
 *   - ESP8266Audio   (by earlephilhower)  用于 MP3 解码播放
 *
 * 使用步骤:
 *   1. Arduino IDE 安装 esp32 开发板包:
 *        文件 > 首选项 > 附加开发板管理器网址 填入
 *        https://espressif.github.io/arduino-esp32/package_esp32_index.json
 *        然后在 "开发板管理器" 搜索 esp32 安装 (选 ESP32-S3 支持)
 *   2. 库管理器安装 ESP8266Audio
 *   3. 修改下方 WIFI_SSID / WIFI_PASSWORD / SERVER_BASE
 *   4. 开发板选择 "ESP32S3 Dev Module", 点上传
 *   5. 串口监视器设 115200 查看运行日志
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <LittleFS.h>

/* ---- ESP8266Audio (MP3 播放) ---- */
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

/* ---- I2S 麦克风录音 (INMP441) 用新版 ESP-IDF 驱动 ---- */
#include "driver/i2s_std.h"

/* ==================== 配置区 (按需修改) ==================== */

// WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 云服务器地址 (电脑上先运行 server.py):
//   查电脑 IP: Windows 下 ipconfig, 例如 192.168.1.100
//   当前: 电脑走有线接路由器, 电脑 IP = 192.168.5.19 (ESP32 与电脑同一网段)
const char* SERVER_BASE = "http://192.168.5.19:8000";
const char* CHAT_URL    = "/chat";     // POST {"text":"..."} -> {"reply":"..."}
const char* TTS_URL     = "/tts";      // POST {"text":"..."} -> mp3 字节

// 与 STM32 通信的 UART (经典 ESP32 DevKit 用 UART2; ESP32-S3 也是 UART2 可自定义引脚)
// 本工程: ESP32-S3, GPIO16=RX2, GPIO17=TX2 (与 H11 接口飞线)
#define STM32_UART_NUM  2
#define STM32_RX_PIN    16    /* ESP32 接收 STM32 数据, 接 STM32 PB10 (USART3 TX) */
#define STM32_TX_PIN    17    /* ESP32 发送给 STM32,    接 STM32 PB11 (USART3 RX) */

// I2S 输出 -> MAX98357A (按你的板子/接线修改!)
#define I2S_BCLK_PIN    10    /* MAX98357A BCLK (2026-08-12 改: 原GPIO5, 板上固件=10) */
#define I2S_LRC_PIN     9     /* MAX98357A LRCLK (原GPIO4) */
#define I2S_DIN_PIN     11    /* MAX98357A DIN  (原GPIO6) */
#define I2S_GAIN        0.4   /* 音量 0.0~1.0 */

/* ==================== 录音配置 (INMP441 I2S 麦克风) ====================
 * 设计: 录音独立占用 I2S0, 播放由 ESP8266Audio 自动拿空闲的 I2S1
 *       (ESP32-S3 有 I2S0/I2S1 两个端口, 完全独立, 无需切换复用)
 * 接线:
 *   ESP32 GPIO15 (I2S0 BCLK) -> INMP441 BCLK
 *   ESP32 GPIO13 (I2S0 WS)   -> INMP441 WS/LRC   (勿用16! 16已给UART2接STM32; 板上无14丝印用13)
 *   ESP32 GPIO7  (I2S0 DIN)  <- INMP441 DOUT
 *   ESP32 3V3 -> INMP441 VDD,  GND -> GND
 *   (INMP441 L/R 接 GND=左声道)
 */
#define MIC_BCLK_PIN    15
#define MIC_WS_PIN      13    /* 2026-08-12 改: 板上无GPIO14丝印, 用13 */
#define MIC_DOUT_PIN    7
#define MIC_SAMPLE_RATE 16000
#define MIC_DMA_BUFS    8
#define MIC_DMA_FRAMES  240
#define REC_MAX_MS      4000   /* 最长录音 4 秒 (够短句STT; 降堆峰值~128KB, 2026-09-03改) */
#define REC_SILENCE_MS  900    /* 说话后静音 900ms 视为说完 */
#define REC_VOICE_THRESH 400   /* 音量阈值, 需按实际麦克风环境调 */
#define STT_URL         "/stt" /* 语音识别接口 */

// 麦克风数据格式 (INMP441 与 ICS-43434 一致):
//   32bit槽位, 24bit左对齐, 数据在高16位 (MSB), 低位是符号扩展/垃圾
//   -> 统一取高 16 位, 无需按型号区分
// 声道: INMP441 靠 L/R 脚选, ICS-43434 单声道; 都按 LEFT 采

// 独立对话触发按键: BOOT 键 = GPIO0 (按下为低电平), 用于 ESP32 脱离 STM32 独立测试
#define BOOT_BTN_PIN    0
#define BTN_DEBOUNCE_MS 200    /* 按键消抖 */

// AI 请求超时 (毫秒)
#define AI_TIMEOUT_MS   15000
// 单行协议最大长度
#define LINE_BUF_MAX    256

/* ==================== 全局变量 ==================== */

HardwareSerial Stm32Uart(STM32_UART_NUM);   /* 与 STM32 通信的串口 */
char lineBuf[LINE_BUF_MAX];                 /* 行缓冲 */
int  lineLen = 0;

/* 音频对象 */
AudioFileSourceLittleFS *g_file = NULL;
AudioGeneratorMP3       *g_mp3  = NULL;
AudioOutputI2S          *g_out  = NULL;

/* ==================== 函数声明 ==================== */

void handleLine(char *line);
void runStandaloneAI(void);      /* ESP32 独立对话: 按BOOT键→录音→STT→AI→TTS→播放 */
String listenAndSTT(void);       /* 录音 + STT, 返回识别文本(空串=没听清) */
String queryAI(const char *text);
String extractReply(const String &json);
void stopAudio(void);
bool fetchAndPlayTTS(const String &text);

/* ---- 录音/识别 ---- */
bool initMic(void);
void micEnable(void);    /* 开麦克风 I2S0 时钟 (录音前) */
void micDisable(void);   /* 关麦克风 I2S0 时钟 (录完/平时省电) */
bool recordToWav(uint8_t **out, size_t *outLen);
bool sttWav(uint8_t *wav, size_t len, String &out);

/* ==================== setup ==================== */

void setup(void)
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("=== ESP32-S3 AI Pet Gateway (with TTS) ===");

    /* ---- 挂载 LittleFS (用于暂存 TTS 音频) ----
     * huge_app 分区方案里 1MB SPIFFS 分区首次是空白(全0xFF), begin() 会失败
     * (报 "Corrupted dir pair, mount failed")。此时必须先 format() 再重试。
     */
    if (!LittleFS.begin()) {
        Serial.println("[FS] mount failed, formatting...");
        if (LittleFS.format()) {
            Serial.println("[FS] formatted OK, retrying mount...");
            if (!LittleFS.begin()) {
                Serial.println("[FS] mount STILL FAILED, 用 工具>分区方案 选大容量分区");
            } else {
                Serial.println("[FS] LittleFS OK (after format)");
            }
        } else {
            Serial.println("[FS] format FAILED, 用 工具>分区方案 选大容量分区");
        }
    } else {
        Serial.println("[FS] LittleFS OK");
    }

    /* ---- 连接 WiFi ---- */
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting WiFi [%s] ...", WIFI_SSID);
    int retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 40) {
        delay(500);
        Serial.print(".");
        retry++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println();
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println();
        Serial.println("WiFi connect FAILED! 请检查 SSID/密码, 运行中会自动重连.");
    }

    /* ---- 初始化与 STM32 的 UART (9600, 8N1) ---- */
    Stm32Uart.begin(9600, SERIAL_8N1, STM32_RX_PIN, STM32_TX_PIN);
    Serial.printf("STM32 UART%d @9600: RX=GPIO%d, TX=GPIO%d\n",
                  STM32_UART_NUM, STM32_RX_PIN, STM32_TX_PIN);

    /* ---- BOOT 键(GPIO0) 触发独立对话: 内部上拉, 按下=低电平 ---- */
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    Serial.println("[BTN] BOOT key (GPIO0) = standalone AI trigger");

    /* ---- 初始化 I2S 音频输出 (MAX98357A) ---- */
    g_out = new AudioOutputI2S();
    g_out->SetPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN);
    g_out->SetGain(I2S_GAIN);
    g_mp3 = new AudioGeneratorMP3();
    Serial.printf("[I2S] BCLK=%d LRC=%d DIN=%d gain=%.1f\n",
                  I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN, I2S_GAIN);

    /* ---- 初始化麦克风 (占住 I2S0 通道让播放走 I2S1; 时钟默认关, 录音时才开) ---- */
    initMic();
}

/* ==================== loop ==================== */

void loop(void)
{
    /* ---- 1. 接收 STM32 数据, 按行处理 ---- */
    while (Stm32Uart.available() > 0) {
        char ch = (char)Stm32Uart.read();
        if (ch == '\n') {
            lineBuf[lineLen] = '\0';
            handleLine(lineBuf);
            lineLen = 0;
        } else if (ch != '\r' && lineLen < LINE_BUF_MAX - 1) {
            lineBuf[lineLen++] = ch;
        }
    }

    /* ---- 1.5 独立对话: BOOT 键(GPIO0)触发, 按下松开后开始 ---- */
    if (digitalRead(BOOT_BTN_PIN) == LOW) {
        delay(BTN_DEBOUNCE_MS);               /* 消抖 */
        if (digitalRead(BOOT_BTN_PIN) == LOW) {
            /* 等待松开, 避免长按期间反复触发 */
            while (digitalRead(BOOT_BTN_PIN) == LOW) {
                delay(20);
            }
            Serial.println("[BTN] BOOT key pressed, standalone AI start");
            runStandaloneAI();
        }
    }

    /* ---- 2. 音频播放推进 (MP3 解码循环) ---- */
    if (g_mp3 && g_mp3->isRunning()) {
        if (!g_mp3->loop()) {
            Serial.println("[Audio] playback finished");
            stopAudio();
        }
    }

    /* ---- 3. WiFi 断线自动重连 (非阻塞, 不卡音频播放) ---- */
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastTry = 0;
        if (millis() - lastTry > 10000) {
            lastTry = millis();
            Serial.println("WiFi lost, reconnecting...");
            /* 直接 begin() 重连; 不要先 disconnect(), 那会触发认证失败回调且有时不生效 */
            WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
    }
}

/* ==================== 业务逻辑 ==================== */

/* ==================== 录音 / 语音识别 (STT) ==================== */

i2s_chan_handle_t mic_chan = NULL;

/**
 * 初始化 INMP441 麦克风 (I2S0 RX, 16k, 32bit 槽位)
 * 幂等: 已初始化则直接返回
 * ⚠️ 只创建/初始化通道, 不 enable —— 时钟默认关闭省电,
 *    由 micEnable()/micDisable() 在录音前后开关 (供电卡点 §43/§49 缓解)
 */
bool initMic(void)
{
    if (mic_chan) return true;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num  = MIC_DMA_BUFS;
    chan_cfg.dma_frame_num = MIC_DMA_FRAMES;
    if (i2s_new_channel(&chan_cfg, NULL, &mic_chan) != ESP_OK) {
        Serial.println("[MIC] new_channel failed");
        return false;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = (gpio_num_t)MIC_BCLK_PIN,
            .ws   = (gpio_num_t)MIC_WS_PIN,
            .dout = I2S_GPIO_UNUSED,
            .din  = (gpio_num_t)MIC_DOUT_PIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    /* 声道掩码: INMP441 数据在 LEFT (L/R接地出LEFT); ICS-43434 标准LEFT */
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    if (i2s_channel_init_std_mode(mic_chan, &std_cfg) != ESP_OK) {
        Serial.println("[MIC] init_std_mode failed");
        return false;
    }
    Serial.println("[MIC] I2S mic channel created (I2S0 RX, clock OFF until record)");
    return true;
}

/* 开/关麦克风 I2S0 时钟 —— 只在录音时开, 平时关 (省电 + 减少共用电源耦合) */
void micEnable(void)
{
    if (mic_chan && i2s_channel_enable(mic_chan) == ESP_OK) {
        Serial.println("[MIC] clock ON (recording)");
    }
}
void micDisable(void)
{
    if (mic_chan && i2s_channel_disable(mic_chan) == ESP_OK) {
        Serial.println("[MIC] clock OFF (power save)");
    }
}

/**
 * 录音: 能量检测(VAD)自动停, 最长 REC_MAX_MS
 * 输出 16k 单声道 16bit WAV 字节缓冲 (malloc, 调用方 free)
 */
bool recordToWav(uint8_t **out, size_t *outLen)
{
    if (!initMic()) return false;
    micEnable();                    /* 录音才开时钟 (平时关闭, 供电卡点缓解) */

    size_t maxData = (size_t)(REC_MAX_MS / 1000) * MIC_SAMPLE_RATE * 2;
    uint8_t *wav = (uint8_t *)malloc(44 + maxData);
    if (!wav) {
        Serial.println("[REC] malloc failed");
        micDisable();
        return false;
    }

    /* 占位 WAV 头, 录完回填大小字段 */
    memset(wav, 0, 44);
    memcpy(wav + 0,  "RIFF", 4);
    memcpy(wav + 8,  "WAVE", 4);
    memcpy(wav + 12, "fmt ", 4);
    *(uint32_t *)(wav + 16) = 16;                  /* fmt chunk size */
    *(uint16_t *)(wav + 20) = 1;                   /* PCM */
    *(uint16_t *)(wav + 22) = 1;                   /* mono */
    *(uint32_t *)(wav + 24) = MIC_SAMPLE_RATE;
    *(uint32_t *)(wav + 28) = MIC_SAMPLE_RATE * 2; /* byte rate */
    *(uint16_t *)(wav + 32) = 2;                   /* block align */
    *(uint16_t *)(wav + 34) = 16;                  /* bits per sample */
    memcpy(wav + 36, "data", 4);

    int32_t chunk[256];           /* 一次读 1KB (256 * 32bit) */
    size_t dataBytes = 0;
    unsigned long t0 = millis();
    unsigned long lastVoice = 0;
    bool hasVoice = false;

    while (millis() - t0 < REC_MAX_MS) {
        size_t got = 0;
        esp_err_t err = i2s_channel_read(mic_chan, chunk, sizeof(chunk), &got, pdMS_TO_TICKS(1000));
        if (err != ESP_OK || got == 0) continue;
        int n = got / 4;      /* 每样本 4 字节 (32bit 槽) */

        /* 越界保护: 只写能放进剩余空间的样本数, 防止 dataBytes 越过 maxData */
        size_t room = (maxData - dataBytes) / 2;   /* 剩余可写样本数 */
        if (n > (int)room) n = (int)room;
        if (n <= 0) break;

        int32_t peak = 0;
        for (int i = 0; i < n; i++) {
            /* INMP441 与 ICS-43434 都是 24bit 左对齐在高位 -> 统一取高 16 位 */
            int32_t v = chunk[i] >> 16;
            if (v < 0) v = -v;
            if (v > peak) peak = v;
            int16_t s = (int16_t)(chunk[i] >> 16);
            memcpy(wav + 44 + dataBytes, &s, 2);
            dataBytes += 2;
        }
        if (peak > REC_VOICE_THRESH) {
            hasVoice = true;
            lastVoice = millis();
        }
        /* 说过话, 且静音足够久 -> 说完 */
        if (hasVoice && (millis() - lastVoice > REC_SILENCE_MS)) break;
    }

    *(uint32_t *)(wav + 4)  = 36 + (uint32_t)dataBytes;
    *(uint32_t *)(wav + 40) = (uint32_t)dataBytes;

    micDisable();                /* 录完立刻关时钟 */

    *out = wav;
    *outLen = 44 + dataBytes;
    Serial.printf("[REC] done: %d ms, %u bytes, voice=%s\n",
                  (int)(millis() - t0), (unsigned)(*outLen), hasVoice ? "Y" : "N");
    return hasVoice && dataBytes > 2000;
}

/**
 * 上传 WAV 到 /stt, 得到简体中文文本
 */
bool sttWav(uint8_t *wav, size_t len, String &out)
{
    HTTPClient http;
    http.begin(String(SERVER_BASE) + STT_URL);
    http.setTimeout(30000);
    http.addHeader("Content-Type", "audio/wav");

    int code = http.POST(wav, len);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[STT] http error code: %d\n", code);
        http.end();
        return false;
    }
    String resp = http.getString();
    http.end();

    /* 解析 {"text":"..."} */
    int start = resp.indexOf("\"text\":\"");
    if (start < 0) return false;
    start += 8;
    String text = "";
    for (unsigned i = start; i < resp.length(); i++) {
        char c = resp[i];
        if (c == '\\' && i + 1 < resp.length()) { text += resp[i + 1]; i++; }
        else if (c == '"') break;
        else text += c;
    }
    out = text;
    return out.length() > 0;
}

/**
 * 处理来自 STM32 的一行协议数据
 */
void handleLine(char *line)
{
    Serial.print("[STM32] ");
    Serial.println(line);

    if (strncmp(line, "$AI:", 4) == 0) {
        const char *query = line + 4;

        String text;
        bool fromStm32 = true;

        if (strlen(query) > 0) {
            text = query;                        /* 已有文字问题, 直接用 */
        } else {
            /* 空问题 = 开始听麦克风 -> STT */
            text = listenAndSTT();               /* 返回空串表示没听清 */
            if (text.length() == 0) {
                /* 没听清: 语音提示 + 回 STM32 让 OLED 有反馈 */
                String fallback = "没听清, 再说一次吧";
                Serial.println(fallback);
                Stm32Uart.print("$AI:");
                Stm32Uart.print(fallback);
                Stm32Uart.print("\r\n");
                fetchAndPlayTTS(fallback);
                return;
            }
        }

        /* 1. 请求 AI 中文回复 */
        String reply = queryAI(text.c_str());
        Serial.print("[AI] reply: ");
        Serial.println(reply);

        /* 2. 把中文回复发给 STM32 (STM32 收到后 OLED 显示随机表情) */
        Stm32Uart.print("$AI:");
        Stm32Uart.print(reply);
        Stm32Uart.print("\r\n");
        Serial.println("[STM32] << $AI:" + reply);

        /* 3. 拉取 TTS 语音并播放 */
        if (reply.length() > 0) {
            fetchAndPlayTTS(reply);
        }
    } else {
        Serial.println("未知命令, 忽略.");
    }
}

/**
 * 录音并做 STT 识别, 返回识别文本 (失败返回空串)
 */
// 诊断开关: 1 = 录音后即使 voice=N 也强制上传 WAV 到服务器存盘 (定位录音质量问题)
// 正式使用应设为 0, 避免每次录音都上传诊断音频
#define DIAG_DUMP_REC 1

String listenAndSTT(void)
{
    /* 若正在播 TTS, 先停掉释放堆 (mp3解码/文件源缓冲), 否则录音 malloc 192KB 易失败 (2026-09-03修) */
    stopAudio();
    Serial.println("[MIC] start recording...");
    uint8_t *wav = NULL;
    size_t wavLen = 0;
    String text;

    bool gotRec = recordToWav(&wav, &wavLen);
#if DIAG_DUMP_REC
    /* 诊断: 即使 voice=N 也把音频发给 /stt, 服务器会存盘 stt_fail_*.wav 供分析 */
    if (wav && wavLen > 0) {
        Serial.printf("[DIAG] dump %u bytes to /stt...\n", (unsigned)wavLen);
        if (sttWav(wav, wavLen, text)) {
            Serial.print("[STT] recognized: ");
            Serial.println(text);
        } else {
            Serial.println("[STT] recognize failed (audio saved server-side)");
            text = "";
        }
    } else {
        Serial.println("[STT] no rec data");
        text = "";
    }
#else
    if (gotRec) {
        if (sttWav(wav, wavLen, text)) {
            Serial.print("[STT] recognized: ");
            Serial.println(text);
        } else {
            Serial.println("[STT] recognize failed");
            text = "";
        }
    } else {
        Serial.println("[STT] no speech");
        text = "";
    }
#endif
    if (wav) free(wav);
    return text;
}

/**
 * ESP32 独立 AI 对话: 按 BOOT 键触发, 录音 -> STT -> AI -> TTS 播放
 * 不依赖 STM32, 不回 STM32 表情 (独立模块使用)
 */
void runStandaloneAI(void)
{
    String text = listenAndSTT();
    if (text.length() == 0) {
        /* 没听清: 只语音提示 */
        String fallback = "没听清, 再说一次吧";
        Serial.println(fallback);
        fetchAndPlayTTS(fallback);
        return;
    }

    String reply = queryAI(text.c_str());
    Serial.print("[AI] reply: ");
    Serial.println(reply);

    if (reply.length() > 0) {
        fetchAndPlayTTS(reply);
    }
}

/**
 * 请求云服务器, 返回 AI 回复文本
 * 阻塞调用, 最多等待 AI_TIMEOUT_MS
 */
String queryAI(const char *text)
{
    HTTPClient http;

    /* 构造 JSON 请求体: {"text":"..."}
       注意: 控制字符(\n \r \t)在 JSON 字符串中必须转义, 否则服务器解析会 400 */
    String esc = String(text);
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    esc.replace('\n', ' ');
    esc.replace('\r', ' ');
    esc.replace('\t', ' ');

    String body = "{\"text\":\"";
    body += esc;
    body += "\"}";

    http.begin(String(SERVER_BASE) + CHAT_URL);
    http.setTimeout(AI_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[HTTP] chat error code: %d\n", code);
        http.end();
        return String("服务器出错了, 稍后再试");
    }

    String resp = http.getString();
    http.end();

    /* 解析 {"reply":"..."} —— 用轻量字符串解析, 无需 ArduinoJson 库 */
    String reply = extractReply(resp);
    if (reply.length() == 0) {
        return String("服务器没回应呢");
    }
    return reply;
}

/**
 * 从 {"reply":"..."} 中提取 reply 值, 并反转义 \n \" \\ 等
 */
String extractReply(const String &json)
{
    const char *key = "\"reply\":\"";
    int start = json.indexOf(key);
    if (start < 0) return String("");

    start += strlen(key);
    String out = "";
    bool inEscape = false;

    for (int i = start; i < json.length(); i++) {
        char c = json[i];
        if (inEscape) {
            switch (c) {
                case 'n':  out += '\n';  break;
                case 'r':  out += '\r';  break;
                case 't':  out += '\t';  break;
                case '"':  out += '"';   break;
                case '\\': out += '\\';  break;
                default:   out += c;     break;
            }
            inEscape = false;
        } else if (c == '\\') {
            inEscape = true;
        } else if (c == '"') {
            break;              /* 字符串结束 */
        } else {
            out += c;
        }
    }
    return out;
}

/* ==================== TTS 语音播放 ==================== */

/**
 * 停止当前播放并释放音频文件源
 */
void stopAudio(void)
{
    if (g_mp3 && g_mp3->isRunning()) {
        g_mp3->stop();
    }
    if (g_file) {
        delete g_file;
        g_file = NULL;
    }
}

/**
 * 请求 /tts 生成 mp3, 存入 LittleFS 后播放
 */
bool fetchAndPlayTTS(const String &text)
{
    HTTPClient http;

    /* 构造 JSON 请求体: {"text":"..."} */
    String esc = String(text);
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    esc.replace('\n', ' ');
    esc.replace('\r', ' ');
    esc.replace('\t', ' ');

    String body = "{\"text\":\"";
    body += esc;
    body += "\"}";

    Serial.print("[TTS] requesting: ");
    Serial.println(text);

    http.begin(String(SERVER_BASE) + TTS_URL);
    http.setTimeout(AI_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int code = http.POST(body);
    if (code != HTTP_CODE_OK) {
        Serial.printf("[HTTP] tts error code: %d\n", code);
        http.end();
        return false;
    }

    /* 保存到 LittleFS */
    fs::File f = LittleFS.open("/reply.mp3", "w");
    if (!f) {
        Serial.println("[FS] cannot open /reply.mp3 for write");
        http.end();
        return false;
    }
    http.writeToStream(&f);
    f.close();
    http.end();
    Serial.println("[FS] saved /reply.mp3");

    /* 停止上一次播放, 再从头播放 */
    stopAudio();

    g_file = new AudioFileSourceLittleFS("/reply.mp3");
    if (!g_mp3->begin(g_file, g_out)) {
        Serial.println("[Audio] begin FAILED");
        stopAudio();
        return false;
    }
    Serial.println("[Audio] playing...");
    return true;
}
