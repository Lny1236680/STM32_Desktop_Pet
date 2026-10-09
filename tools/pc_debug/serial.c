/*
 * serial.c - Windows 串口封装 (Win32 API)
 *
 * 说明:
 *   - 枚举串口: 用注册表方式, 简单可靠, 不用安装额外驱动
 *   - 打开串口: CreateFile
 *   - 配置:     DCB (9600 8N1) + 超时
 *   - 读:       非阻塞轮询, 每次最多读一字节, 凑成一行
 *   - 写:       WriteFile 写整个字符串
 *
 * 作者: 桌宠项目 (2026-07-31)
 */

#include "serial.h"
#include <stdio.h>
#include <string.h>
#include <windows.h>

/* ==================== 内部函数 ==================== */

/*
 * 检查 COMx 是否存在 (尝试打开再关闭)
 * 返回: 1存在, 0不存在
 */
static int PortExists(int index)
{
    char portName[SERIAL_NAME_LEN];
    HANDLE h;

    _snprintf(portName, sizeof(portName), "COM%d", index);
    h = CreateFileA(portName,
                    GENERIC_READ | GENERIC_WRITE,
                    0,                /* 独占访问 */
                    NULL,
                    OPEN_EXISTING,
                    0,
                    NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    CloseHandle(h);
    return 1;
}

/* ==================== 公共函数 ==================== */

int Serial_Enumerate(SerialInfo *out, int max)
{
    int count = 0;
    int i;

    if (out == NULL || max <= 0) {
        return 0;
    }

    for (i = 1; i < SERIAL_MAX_PORTS && count < max; i++) {
        if (PortExists(i)) {
            _snprintf(out[count].name, sizeof(out[count].name), "COM%d", i);
            _snprintf(out[count].description, sizeof(out[count].description),
                      "串口 COM%d", i);
            count++;
        }
    }
    return count;
}

SerialHandle Serial_Open(const char *portName)
{
    char fullName[32];

    /* 串口名在 Win32 里要写成 "\\\\.\\COM3" (COM10 以上必须加前缀) */
    _snprintf(fullName, sizeof(fullName), "\\\\.\\%s", portName);

    return CreateFileA(fullName,
                       GENERIC_READ | GENERIC_WRITE,
                       0,
                       NULL,
                       OPEN_EXISTING,
                       0,
                       NULL);
}

int Serial_Config(SerialHandle h)
{
    DCB dcb;
    COMMTIMEOUTS timeouts;

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }

    /* ---- 取当前配置 ---- */
    if (!GetCommState(h, &dcb)) {
        return 0;
    }

    /* ---- 配置 9600 8N1 ---- */
    dcb.BaudRate = CBR_9600;        /* 波特率 9600 */
    dcb.ByteSize = 8;               /* 8 数据位 */
    dcb.Parity = NOPARITY;          /* 无校验 */
    dcb.StopBits = ONESTOPBIT;      /* 1 停止位 */

    dcb.fBinary = TRUE;             /* 二进制模式 */
    dcb.fOutxCtsFlow = FALSE;       /* 关闭硬件流控 */
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;              /* 关闭 XON/XOFF 软流控 */
    dcb.fInX = FALSE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fNull = FALSE;

    if (!SetCommState(h, &dcb)) {
        return 0;
    }

    /* ---- 清空缓冲 ---- */
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    /* ---- 超时设置 (轮询方式, 设超时为 50ms) ---- */
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    if (!SetCommTimeouts(h, &timeouts)) {
        return 0;
    }

    return 1;
}

int Serial_ReadLine(SerialHandle h, char *buf, int size)
{
    static char pending[SERIAL_BUF_SIZE];   /* 跨调用保留未读满的行 */
    static int  pendingLen = 0;
    static int  pendingInit = 0;
    char ch;
    DWORD bytesRead;
    int result = 0;

    if (h == INVALID_HANDLE_VALUE || buf == NULL || size <= 0) {
        return -1;
    }

    /* 首次调用初始化 */
    if (!pendingInit) {
        memset(pending, 0, sizeof(pending));
        pendingLen = 0;
        pendingInit = 1;
    }

    /* 读一字节 (非阻塞, 无数据返回超时) */
    if (!ReadFile(h, &ch, 1, &bytesRead, NULL) || bytesRead == 0) {
        return -1;   /* 无数据或出错 */
    }

    /* 把字节加入待处理行 */
    if (ch == '\n') {
        /* 读到行尾 */
        if (pendingLen < size - 1) {
            pending[pendingLen] = '\0';
        }
        /* 去掉行尾 \r */
        if (pendingLen > 0 && pending[pendingLen - 1] == '\r') {
            pending[pendingLen - 1] = '\0';
        }
        strncpy(buf, pending, size - 1);
        buf[size - 1] = '\0';
        result = 1;   /* 读到了一行 */

        pendingLen = 0;
        pending[0] = '\0';
    } else {
        /* 行中字符 */
        if (pendingLen < sizeof(pending) - 1) {
            pending[pendingLen++] = ch;
            pending[pendingLen] = '\0';
        }
        result = 0;   /* 还没凑成一行 */
    }

    return result;
}

int Serial_Write(SerialHandle h, const char *str)
{
    DWORD written = 0;

    if (h == INVALID_HANDLE_VALUE || str == NULL) {
        return -1;
    }
    if (!WriteFile(h, str, (DWORD)strlen(str), &written, NULL)) {
        return -1;
    }
    return (int)written;
}

void Serial_Close(SerialHandle h)
{
    if (h != INVALID_HANDLE_VALUE) {
        CloseHandle(h);
    }
}

int Serial_BytesAvailable(SerialHandle h)
{
    COMSTAT status;
    DWORD errors = 0;

    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!ClearCommError(h, &errors, &status)) {
        return 0;
    }
    return (int)status.cbInQue;
}
