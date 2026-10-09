/**
  ******************************************************************************
  * @file    WiFi.c
  * @brief   WiFi模块驱动 + AI对话协议处理
  *
  *  硬件: ESP32 通过底板 "蓝牙/WIFI模块" 接口(H11) 连接
  *         USART3: PB10 = TX, PB11 = RX  @9600bps 8N1
  *
  *  协议: $CMD:DATA\r\n  行文本格式
  *    $AI:text   - AI对话内容 (双向)
  *    $OK        - 确认
  *    $ERR       - 错误
  *
  *  注意: USART3 原为蓝牙模块使用, 本文件接管后蓝牙功能被替代,
  *        使用前需拔掉蓝牙模块 (见底板丝印提示 "烧录时需卸下语音模块"
  *        及 H11 接口丝印 "蓝牙/WIFI模块")
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "WiFi.h"
#include "Variable.h"
#include <string.h>

/* ==================== 常量定义 ==================== */

/* 环形缓冲区大小 (256 = 2^8, 可用 & 掩码代替取模) */
#define BUF_SIZE      256
#define BUF_MASK      (BUF_SIZE - 1)

/* 串口引脚: PB10 = TX, PB11 = RX */
#define WIFI_USART    USART3
#define WIFI_TX_PORT  GPIOB
#define WIFI_TX_PIN   GPIO_Pin_10
#define WIFI_RX_PORT  GPIOB
#define WIFI_RX_PIN   GPIO_Pin_11

/* ==================== 静态变量 ==================== */

/* -------- RX 环形缓冲区 (ISR写, 主循环读) -------- */
static uint8_t  rx_buf[BUF_SIZE];
static volatile uint8_t rx_head = 0;  /* 主循环读取位置 */
static volatile uint8_t rx_tail = 0;  /* ISR 写入位置 */

/* ==================== 内部辅助函数 ==================== */

/**
  * @brief  向 RX 缓冲区放入一个字节 (ISR 中调用)
  */
static void rx_buf_put(uint8_t byte)
{
    uint8_t next = (rx_tail + 1) & BUF_MASK;
    if (next != rx_head) {        /* 缓冲区未满 */
        rx_buf[rx_tail] = byte;
        rx_tail = next;
    }
    /* 缓冲区满: 丢弃字节 (避免覆盖未读数据) */
}

/* ==================== USART3 中断处理 (接收) ==================== */

void USART3_IRQHandler(void)
{
    if (USART_GetITStatus(WIFI_USART, USART_IT_RXNE) == SET)
    {
        rx_buf_put((uint8_t)USART_ReceiveData(WIFI_USART));
        USART_ClearITPendingBit(WIFI_USART, USART_IT_RXNE);
    }
}

/* ==================== GPIO & 外设初始化 ==================== */

/**
  * @brief  初始化WiFi串口 (USART3)
  *         PB10=TX复用推挽, PB11=RX上拉输入
  *         @9600bps 8N1, 开RXNE中断
  */
void WiFi_Init(void)
{
    GPIO_InitTypeDef       GPIO_InitStruct;
    USART_InitTypeDef      USART_InitStruct;
    NVIC_InitTypeDef       NVIC_InitStruct;

    /* ---- 开启时钟 ---- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    /* ---- TX引脚: PB10 复用推挽 ---- */
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStruct.GPIO_Pin   = WIFI_TX_PIN;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WIFI_TX_PORT, &GPIO_InitStruct);

    /* ---- RX引脚: PB11 上拉输入 ---- */
    GPIO_InitStruct.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStruct.GPIO_Pin   = WIFI_RX_PIN;
    GPIO_InitStruct.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(WIFI_RX_PORT, &GPIO_InitStruct);

    /* ---- USART3: 9600 8N1, TX+RX ---- */
    USART_InitStruct.USART_BaudRate            = 9600;
    USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStruct.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_InitStruct.USART_Parity              = USART_Parity_No;
    USART_InitStruct.USART_StopBits            = USART_StopBits_1;
    USART_InitStruct.USART_WordLength          = USART_WordLength_8b;
    USART_Init(WIFI_USART, &USART_InitStruct);

    /* ---- 接收中断 ---- */
    USART_ITConfig(WIFI_USART, USART_IT_RXNE, ENABLE);

    /* ---- NVIC 中断优先级 ---- */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitStruct.NVIC_IRQChannel                   = USART3_IRQn;
    NVIC_InitStruct.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStruct.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStruct.NVIC_IRQChannelSubPriority        = 1;
    NVIC_Init(&NVIC_InitStruct);

    /* 使能串口 */
    USART_Cmd(WIFI_USART, ENABLE);

    /* 初始化状态 */
    rx_head = 0;
    rx_tail = 0;
}

