#include "project_config.h"  // Local credentials/configuration; ignored by Git
#include "xf_stt.h"
#include <WebSocketsClient.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <driver/i2s.h>
#include <Arduino.h>
#include <time.h>
#include <esp-ai.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#undef typeof // 解决ArduinoJson与ESP32的typeof宏冲突
#include <ArduinoJson.h>
#include "MQTTClient.h"
#include <HTTPClient.h>
#include "Face.h"

// 外部变量声明
extern bool isPaired;
extern I2SStream esp_ai_i2s_input;
extern bool isInMessageMode;
extern void exitMessageMode();
extern bool waitingForMessageResponse;
extern void processMessageResponse(const String &response);

const char *STTAPPID = PROJECT_XF_APPID;
const char *STTAPISecret = PROJECT_XF_API_SECRET;
const char *STTAPIKey = PROJECT_XF_API_KEY;
WebSocketsClient xf_client;
int16_t *xf_pcm_data = nullptr;
uint xf_recordingSize = 0;

// NTP时间配置
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

// 录音区间采集相关变量
static int16_t *xf_record_buffer = nullptr;
static size_t xf_recordingSizeBtn = 0;
static bool xf_isRecording = false;
static const int xf_maxSeconds = 10;
static const int xf_sample_rate = 16000;
static const int xf_maxSamples = xf_sample_rate * xf_maxSeconds;

String xf_stt_result = "";

// 初始化NTP客户端
void setup_ntp_client()
{
    timeClient.begin();
    timeClient.setTimeOffset(0); // GMT时间偏移
}

String unixTimeToGMTString(time_t unixTime)
{
    char buffer[80];
    struct tm timeinfo;
    gmtime_r(&unixTime, &timeinfo);
    strftime(buffer, sizeof(buffer), "%a, %d %b %Y %H:%M:%S GMT", &timeinfo);
    return String(buffer);
}

String getDateTime()
{
    timeClient.update();
    unsigned long epochTime = timeClient.getEpochTime();
    return unixTimeToGMTString(epochTime);
}

String formatDateForURL(String dateString)
{
    dateString.replace(" ", "+");
    dateString.replace(",", "%2C");
    dateString.replace(":", "%3A");
    return dateString;
}

String XF_wsUrl(const char *Secret, const char *Key, String request, String host)
{
    String timeString = getDateTime();
    String signature_origin = "host: " + host + "\n";
    signature_origin += "date: " + timeString + "\n";
    signature_origin += "GET " + request + " HTTP/1.1";
    unsigned char hmacResult[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_type_t md_type = MBEDTLS_MD_SHA256;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(md_type), 1);
    mbedtls_md_hmac_starts(&ctx, (const unsigned char *)Secret, strlen(Secret));
    mbedtls_md_hmac_update(&ctx, (const unsigned char *)signature_origin.c_str(), signature_origin.length());
    mbedtls_md_hmac_finish(&ctx, hmacResult);
    mbedtls_md_free(&ctx);
    String base64Result = base64::encode(hmacResult, 32);
    String authorization_origin = "api_key=\"" + String(Key) + "\", algorithm=\"hmac-sha256\", headers=\"host date request-line\", signature=\"" + base64Result + "\"";
    String authorization = base64::encode(authorization_origin);
    String url = "ws://" + host + request;
    url += "?authorization=" + authorization;
    url += "&date=" + formatDateForURL(timeString);
    url += "&host=" + host;
    return url;
}

