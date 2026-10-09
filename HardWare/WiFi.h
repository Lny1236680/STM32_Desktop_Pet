/**
  ******************************************************************************
  * @file    WiFi.h
  * @brief   WiFi模块驱动头文件
  *          ESP32 通过底板 "蓝牙/WIFI模块" 接口(H11) 连接, 走 USART3 硬件串口
  *          引脚: PB10 = USART3_TX, PB11 = USART3_RX  @9600bps
  *          注意: USART3 原来用于蓝牙模块, 迁移后需拔掉蓝牙模块
  *          协议: $CMD:DATA\r\n 行文本格式
  ******************************************************************************
  */

#ifndef __WIFI_H
#define __WIFI_H

#include "stm32f10x.h"

/* ==================== 公共API ==================== */

void WiFi_Init(void);
void WiFi_SendByte(uint8_t byte);
void WiFi_SendString(char *str);
void WiFi_SendLine(char *str);
uint8_t WiFi_GetRxLen(void);
uint8_t WiFi_ReadByte(void);
uint8_t WiFi_ReadLine(char *buf, uint8_t maxLen);
void WiFi_Process(void);

/* ==================== 中断处理 ====================
 * USART3_IRQHandler 定义在 WiFi.c 中
 * (BlueTooth.c 中也有 USART3_IRQHandler, 二选一, 本工程已移除蓝牙)
 */

#endif /* __WIFI_H */
