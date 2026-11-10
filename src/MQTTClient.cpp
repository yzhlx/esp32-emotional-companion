#include "project_config.h"  // Local credentials/configuration; ignored by Git
#include "MQTTClient.h"
#include <ArduinoMqttClient.h>
#include <WiFiClientSecure.h>
#undef typeof // 解决ArduinoJson与ESP32的typeof宏冲突
#include <ArduinoJson.h>
#include <Arduino.h>
#include <esp-ai.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "xf_stt.h"
#include "Face.h"

// MQTT 服务器信息（请根据实际情况修改）
static const char broker[] = PROJECT_MQTT_BROKER;
static const int port = PROJECT_MQTT_PORT; // MQTTs默认端口

// 全局可配置变量
static String g_mqtt_clientid = "";
static String g_mqtt_user = "";
static String g_mqtt_pass = "";
static String g_mqtt_inTopic = "";
static String g_mqtt_outTopic = "";

// CA证书（如需验证服务器）
static const char root_ca[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIDrzCCApegAwIBAgIQCDvgVpBCRrGhdWrJWZHHSjANBgkqhkiG9w0BAQUFADBh
MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3
d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBD
QTAeFw0wNjExMTAwMDAwMDBaFw0zMTExMTAwMDAwMDBaMGExCzAJBgNVBAYTAlVT
MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j
b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IENBMIIBIjANBgkqhkiG
9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4jvhEXLeqKTTo1eqUKKPC3eQyaKl7hLOllsB
CSDMAZOnTjC3U/dDxGkAV53ijSLdhwZAAIEJzs4bg7/fzTtxRuLWZscFs3YnFo97
nh6Vfe63SKMI2tavegw5BmV/Sl0fvBf4q77uKNd0f3p4mVmFaG5cIzJLv07A6Fpt
43C/dxC//AH2hdmoRBBYMql1GNXRor5H4idq9Joz+EkIYIvUX7Q6hL+hqkpMfT7P
T19sdl6gSzeRntwi5m3OFBqOasv+zbMUZBfHWymeMr/y7vrTC0LUq7dBMtoM1O/4
gdW7jVg/tRvoSSiicNoxBN33shbyTApOB6jtSj1etX+jkMOvJwIDAQABo2MwYTAO
BgNVHQ8BAf8EBAMCAYYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUA95QNVbR
TLtm8KPiGxvDl7I90VUwHwYDVR0jBBgwFoAUA95QNVbRTLtm8KPiGxvDl7I90VUw
DQYJKoZIhvcNAQEFBQADggEBAMucN6pIExIK+t1EnE9SsPTfrgT1eXkIoyQY/Esr
hMAtudXH/vTBH1jLuG2cenTnmCmrEbXjcKChzUyImZOMkXDiqw8cvpOp/2PV5Adg
06O/nVsJ8dWO41P0jmP6P6fbtGbfYmbW0W5BjfIttep3Sp+dWOIrWcBAI+0tKIJF
PnlUkiaY4IBIqDfv8NZ5YBberOgOzW6sRBc4L0na4UU+Krk2U886UAb3LujEV0ls
YSEY1QSteDwsOoBrp+uvFRTp2InBuThs4pFsiv9kuXclVzDAGySj4dzp30d8tbQk
CAUw7C29C79Fv1C5qfPrmAESrciIxpg0X40KPMbp1ZWVbd4=
-----END CERTIFICATE-----
)EOF";

// MQTT客户端和WiFi客户端
static WiFiClientSecure wifiClient;
static MqttClient mqttClient(wifiClient);

// 全局变量
static String inputString = "";
extern ESP_AI esp_ai; // 声明外部esp_ai对象
extern bool isPaired;
extern bool waitingPairRequest;
extern Face *face;                 // 声明外部Face指针
unsigned long pairRequestTime = 0; // 配对请求时间变量

extern I2SStream esp_ai_i2s_input;

// MQTT连接状态和重连计数
static bool mqtt_was_connected = false;
static int mqtt_reconnect_count = 0;
static unsigned long last_reconnect_attempt = 0;
static const unsigned long reconnect_interval = 5000; // 5秒重连间隔

/**
 * @description: MQTT消息接收回调函数
 * @param {int} messageSize - 接收到的消息大小
 * @return {void}
 */
