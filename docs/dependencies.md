# 依赖恢复与编译限制

原 ZIP 有两个 `esp-ai` 目录，内容不完全相同；其中 `esp-ai-2.86.49/library.properties` 的 `version` 却写为 `2.84.43`。故没有依据将它作为可复现的精确版本。当前的 `platformio.ini` 是**待验证依赖清单**，不是依赖锁文件。

## 已知外部依赖

- [ESP-AI](https://github.com/wangzongming/esp-ai/releases)：历史工程中的主要语音 AI 客户端（实际所需 2.84.43 / 2.86.49 待确认）。官方对应发行包可能需要从 release asset 单独导入。
- [esp32-eyes](https://github.com/playfultechnology/esp32-eyes)：提供 `Face.h`；历史副本可能含 API 改动，需比对后使用。
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)：历史副本 1.0.1。
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson)：历史 `platformio.ini` 写 `^6.21.2`，随包 `lib/ArduinoJson` 却为 `7.4.1`，存在版本冲突。
- [ArduinoMqttClient](https://github.com/arduino-libraries/ArduinoMqttClient)、[WebSockets](https://github.com/Links2004/arduinoWebSockets)、[OneButton](https://github.com/mathertel/OneButton)、ESP32Servo、NTPClient。
- [U8g2](https://github.com/olikraus/u8g2)、TFT_eSPI、U8g2_for_TFT_eSPI、Adafruit NeoPixel、TJpg_Decoder。
- arduino-libhelix / arduino-liblame 等音频编解码支持库。

## 为什么不直接保留 lib

原压缩包的多数未压缩内容都在 `lib/`，其中混合了多个依赖版本和重复内容。该目录不适合毫无筛选地作为本人原创代码上传。**但仅从目录删除它，并不意味着构建必然成功**。

## 验证前禁止宣称

- 未安装 PlatformIO 工具链、未执行 `pio run`，所以本次不宣称编译通过。
- 原始目录中的 `Face.h` 与在线最新版本可能不兼容。
- 原始 `ESP-AI` 客户端库不能仅凭它的 GitHub 仓库地址保证可安装。
- 核对依赖版本、恢复构建、进行真机测试后，再发布能够开箱即用的仓库。

## 版权

源文件 `Base64_Arturo.cpp/.h` 包含 Arturo Guadalupi 的原始版权声明，本次予以保留。使用第三方依赖时继续遵守其独立许可证；未获得确认前不把此历史代码包整体标注为 MIT 许可。