// 讯飞STT WebSocket回调
void xf_ws_on_event(WStype_t type, uint8_t *payload, size_t length)
{
    switch (type)
    {
    case WStype_DISCONNECTED:
        Serial.println("[STT] WebSocket连接断开");
        extern Face *face;
        if (face) {
            face->ShowNotification("讯飞服务断开");
            face->SetEmotion("发愁");
        }
        break;
    case WStype_CONNECTED:
        Serial.println("[STT] WebSocket连接成功");
        break;
    case WStype_ERROR:
        Serial.println("[STT] WebSocket连接错误");
        extern Face *face;
        if (face) {
            face->ShowNotification("讯飞服务断开");
            face->SetEmotion("发愁");
        }
        break;
    case WStype_TEXT:
    {
        DynamicJsonDocument doc(2048);
        DeserializationError error = deserializeJson(doc, String((char *)payload));
        if (error)
            break;
        if (doc["data"].containsKey("result"))
        {
            JsonArray ws = doc["data"]["result"]["ws"];
            for (JsonObject word : ws)
            {
                xf_stt_result += word["cw"][0]["w"].as<const char *>();
            }
            if (doc["data"].containsKey("status") && doc["data"]["status"] == 2)
            {
                Serial.print("[STT] 识别结果: ");
                Serial.println(xf_stt_result);
                // 新增：OLED显示识别文本
                extern Face *face;
                if (face && xf_stt_result.length() > 0)
                {
                    face->ShowNotification("识别成功");
                    face->SetEmotion("无情绪");
                    face->SetChatMessage(xf_stt_result);
                }
                // 优先处理等待“是/否”回答
                if (waitingForMessageResponse && xf_stt_result.length() > 0)
                {
                    processMessageResponse(xf_stt_result);
                    xf_stt_result = "";
                    return;
                }
                // 留言模式下保存留言并退出
                if (isInMessageMode && xf_stt_result.length() > 0)
                {
                    addMessage(DEVICE_NAME, xf_stt_result);
                    exitMessageMode();
                    xf_stt_result = "";
                    return;
                }
                if (isPaired && xf_stt_result.length() > 0)
                {
                    // 优先检查是否为动作指令
                    extern bool containsActionCommand(const String &text);
                    extern String getActionDescription(const String &text);
                    if (containsActionCommand(xf_stt_result))
                    {
                        // 如果是动作指令，发送action消息而不是tts消息
                        String actionDesc = getActionDescription(xf_stt_result);
                        Serial.println("[远程控制] 检测到动作指令：" + actionDesc + "，发送给另一个设备");
                        String actionPayload = "{\"action\":\"" + xf_stt_result + "\"}";
                        mqtt_send(actionPayload);
                    }
                    else
                    {
                        // 如果不是动作指令，才发送TTS消息
                        String jsonPayload = "{\"tts\":\"" + xf_stt_result + "\"}";
                        mqtt_send(jsonPayload);
                    }
                }
                xf_stt_result = "";
            }
        }
        break;
    }
    default:
        break;
    }
}

void XF_STTsend()
{
    uint8_t status = 0;
    int dataSize = 1280 * 8;
    int audioDataSize = xf_recordingSize * 2;
    uint lan = audioDataSize / dataSize;
    uint lan_end = audioDataSize % dataSize;
    if (lan_end > 0)
        lan++;

    String host_url = XF_wsUrl(STTAPISecret, STTAPIKey, "/v2/iat", "ws-api.xfyun.cn");
    Serial.println("[STT] 连接讯飞STT服务器...");
    String host = "ws-api.xfyun.cn";
    uint16_t port = 80;
    String url = host_url.substring(host_url.indexOf('/', 5));
    xf_client.begin(host.c_str(), port, url.c_str());
    xf_client.onEvent(xf_ws_on_event);
    Serial.println("[STT] 等待WebSocket连接...");
    unsigned long connect_start = millis();
    while (millis() - connect_start < 3000)
    {
        xf_client.loop();
        if (xf_client.isConnected())
        {
            break;
        }
        delay(10);
    }
    if (!xf_client.isConnected())
    {
        Serial.println("[STT] WebSocket连接失败");
        return;
    }
    for (int i = 0; i < lan; i++)
    {
        if (i == (lan - 1))
            status = 2;
        String input = "{";
        if (status == 0)
        {
            input += "\"common\":{ \"app_id\":\"" + String(STTAPPID) + "\" },";
            input += "\"business\":{\"domain\": \"iat\", \"language\": \"zh_cn\", \"accent\": \"mandarin\", \"vinfo\":1,\"vad_eos\":10000},";
            input += "\"data\":{\"status\": 0, \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
        }
        else
        {
            input += "\"data\":{\"status\": " + String(status) + ", \"format\": \"audio/L16;rate=16000\",\"encoding\": \"raw\",\"audio\":\"";
        }
        int sendSize = (status == 2 && lan_end > 0) ? lan_end : dataSize;
        String base64audioString = base64::encode((uint8_t *)xf_pcm_data + i * dataSize, sendSize);
        input += base64audioString + "\"}}";
        xf_client.sendTXT(input);
        if (status == 0)
            status = 1;
        delay(30);
    }
    // 等待服务器返回，最多8秒
    unsigned long t_start = millis();
    while (millis() - t_start < 8000)
    {
        xf_client.loop();
        delay(10);
    }
}