static void onMqttMessage(int messageSize)
{
    if (messageSize <= 0 || messageSize > 1024)
    {
        Serial.println("[MQTT] 收到无效消息大小");
        return;
    }

    Serial.print("[MQTT] 接收到主题 '");
    Serial.print(mqttClient.messageTopic());
    Serial.print("', 长度 ");
    Serial.print(messageSize);
    Serial.println(" 字节:");

    inputString.clear(); // 确保字符串为空

    // 读取消息内容
    while (mqttClient.available() && inputString.length() < messageSize)
    {
        inputString += (char)mqttClient.read();
    }

    if (inputString.length() > 0)
    {
        Serial.print("消息内容: ");
        Serial.println(inputString);

        // 解析JSON消息
        DynamicJsonDocument json_msg(1024);
        DeserializationError error = deserializeJson(json_msg, inputString);
        if (error)
        {
            Serial.print("JSON解析失败: ");
            Serial.println(error.c_str());
            inputString.clear();
            return;
        }

        // 新增：收到消息先停止当前会话
        esp_ai.stopSession();

        // 检查是否有emotion字段，有则更新表情
        if (json_msg.containsKey("emotion"))
        {
            String emotion = json_msg["emotion"].as<String>();
            Serial.print("收到情绪指令，更新表情: ");
            Serial.println(emotion);

            // 更新表情显示
            if (face)
            {
                face->SetEmotion(emotion);
                face->ShowNotification("对方情绪: " + emotion);
            }
        }

        // 检查是否有tts字段，有则TTS朗读
        if (json_msg.containsKey("tts"))
        {
            String ttsText = json_msg["tts"].as<String>();
            Serial.print("收到TTS指令，播放文本: ");
            Serial.println(ttsText);
            if (face)
            {
                face->ShowNotification("接收成功");
                face->SetChatMessage("对方说: " + ttsText);
            }

            esp_ai.tts(ttsText);
        }
        // 收到配对请求，A和B都TTS提示"对方请求配对"，并设置waitingPairRequest=true
        if (json_msg.containsKey("pair") && json_msg["pair"].as<bool>() == true)
        {
            esp_ai.tts("对方请求配对");
            waitingPairRequest = true;
            // 记录配对请求时间，用于超时检测
            pairRequestTime = millis();
        }
        // 收到pair_accept字段，A和B都TTS配对成功
        if (json_msg.containsKey("pair_accept") && json_msg["pair_accept"].as<bool>() == true)
        {
            esp_ai.tts("配对成功");
            isPaired = true;
            waitingPairRequest = false;
            // 重置配对请求时间
            pairRequestTime = 0;

            // 配对成功后提示可用的动作指令
            if (face)
            {
                face->ShowNotification("配对成功！");
            }
        }
        // 收到remote_off字段，TTS提示对方已关闭异地模式
        if (json_msg.containsKey("remote_off") && json_msg["remote_off"].as<bool>() == true)
        {
            esp_ai.tts("对方已关闭异地模式");
            isPaired = false;
            waitingPairRequest = false;
            // 重置配对请求时间
            pairRequestTime = 0;
        }

        // 收到动作指令，执行对应的动作
        if (json_msg.containsKey("action"))
        {
            String actionText = json_msg["action"].as<String>();
            Serial.print("收到远程动作指令: ");
            Serial.println(actionText);

            // 语音提示收到动作指令
            esp_ai.tts("收到动作指令");

            // 更新表情显示
            if (face)
            {
                extern String getActionDescription(const String &text);
                String actionDesc = getActionDescription(actionText);
                face->ShowNotification("收到动作指令: " + actionDesc);
                face->SetEmotion("专注");
            }

            // 执行动作指令
            // 这里需要调用动作识别函数，但需要避免重复发送
            // 我们直接调用动作执行函数
            extern void detectAction(const String &text, bool isRemote);
            detectAction(actionText, true);
        }

        // 收到reminder消息，处理提醒
        if (json_msg.containsKey("reminder"))
        {
            String reminderText = json_msg["reminder"].as<String>();
            Serial.print("收到提醒消息: ");
            Serial.println(reminderText);

            // 调用reminder处理函数
            extern void processReminder(const String &reminderText);
            processReminder(reminderText);
        }

        // 新增：处理Android_Set主题的本地模式指令
        // 检查消息来源主题，如果是Android_Set主题，则处理本地模式指令
        String messageTopic = mqttClient.messageTopic();
        if (messageTopic == "Android_Set")
        {
            Serial.println("[本地模式] 收到Android_Set主题指令");

            // 处理各种指令类型
            if (json_msg.containsKey("action"))
            {
                String action = json_msg["action"].as<String>();
                Serial.print("[本地模式] 收到指令: ");
                Serial.println(action);

                // 语音提示收到指令
                esp_ai.tts("收到指令");

                // 更新表情显示
                if (face)
                {
                    face->ShowNotification("收到指令: " + action);
                    face->SetEmotion("专注");
                }

                // 执行对应的动作指令
                extern void detectAction(const String &text, bool isRemote);
                detectAction(action, true);
            }
            // 处理直接的动作指令（兼容性）
            else if (json_msg.containsKey("action"))
            {
                String actionText = json_msg["action"].as<String>();
                Serial.print("[本地模式] 收到动作指令: ");
                Serial.println(actionText);

                // 语音提示收到动作指令
                esp_ai.tts("收到动作指令");

                // 更新表情显示
                if (face)
                {
                    extern String getActionDescription(const String &text);
                    String actionDesc = getActionDescription(actionText);
                    face->ShowNotification("收到动作指令: " + actionDesc);
                    face->SetEmotion("专注");
                }

                // 执行动作指令
                extern void detectAction(const String &text, bool isRemote);
                detectAction(actionText, true);
            }
            // 处理TTS指令
            else if (json_msg.containsKey("tts"))
            {
                String ttsText = json_msg["tts"].as<String>();
                Serial.print("[本地模式] 收到TTS指令: ");
                Serial.println(ttsText);
                esp_ai.tts(ttsText);
            }
            // 处理情绪指令
            else if (json_msg.containsKey("emotion"))
            {
                String emotion = json_msg["emotion"].as<String>();
                Serial.print("[本地模式] 收到情绪指令: ");
                Serial.println(emotion);

                if (face)
                {
                    face->SetEmotion(emotion);
                    face->ShowNotification("收到情绪指令: " + emotion);
                }
            }
        }

        inputString.clear();
    }
}

