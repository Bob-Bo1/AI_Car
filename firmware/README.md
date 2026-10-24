# 固件源码 v0.7.0

使用 ESP-IDF 5.5.4，目标 ESP32-C3、4MB Flash。当前使用方式见[项目首页](../README.md)。

在已激活 ESP-IDF 的终端中执行：

```text
cd firmware
idf.py set-target esp32c3
idf.py build
idf.py -p <你的串口> flash
```

Wi-Fi 凭据通过安装器、设备热点配网页或 USB 本地窗口配置；仓库不附带个人 NVS。
