# 云萌宝伴 · 异地情感交互机器宠物

> **2025 年多人协作的 ESP32-S3 历史项目源码归档。** 由历史 `ESP-AI-master.zip` 经安全清理、精简整理而来，**尚未验证可以独立编译或在真机运行**。

## 项目内容

原工程包含 ESP32-S3 主控、讯飞语音服务、MQTT 远程交互、OLED 表情与舵机控制，以及留言服务相关逻辑。以上仅说明**代码中的设计或实现模块**，不代表所有功能均已通过测试。

## 本次整理

- 删除确认属于编辑器自动生成的署名和不适用的公司版权模板；保留 `Base64_Arturo` 的原作者版权信息。
- 将讯飞 APPID、APISecret、APIKey、MQTT 凭据及原留言服务地址迁移至本地 `include/project_config.h`；仓库中仅提供无凭据的示例文件。
- 排除原始 `lib/` 中约 255 MB 第三方依赖副本、重复库、IDE 缓存和编译产物。
- 在 `platformio.ini` 中登记可识别的依赖，**但 ESP-AI/esp32-eyes 的精确版本和兼容性仍待恢复**。
- 不提交含旧密钥的 ZIP、原始源码或历史提交记录。

## 本地配置

复制 `include/project_config.example.h` 为 `include/project_config.h`，填入**新生成的**密钥和自己控制的服务端地址：

```bash
cp include/project_config.example.h include/project_config.h
pio run -e esp32-s3-devkitc-1
```

以上命令仅说明构建入口，**不代表当前构建已验证通过**。请先阅读 [依赖与构建限制](docs/dependencies.md)。

## 目录

```text
src/                               历史应用代码（脱敏）
include/project_config.example.h   本地配置模板（无真实密钥）
platformio.ini                     PlatformIO 配置（依赖待核实）
docs/dependencies.md               第三方依赖与构建限制
docs/security-review.md            安全清理记录
docs/cleanup-manifest.json         来源校验与修改清单
```

## 贡献与版权

这是一个**团队历史工程**，不宣称由仓库维护者独立开发。编辑器自动添加的署名不等于真实著作权归属，第三方代码版权仍保留。本仓库不附加统一开源许可证；若准备公开发布、授权复制或商用，应先核实团队成员授权和第三方许可。

> **安全提醒：** 如果带密钥的旧 ZIP 曾被分享或上传，必须到讯飞开放平台及 MQTT 服务侧轮换旧凭据。删除源码内明文值并不能使旧密钥失效。