/**
 * @description: 设置MQTT连接参数
 * @param {String} clientid - 客户端ID
 * @param {String} user - 用户名
 * @param {String} pass - 密码
 * @param {String} inTopic - 订阅主题
 * @param {String} outTopic - 发布主题
 * @return {void}
 */
void mqtt_set_params(const String &clientid, const String &user, const String &pass, const String &inTopic, const String &outTopic)
{
    g_mqtt_clientid = clientid;
    g_mqtt_user = user;
    g_mqtt_pass = pass;
    g_mqtt_inTopic = inTopic;
    g_mqtt_outTopic = outTopic;
}

/**
 * @description: 初始化MQTT客户端
 * @return {void}
 */
void mqtt_init()
{
    wifiClient.setCACert(root_ca);
    mqttClient.setId(g_mqtt_clientid.c_str());
    mqttClient.setUsernamePassword(g_mqtt_user.c_str(), g_mqtt_pass.c_str());
    mqttClient.onMessage(onMqttMessage);
    // 设置保持连接参数
    mqttClient.setKeepAliveInterval(30 * 1000); // 30秒
}

/**
 * @description: 连接MQTT服务器
 * @return {bool} 连接是否成功
 */
bool mqtt_connect()
{
    // 检查WiFi连接状态
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[MQTT] WiFi未连接，无法连接MQTT");
        return false;
    }

    // 尝试连接MQTT服务器
    Serial.println("[MQTT] 正在连接MQTT服务器...");
    if (!mqttClient.connect(broker, port))
    {
        Serial.print("[MQTT] 连接失败！错误代码 = ");
        Serial.println(mqttClient.connectError());
        return false;
    }

    Serial.println("[MQTT] 已成功连接到MQTT服务器！");

    // 订阅主题
    if (!mqttClient.subscribe(g_mqtt_inTopic.c_str(), 1))
    {
        Serial.println("[MQTT] 主题订阅失败！");
        return false;
    }

    // 订阅reminder/notification主题
    if (!mqttClient.subscribe("reminder/notification", 1))
    {
        Serial.println("[MQTT] reminder主题订阅失败！");
        return false;
    }

    // 订阅Android_Set主题（用于本地模式接收指令）
    if (!mqttClient.subscribe("Android_Set", 1))
    {
        Serial.println("[MQTT] Android_Set主题订阅失败！");
        return false;
    }

    Serial.print("[MQTT] 已订阅主题: ");
    Serial.println(g_mqtt_inTopic);
    Serial.println("[MQTT] 已订阅reminder/notification主题");
    Serial.println("[MQTT] 已订阅Android_Set主题（本地模式指令接收）");

    mqtt_was_connected = true;
    mqtt_reconnect_count = 0;

    return true;
}

/**
 * @description: 定期处理MQTT消息，检查连接状态
 * @return {void}
 */
void mqtt_poll()
{
    // 处理MQTT消息
    mqttClient.poll();

    // 检查连接状态并进行重连
    if (mqtt_was_connected && !mqttClient.connected())
    {
        mqtt_was_connected = false;
        Serial.println("[MQTT] 连接已断开");

        // 尝试重新连接
        unsigned long current_time = millis();
        if (current_time - last_reconnect_attempt >= reconnect_interval)
        {
            last_reconnect_attempt = current_time;
            mqtt_reconnect_count++;

            if (mqtt_reconnect_count <= 3)
            {
                Serial.print("[MQTT] 尝试重新连接 #");
                Serial.println(mqtt_reconnect_count);
                mqtt_connect();
            }
            else
            {
                Serial.println("[MQTT] 重连次数过多，停止尝试");
            }
        }
    }
}

/**
 * @description: 发送MQTT消息
 * @param {String} payload - 消息内容
 * @return {void}
 */
void mqtt_send(const String &payload)
{
    if (!mqttClient.connected())
    {
        Serial.println("[MQTT] 未连接，无法发送消息");
        return;
    }

    if (payload.length() == 0)
    {
        Serial.println("[MQTT] 消息内容为空，不发送");
        return;
    }

    // 发送消息
    bool success = false;
    try
    {
        mqttClient.beginMessage(g_mqtt_outTopic.c_str(), payload.length(), false, 1, false);
        mqttClient.print(payload);
        success = mqttClient.endMessage();
    }
    catch (...)
    {
        Serial.println("[MQTT] 发送消息时出现异常");
        success = false;
    }

    if (success)
    {
        Serial.print("[MQTT] 消息发送成功: ");
        Serial.println(payload);
    }
    else
    {
        Serial.println("[MQTT] 消息发送失败");
    }
}