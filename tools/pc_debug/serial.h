/*
 * serial.h - Windows 串口封装 (Win32 API)
 *
 * 用途: STM32 桌宠 PC 联调工具的串口底层
 * 说明: 纯 C, 不依赖第三方库, 用 CreateFile/ReadFile/WriteFile 操作串口
 *
 * 作者: 桌宠项目 (2026-07-31)
 */

#ifndef SERIAL_H
#define SERIAL_H

#include <windows.h>

/* ==================== 常量 ==================== */

/* 串口最大数量 (COM1 ~ COM255) */
#define SERIAL_MAX_PORTS   256

/* 串口名最大长度, 如 "COM10" */
#define SERIAL_NAME_LEN    16

/* 读缓冲区大小 */
#define SERIAL_BUF_SIZE    512

/* ==================== 类型 ==================== */

/* 一个串口的描述信息 */
typedef struct {
    char name[SERIAL_NAME_LEN];      /* 串口名, 如 "COM3" */
    char description[128];           /* 设备描述 (可能为空) */
} SerialInfo;

/* 串口句柄 */
typedef HANDLE SerialHandle;

/* ==================== 函数 ==================== */

/*
 * 枚举系统所有可用串口
 * 参数: out  - 输出数组, 存放每个串口信息
 *        max  - 数组容量
 * 返回: 实际找到的串口数量
 */
int Serial_Enumerate(SerialInfo *out, int max);

/*
 * 打开串口
 * 参数: portName - 串口名, 如 "COM3"
 * 返回: 成功返回句柄(非INVALID_HANDLE_VALUE), 失败返回 INVALID_HANDLE_VALUE
 */
SerialHandle Serial_Open(const char *portName);

/*
 * 配置串口 (9600, 8位, 无校验, 1停止位)
 * 参数: h - 已打开的串口句柄
 * 返回: 1成功, 0失败
 */
int Serial_Config(SerialHandle h);

/*
 * 读取一行 (以 '\n' 结尾), 自动去掉 \r
 * 参数: h   - 串口句柄
 *        buf - 输出缓冲区
 *        size - 缓冲区大小
 * 返回: 读到完整一行返回1, 未读到返回0 (超时则返回-1)
 */
int Serial_ReadLine(SerialHandle h, char *buf, int size);

/*
 * 写字符串到串口
 * 参数: h   - 串口句柄
 *        str - 要写的字符串
 * 返回: 成功写入的字节数, 失败返回-1
 */
int Serial_Write(SerialHandle h, const char *str);

/*
 * 关闭串口
 */
void Serial_Close(SerialHandle h);

/*
 * 获取串口读缓冲区中已有数据字节数
 * 返回: 字节数 (0表示没有数据)
 */
int Serial_BytesAvailable(SerialHandle h);

#endif /* SERIAL_H */
