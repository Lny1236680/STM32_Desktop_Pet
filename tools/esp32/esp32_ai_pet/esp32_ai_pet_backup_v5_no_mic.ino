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

/* ==================== 配置区 (按需修改) ==================== */

// WiFi
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// 云服务器地址 (电脑上先运行 server.py):
//   查电脑 IP: Windows 下 ipconfig, 例如 192.168.1.100
//   当前: 电脑连手机热点, 电脑在热点网络的 IP = 172.20.10.7
const char* SERVER_BASE = "http://172.20.10.7:8000";
const char* CHAT_URL    = "/chat";     // POST {"text":"..."} -> {"reply":"..."}
const char* TTS_URL     = "/tts";      // POST {"text":"..."} -> mp3 字节

// 与 STM32 通信的 UART (经典 ESP32 DevKit 用 UART2; ESP32-S3 也是 UART2 可自定义引脚)
// 本工程: ESP32-S3, GPIO16=RX2, GPIO17=TX2 (与 H11 接口飞线)
#define STM32_UART_NUM  2
#define STM32_RX_PIN    16    /* ESP32 接收 STM32 数据, 接 STM32 PB10 (USART3 TX) */
#define STM32_TX_PIN    17    /* ESP32 发送给 STM32,    接 STM32 PB11 (USART3 RX) */

// I2S 输出 -> MAX98357A (按你的板子/接线修改!)
#define I2S_BCLK_PIN    5     /* MAX98357A BCLK */
#define I2S_LRC_PIN     4     /* MAX98357A LRCLK */
#define I2S_DIN_PIN     6     /* MAX98357A DIN  */
#define I2S_GAIN        0.4   /* 音量 0.0~1.0 */

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
String queryAI(const char *text);
String extractReply(const String &json);
void stopAudio(void);
bool fetchAndPlayTTS(const String &text);

/* ==================== setup ==================== */

void setup(void)
{
    Serial.begin(115200);
    Serial.println();
    Serial.println("=== ESP32-S3 AI Pet Gateway (with TTS) ===");

    /* ---- 挂载 LittleFS (用于暂存 TTS 音频) ---- */
    if (!LittleFS.begin()) {
        Serial.println("[FS] LittleFS mount FAILED, 用 工具>分区方案 选大容量分区");
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

    /* ---- 初始化 I2S 音频输出 (MAX98357A) ---- */
    g_out = new AudioOutputI2S();
    g_out->SetPinout(I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN);
    g_out->SetGain(I2S_GAIN);
    g_mp3 = new AudioGeneratorMP3();
    Serial.printf("[I2S] BCLK=%d LRC=%d DIN=%d gain=%.1f\n",
                  I2S_BCLK_PIN, I2S_LRC_PIN, I2S_DIN_PIN, I2S_GAIN);
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

    /* ---- 2. 音频播放推进 (MP3 解码循环) ---- */
    if (g_mp3 && g_mp3->isRunning()) {
        if (!g_mp3->loop()) {
            Serial.println("[Audio] playback finished");
            stopAudio();
        }
    }

    /* ---- 3. WiFi 断线自动重连 ---- */
    if (WiFi.status() != WL_CONNECTED) {
        static unsigned long lastTry = 0;
        if (millis() - lastTry > 10000) {
            lastTry = millis();
            Serial.println("WiFi lost, reconnecting...");
            WiFi.disconnect();
            WiFi.reconnect();
        }
    }
}

/* ==================== 业务逻辑 ==================== */

/**
 * 处理来自 STM32 的一行协议数据
 */
void handleLine(char *line)
{
    Serial.print("[STM32] ");
    Serial.println(line);

    if (strncmp(line, "$AI:", 4) == 0) {
        const char *query = line + 4;

        /* 1. 请求 AI 中文回复 */
        String reply = queryAI(query);
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