// 录音区间采集：按下开始，松开结束并上传
void startXFSTT_record_begin()
{
    if (xf_isRecording)
        return;
    xf_record_buffer = (int16_t *)ps_malloc(xf_maxSamples * sizeof(int16_t));
    if (!xf_record_buffer)
    {
        Serial.println("[STT] 录音内存分配失败");
        return;
    }
    xf_recordingSizeBtn = 0;
    xf_isRecording = true;
    Serial.println("[STT] 开始录音...");
}
void startXFSTT_record_end()
{
    if (!xf_isRecording)
        return;
    xf_isRecording = false;
    Serial.printf("[STT] 录音结束，采样点数: %d\n", xf_recordingSizeBtn);
    if (xf_recordingSizeBtn > 0)
    {
        xf_pcm_data = xf_record_buffer;
        xf_recordingSize = xf_recordingSizeBtn;
        XF_STTsend();
    }
    free(xf_record_buffer);
    xf_record_buffer = nullptr;
    xf_recordingSizeBtn = 0;
}
// 需在main.cpp loop中定期调用
void startXFSTT_record_loop()
{
    if (xf_isRecording && xf_recordingSizeBtn < xf_maxSamples)
    {
        size_t bytesRead = esp_ai_i2s_input.readBytes(
            (uint8_t *)(xf_record_buffer + xf_recordingSizeBtn),
            256 * sizeof(int16_t));
        xf_recordingSizeBtn += bytesRead / 2;
    }
    xf_client.loop();
}

// 保留原有一键采集上传逻辑
void startXFSTT()
{
    const int sample_rate = 16000;
    const int seconds = 5;
    const int sample_count = sample_rate * seconds;
    int16_t *buffer = (int16_t *)ps_malloc(sample_count * sizeof(int16_t));
    if (!buffer)
    {
        Serial.println("讯飞STT内存分配失败");
        return;
    }
    size_t bytes_read = 0;
    esp_ai_i2s_input.readBytes((uint8_t *)buffer, sample_count * sizeof(int16_t));
    xf_pcm_data = buffer;
    xf_recordingSize = sample_count;
    XF_STTsend();
    free(buffer);
    xf_client.loop();
}

/**
 * @description: 添加留言到服务器
 * @param {String} person_username 发送者用户名
 * @param {String} message 留言内容
 * @return {void}
 */
void addMessage(const String &person_username, const String &message)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi未连接，无法发送留言");
        return;
    }

    HTTPClient http;
    http.begin(PROJECT_MESSAGE_POST_URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String postData = "person=" + person_username + "&message=" + message;
    int httpCode = http.POST(postData);

    if (httpCode > 0)
    {
        String response = http.getString();
        Serial.println("留言发送响应：" + response);
        if (response.indexOf("添加成功") != -1)
        {
            Serial.println("留言发送成功");
        }
        else
        {
            Serial.println("留言发送失败：" + response);
        }
    }
    else
    {
        Serial.print("留言发送请求失败：");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}