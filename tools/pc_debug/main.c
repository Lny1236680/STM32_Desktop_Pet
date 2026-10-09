/*
 * main.c - STM32 桌宠 AI 调试服务器 (C 版)
 *
 * 功能: 模拟 ESP32 + 云服务器, 通过 USB-TTL 串口与 STM32 通信
 *       验证 STM32 的 WiFi 串口 (USART3) 和 OLED 显示
 *
 * 接线:  USB-TTL  RX  → STM32 PB10 (USART3 TX)
 *        USB-TTL  TX  → STM32 PB11 (USART3 RX)
 *        USB-TTL GND  → STM32 GND
 *
 * 协议:  $AI:text\r\n  — AI对话内容 (双向)
 *
 * 用法:  ai_debug_server.exe COM3        (直接指定串口)
 *        ai_debug_server.exe              (自动列出串口让你选)
 *
 * 编译:  双击 build.bat (需要 VS2022)
 *
 * 作者: 桌宠项目 (2026-07-31)
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "serial.h"

/* ==================== 常量 ==================== */

/* STM32 OLED 显示上限: 标题 1 行 + 回复 3 行, 每行 16 字符 (8X16 字体) */
#define MAX_REPLY_LEN  48

/* 串口波特率 */
#define BAUD_RATE      9600

/* ==================== 模拟 AI 回复表 ====================
 * 注意: STM32 OLED 8X16 字库只支持 ASCII, 中文会显示为乱码,
 *       所以模拟回复用英文, 与云服务器 server.py 的约束保持一致
 */
typedef struct {
    const char *keyword;    /* 匹配关键字 */
    const char *reply;      /* 回复文本 */
} MockReply;

static const MockReply s_mockReplies[] = {
    { "Hello",   "Hello! I am your desktop pet AI~" },
    { "你好",    "Hello! I am your desktop pet AI~" },
    { "你是谁",  "I am the pet AI, connected via cloud!" },
    { "天气",    "Sunny and warm today, have fun!" },
    { "时间",    "It is around 3pm Beijing time." },
    { "笑话",    "Why do programmers love night? Bugs!" },
    { "唱歌",    "La la la, I am a happy little pet~" },
};

static const char *s_defaultReply = "Got it! (This is a mock reply)";

/* ==================== 函数 ==================== */

/*
 * 根据输入文本返回模拟 AI 回复 (截断到 MAX_REPLY_LEN)
 */
static void GetReply(const char *text, char *out, int outSize)
{
    int i;
    int n = sizeof(s_mockReplies) / sizeof(s_mockReplies[0]);

    for (i = 0; i < n; i++) {
        if (strstr(text, s_mockReplies[i].keyword) != NULL) {
            strncpy(out, s_mockReplies[i].reply, outSize - 1);
            out[outSize - 1] = '\0';
            return;
        }
    }

    strncpy(out, s_defaultReply, outSize - 1);
    out[outSize - 1] = '\0';
}

/*
 * 列出可用串口, 让用户选择
 * 返回: 选择的串口名 (写入 portName)
 */
static void SelectPort(char *portName, int size)
{
    SerialInfo ports[SERIAL_MAX_PORTS];
    int count, i;
    char input[32];

    printf("=== STM32 桌宠 AI 调试服务器 ===\n\n");
    printf("可用串口:\n");

    count = Serial_Enumerate(ports, SERIAL_MAX_PORTS);
    if (count <= 0) {
        printf("  未检测到串口!\n");
        printf("  请连接 USB-TTL 模块后重试\n");
        printf("  或手动指定: ai_debug_server.exe COM3\n");
        return;
    }

    for (i = 0; i < count; i++) {
        printf("  %d. %s - %s\n", i + 1, ports[i].name, ports[i].description);
    }

    printf("\n请输入编号 (1-%d): ", count);
    if (fgets(input, sizeof(input), stdin) == NULL) {
        return;
    }

    i = atoi(input);
    if (i < 1 || i > count) {
        printf("输入无效\n");
        return;
    }

    strncpy(portName, ports[i - 1].name, size - 1);
    portName[size - 1] = '\0';
}

/*
 * 主函数
 */
int main(int argc, char *argv[])
{
    SerialHandle h;
    char portName[SERIAL_NAME_LEN] = "";
    char line[SERIAL_BUF_SIZE];
    int  n;

    /* ---- 让中文正常显示 ---- */
    SetConsoleOutputCP(65001);   /* UTF-8 输出 */

    /* ---- 选择串口: 带参数用参数, 无参数让用户选 ---- */
    if (argc > 1) {
        strncpy(portName, argv[1], sizeof(portName) - 1);
        portName[sizeof(portName) - 1] = '\0';
    } else {
        SelectPort(portName, sizeof(portName));
        if (portName[0] == '\0') {
            return 1;
        }
    }

    /* ---- 打开串口 ---- */
    printf("\n正在连接 %s @ %d bps ...\n", portName, BAUD_RATE);
    h = Serial_Open(portName);
    if (h == INVALID_HANDLE_VALUE) {
        printf("无法打开串口 %s\n", portName);
        printf("请检查: 串口是否存在? 是否被其他程序占用? USB-TTL 是否插好?\n");
        return 1;
    }
    if (!Serial_Config(h)) {
        printf("串口配置失败\n");
        Serial_Close(h);
        return 1;
    }

    printf("已连接! 等待 STM32 消息... (Ctrl+C 退出)\n");
    printf("==================================================\n");

    /* ---- 主循环 ---- */
    while (1) {
        /* 读一行 */
        n = Serial_ReadLine(h, line, sizeof(line));
        if (n < 0) {
            /* 无数据, 稍等再试 */
            Sleep(50);
            continue;
        }
        if (n == 0) {
            continue;   /* 正在凑一行 */
        }

        /* ---- 解析协议 ---- */
        if (strncmp(line, "$AI:", 4) == 0) {
            const char *query = line + 4;
            char reply[MAX_REPLY_LEN + 1];
            char response[SERIAL_BUF_SIZE];

            printf("\n[STM32] >>> %s\n", query);

            /* 生成模拟回复 */
            GetReply(query, reply, sizeof(reply));
            _snprintf(response, sizeof(response), "$AI:%s\r\n", reply);
            Serial_Write(h, response);

            printf("[AI]    <<< %s\n", reply);
            printf("--------------------------------------------------\n");
        }
        else if (strncmp(line, "$OK", 3) == 0) {
            printf("[STM32] 确认: %s\n", line);
        }
        else if (strncmp(line, "$ERR", 4) == 0) {
            printf("[STM32] 错误: %s\n", line);
        }
        else {
            printf("[STM32] 未知: %s\n", line);
        }
    }

    /* 正常不会走到这里, Ctrl+C 退出 */
    Serial_Close(h);
    return 0;
}