/* ==================== 发送API ==================== */

/**
  * @brief  发送一个字节 (阻塞, 等发送寄存器空)
  */
void WiFi_SendByte(uint8_t byte)
{
    while (USART_GetFlagStatus(WIFI_USART, USART_FLAG_TXE) == RESET);
    USART_SendData(WIFI_USART, byte);
}

/**
  * @brief  发送字符串 (阻塞)
  */
void WiFi_SendString(char *str)
{
    while (*str) {
        WiFi_SendByte(*str++);
    }
}

/**
  * @brief  发送字符串 + 回车换行 (阻塞)
  */
void WiFi_SendLine(char *str)
{
    WiFi_SendString(str);
    WiFi_SendByte('\r');
    WiFi_SendByte('\n');
}

/* ==================== 接收API ==================== */

/**
  * @brief  获取RX缓冲区中可读字节数
  */
uint8_t WiFi_GetRxLen(void)
{
    return (rx_tail - rx_head) & BUF_MASK;
}

/**
  * @brief  从RX缓冲区读取一个字节
  * @return 读取到的字节; 若缓冲区为空返回0
  *         调用前需先用 WiFi_GetRxLen() 检查是否有数据
  */
uint8_t WiFi_ReadByte(void)
{
    uint8_t byte = 0;
    if (rx_head != rx_tail) {
        byte = rx_buf[rx_head];
        rx_head = (rx_head + 1) & BUF_MASK;
    }
    return byte;
}

/**
  * @brief  从RX缓冲区读取完整一行 (以 '\n' 结尾)
  * @param  buf: 输出缓冲区
  * @param  maxLen: 最大长度 (含 '\0' 终止符)
  * @return 0=未读取到完整行, 1=成功读取一行
  *         行尾的 \r\n 被去除, 以 '\0' 结尾
  */
uint8_t WiFi_ReadLine(char *buf, uint8_t maxLen)
{
    uint8_t pos = 0;
    uint8_t found = 0;

    /* 临时遍历缓冲区, 查找 '\n' */
    uint8_t tmp_head = rx_head;
    while (tmp_head != rx_tail && pos < maxLen - 1) {
        uint8_t ch = rx_buf[tmp_head];
        tmp_head = (tmp_head + 1) & BUF_MASK;

        if (ch == '\n') {
            found = 1;
            break;
        }
        if (ch != '\r') {
            buf[pos++] = ch;
        }
    }

    if (!found) return 0;  /* 未找到完整行 */

    buf[pos] = '\0';

    /* 已确认有完整行, 正式从缓冲区移除 */
    while (rx_head != tmp_head) {
        rx_head = (rx_head + 1) & BUF_MASK;
    }

    return 1;
}

/* ==================== 协议处理 ==================== */

/**
  * @brief  处理接收到的数据 (主循环中调用)
  *         解析 $AI:... 协议行, 更新全局状态
  */
void WiFi_Process(void)
{
    char line[200];
    while (WiFi_ReadLine(line, sizeof(line) - 1))
    {
        /* ---- 解析 $AI:text ---- */
        if (strncmp(line, "$AI:", 4) == 0)
        {
            /* 复制AI回复文本到全局缓冲区 */
            strncpy(AI_TextBuf, line + 4, sizeof(AI_TextBuf) - 1);
            AI_TextBuf[sizeof(AI_TextBuf) - 1] = '\0';
            AI_ResponseReady = 1;
            AI_Timeout = 1000;  /* 重置超时 (20秒, 等TTS播放完) */
            AI_Mode = 1;
            Face_Mode = 7;
            /* 伪随机选择一个表情 (LCG, 每次回复都会换一个) */
            AI_FacePick = (AI_FacePick * 7 + 3) % 5;
        }
        /* ---- 解析 $OK ---- */
        else if (strcmp(line, "$OK") == 0)
        {
            /* 可在此处理确认 */
        }
        /* ---- 解析 $ERR ---- */
        else if (strcmp(line, "$ERR") == 0)
        {
            /* 可在此处理错误 */
        }
    }
}
