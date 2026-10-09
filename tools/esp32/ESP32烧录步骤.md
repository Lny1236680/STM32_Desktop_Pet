# ESP32-S3 烧录步骤 (Arduino IDE 2.x)

> 固件位置: `tools/esp32/esp32_ai_pet/esp32_ai_pet.ino`
> 已配置: WiFi=YOUR_WIFI_SSID / 密码=YOUR_WIFI_PASSWORD / 服务器=172.20.10.7:8000

## 一、安装 Arduino IDE 2.x
1. 下载安装 [Arduino IDE 2.x](https://www.arduino.cc/en/software) (Windows 版)
2. 一路下一步安装完成

## 二、安装 ESP32-S3 开发板支持
1. 打开 Arduino IDE
2. `文件` > `首选项` > 附加开发板管理器网址 (Additional boards manager URLs) 填入:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. 点 `工具` > `开发板` > `开发板管理器` (Boards Manager)
4. 搜索 `esp32`，安装 **esp32 by Espressif Systems**（最新版，含 S3 支持）

## 三、安装 ESP8266Audio 库
1. `工具` > `管理库` (Library Manager)
2. 搜索 `ESP8266Audio`
3. 安装 **ESP8266Audio by earlephilhower**

## 四、打开并配置固件
1. `文件` > `打开`，选择 `tools/esp32/esp32_ai_pet/esp32_ai_pet.ino`
2. `工具` 菜单设置：
   - **开发板 (Board)**: `ESP32S3 Dev Module`
   - **USB CDC On Boot**: `Enabled`（可看串口日志）
   - **分区方案 (Partition Scheme)**: `Huge APP (3MB No OTA/1MB SPIFFS)` 或带大 LittleFS 的选项
   - **Flash Size**: `8MB`（如板子是 8MB）或按实际
   - **端口 (Port)**: 选择 ESP32-S3 对应的 COM 口

## 五、烧录
1. 用 USB 线连接 ESP32-S3 到电脑
2. 点击 `→`（上传）按钮
3. 等待编译 + 上传完成（首次编译较慢，需下载工具链）

## 六、验证
- 打开 `工具` > `串口监视器` (Serial Monitor)，波特率 `115200`
- 应看到:
  - `=== ESP32-S3 AI Pet Gateway (with TTS) ===`
  - `[FS] LittleFS OK`
  - `WiFi connected, IP: 172.20.10.x`
  - 与 STM32 通信日志

## 常见问题
| 问题 | 解决 |
|------|------|
| 上传失败 `No device found` | 按住 ESP32-S3 板上 **BOOT 键**再点上传，或用 自动下载电路 |
| LittleFS mount FAILED | 分区方案必须选带大 Flash 的 (Huge APP)，不要用默认 |
| 编译报错找不到库 | 确认 ESP8266Audio 已安装 |
| 串口监视器乱码 | 波特率改 115200 |
| 烧录后没声音 | 检查 MAX98357A 接线 + 喇叭 + 音量 |
