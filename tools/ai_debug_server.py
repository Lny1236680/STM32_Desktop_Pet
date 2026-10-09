#!/usr/bin/env python3
"""
STM32 桌宠 AI 调试服务器
模拟 ESP32 + 云服务器, 通过 USB-TTL 串口与 STM32 通信

接线:  USB-TTL  RX  → STM32 PB6 (TX)
       USB-TTL  TX  → STM32 PB7 (RX)
       USB-TTL GND  → STM32 GND

协议:  $AI:text\r\n  — AI对话内容 (双向)

用法:  python ai_debug_server.py COM3
       或直接运行, 程序会自动检测串口
"""

import sys
import serial
import serial.tools.list_ports

# ====================== 预定义回复 ======================
# 注意: STM32 OLED 8X16 字库只支持 ASCII, 中文会显示为乱码,
#       所以这里的模拟回复用英文, 与云服务器 server.py 的约束保持一致
MOCK_REPLIES = {
    "Hello": "Hello! I am your desktop pet AI~",
    "你好":   "Hello! I am your desktop pet AI~",
    "你是谁":  "I am the pet AI, connected via cloud!",
    "天气":   "Sunny and warm today, have fun!",
    "时间":   "It is around 3pm Beijing time.",
    "笑话":   "Why do programmers love night? Bugs!",
    "唱歌":   "La la la, I am a happy little pet~",
}

DEFAULT_REPLY = "Got it! (This is a mock reply)"

# STM32 OLED 显示上限: 标题 1 行 + 回复 3 行, 每行 16 字符 (8X16 字体)
MAX_REPLY_LEN = 48


def get_reply(text):
    """根据输入文本返回模拟的AI回复, 截断到 OLED 可显示长度"""
    for keyword, reply in MOCK_REPLIES.items():
        if keyword in text:
            return reply[:MAX_REPLY_LEN]
    return DEFAULT_REPLY[:MAX_REPLY_LEN]


def find_stm32_port():
    """自动查找可用的串口"""
    ports = serial.tools.list_ports.comports()
    for port in ports:
        print(f"  发现: {port.device} - {port.description}")
    if ports:
        print(f"\n提示: 通常 STM32 的 USB-TTL 串口以 COM 开头")
    return ports


def main():
    # 解析命令行参数
    if len(sys.argv) > 1:
        port_name = sys.argv[1]
    else:
        print("=== STM32 桌宠 AI 调试服务器 ===\n")
        print("可用串口:")
        ports = find_stm32_port()
        if not ports:
            print("未检测到串口! 请连接 USB-TTL 模块后重试")
            print("或手动指定: python ai_debug_server.py COM3")
            return
        port_name = input("\n请输入串口号 (如 COM3): ").strip()
        if not port_name:
            print("未输入串口号, 退出")
            return

    # 打开串口
    print(f"\n正在连接 {port_name} @ 9600bps ...")
    try:
        ser = serial.Serial(
            port=port_name,
            baudrate=9600,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.5
        )
    except Exception as e:
        print(f"无法打开串口 {port_name}: {e}")
        return

    print(f"已连接! 等待 STM32 消息... (Ctrl+C 退出)\n")
    print("=" * 50)

    try:
        while True:
            # 检查是否有用户输入 (手动发送消息给STM32)
            if ser.in_waiting == 0:
                # 无数据, 短暂休眠后继续
                time.sleep(0.1)
                continue

            # 读取一行
            line = ser.readline()
            if not line:
                continue

            try:
                text = line.decode('utf-8', errors='replace').strip()
            except:
                text = line.decode('gbk', errors='replace').strip()

            if not text:
                continue

            # 解析协议
            if text.startswith('$AI:'):
                query = text[4:]
                print(f"\n[STM32] >>> {query}")

                # 生成模拟回复
                reply = get_reply(query)
                response = f"$AI:{reply}\r\n"
                ser.write(response.encode('utf-8'))

                print(f"[AI]    <<< {reply}")
                print("-" * 50)

            elif text.startswith('$OK'):
                print(f"[STM32] 确认: {text}")

            elif text.startswith('$ERR'):
                print(f"[STM32] 错误: {text}")

            else:
                print(f"[STM32] 未知: {text}")

    except KeyboardInterrupt:
        print("\n\n用户中断, 正在关闭...")
    finally:
        ser.close()
        print("串口已关闭, 再见!")


if __name__ == '__main__':
    main()
