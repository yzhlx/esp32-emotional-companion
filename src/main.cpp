#include "project_config.h"  // Local credentials/configuration; ignored by Git
#include <esp-ai.h>
#include "MQTTClient.h"
#include <OneButton.h>
#include <base64.h>
#include <mbedtls/md.h>
#include <WebSocketsClient.h>
#include <driver/i2s.h>
#include "xf_stt.h"
#include <freertos/FreeRTOS.h>
#include <esp_task_wdt.h>

// 外部函数声明
extern void addMessage(const String &person_username, const String &message);
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include "Face.h"
#include "ServoControl.h"
#include <HTTPClient.h>
#undef typeof // 解决ArduinoJson与ESP32的typeof宏冲突
#include <ArduinoJson.h>

// 全局对象定义
ESP_AI esp_ai;
Face *face = nullptr;

// 舵机控制引脚定义
#define LEFT_FRONT_PIN 8
#define LEFT_BACK_PIN 9
#define RIGHT_FRONT_PIN 19
#define RIGHT_BACK_PIN 20
#define BUTTON_PIN 7 // 多功能按钮引脚
OneButton button(BUTTON_PIN, true, true);

// 全局变量
bool isPaired = false;
bool waitingPairRequest = false;              // B设备：是否收到A的配对请求
static String lastAsrText = "";               // 保存最近一次语音识别文本
static bool isRecording = false;              // 录音状态标志
static bool isProcessingRemoteAction = false; // 是否正在处理远程动作指令

// reminder相关变量
static String currentReminder = "";                  // 当前提醒内容
static int reminderRepeatCount = 0;                  // 提醒重复次数
static unsigned long lastReminderTime = 0;           // 上次提醒时间
static const unsigned long REMINDER_INTERVAL = 2000; // 提醒间隔时间（毫秒）
static const int REMINDER_MAX_REPEAT = 3;            // 最大重复次数

// 留言功能相关变量
bool isInMessageMode = false;                       // 是否处于留言模式
bool waitingForMessageResponse = false;             // 是否等待用户对留言模式的响应
extern unsigned long pairRequestTime;               // 配对请求时间戳（外部变量）
static const unsigned long MESSAGE_TIMEOUT = 10000; // 10秒超时时间
static bool hasCheckedMessagesOnBoot = false;       // 开机是否已检查留言

// 舵机配置
static ServoConfig robot_servo_config = {
    LEFT_FRONT_PIN,
    LEFT_BACK_PIN,
    RIGHT_FRONT_PIN,
    RIGHT_BACK_PIN,
    500, 2500};

// 多线程相关变量
TaskHandle_t mainTaskHandle = NULL;
TaskHandle_t mqttTaskHandle = NULL;
TaskHandle_t buttonTaskHandle = NULL;

TaskHandle_t messageTaskHandle = NULL;
TaskHandle_t sttTaskHandle = NULL;
TaskHandle_t servoTaskHandle = NULL;
TaskHandle_t faceTaskHandle = NULL;

// 互斥锁
SemaphoreHandle_t mainMutex = NULL;
SemaphoreHandle_t mqttMutex = NULL;

SemaphoreHandle_t messageMutex = NULL;
SemaphoreHandle_t sttMutex = NULL;
SemaphoreHandle_t servoMutex = NULL;
SemaphoreHandle_t globalMutex = NULL; // 全局互斥锁，保护共享变量

// 任务控制标志
volatile bool systemRunning = true;

// 任务函数声明
void ServoTask(void *pvParameters);
void FaceTask(void *pvParameters);
void MainTask(void *pvParameters);
void MQTTTask(void *pvParameters);
void ButtonTask(void *pvParameters);

void MessageTask(void *pvParameters);
void STTTask(void *pvParameters);

// 留言功能函数声明
void checkForNewMessages();
void enterMessageMode();
void exitMessageMode();
void processMessageResponse(const String &response);
bool isPositiveResponse(const String &text);
bool isNegativeResponse(const String &text);

// reminder功能函数声明
void processReminder(const String &reminderText);
void updateReminder();

// 新增：服务连接成功后留言检查回调
void onServiceReady()
{
    static bool hasCheckedMessages = false;
    if (!hasCheckedMessages)
    {
        Serial.println("[留言功能] 服务连接成功后检查留言");
        checkForNewMessages();
        hasCheckedMessages = true;
    }
}

/**
 * @description: 表情关键词识别函数
 * @param {String} 输入文本
 * @return {String} 识别到的情绪
 */
String detectEmotion(const String &text)
{
    // 定义情绪关键词映射
    if (text.indexOf("快乐") >= 0 || text.indexOf("高兴") >= 0 || text.indexOf("开心") >= 0)
    {
        return "快乐";
    }
    else if (text.indexOf("伤心") >= 0 || text.indexOf("难过") >= 0 || text.indexOf("悲伤") >= 0)
    {
        return "伤心";
    }
    else if (text.indexOf("愤怒") >= 0 || text.indexOf("生气") >= 0 || text.indexOf("恼火") >= 0)
    {
        return "愤怒";
    }
    else if (text.indexOf("意外") >= 0 || text.indexOf("惊讶") >= 0 || text.indexOf("惊喜") >= 0)
    {
        return "意外";
    }
    else if (text.indexOf("专注") >= 0 || text.indexOf("认真") >= 0)
    {
        return "专注";
    }
    else if (text.indexOf("愁") >= 0 || text.indexOf("担心") >= 0 || text.indexOf("忧虑") >= 0)
    {
        return "发愁";
    }
    else if (text.indexOf("懊恼") >= 0 || text.indexOf("苦恼") >= 0 || text.indexOf("烦") >= 0)
    {
        return "懊恼";
    }
    else if (text.indexOf("困倦") >= 0 || text.indexOf("睡觉") >= 0 || text.indexOf("疲惫") >= 0 || text.indexOf("困") >= 0)
    {
        return "困倦";
    }
    else if (text.indexOf("疑问") >= 0 || text.indexOf("疑惑") >= 0 || text.indexOf("困惑") >= 0 || text.indexOf("为什么") >= 0)
    {
        return "疑问";
    }
    else if (text.indexOf("恐惧") >= 0 || text.indexOf("害怕") >= 0 || text.indexOf("惊恐") >= 0)
    {
        return "恐惧";
    }
    else if (text.indexOf("敬畏") >= 0 || text.indexOf("崇拜") >= 0)
    {
        return "敬畏";
    }
    else if (text.indexOf("说话") >= 0 || text.indexOf("交谈") >= 0 || text.indexOf("聊天") >= 0)
    {
        return "说话中";
    }
    else if (text.indexOf("聆听") >= 0 || text.indexOf("倾听") >= 0)
    {
        return "聆听中";
    }
    else if (text.indexOf("无情绪") >= 0 || text.indexOf("默认") >= 0 || text.indexOf("正常") >= 0)
    {
        return "无情绪";
    }

    // 如果没有匹配到情绪关键词，返回无情绪
    return "无情绪";
}

/**
 * @description: 检查文本是否包含动作指令
 * @param {String} text - 输入文本
 * @return {bool} 是否包含动作指令
 */
bool containsActionCommand(const String &text)
{
    return (text.indexOf("前进") >= 0 || text.indexOf("向前") >= 0 || text.indexOf("走") >= 0 || text.indexOf("向前走") >= 0 ||
            text.indexOf("后退") >= 0 || text.indexOf("向后") >= 0 || text.indexOf("退") >= 0 || text.indexOf("向后走") >= 0 ||
            text.indexOf("左转") >= 0 || text.indexOf("向左") >= 0 || text.indexOf("向左转") >= 0 ||
            text.indexOf("右转") >= 0 || text.indexOf("向右") >= 0 || text.indexOf("向右转") >= 0 ||
            text.indexOf("右手") >= 0 || text.indexOf("举右手") >= 0 || text.indexOf("抬右手") >= 0 ||
            text.indexOf("左手") >= 0 || text.indexOf("举左手") >= 0 || text.indexOf("抬左手") >= 0 ||
            text.indexOf("坐下") >= 0 || text.indexOf("蹲下") >= 0 || text.indexOf("坐") >= 0 ||
            text.indexOf("躺下") >= 0 || text.indexOf("趴下") >= 0 || text.indexOf("躺") >= 0 ||
            text.indexOf("睡觉") >= 0 || text.indexOf("休息") >= 0 || text.indexOf("睡") >= 0 ||
            text.indexOf("跳舞") >= 0 || text.indexOf("舞") >= 0 || text.indexOf("摇摆") >= 0 ||
            text.indexOf("停止") >= 0 || text.indexOf("停下") >= 0 || text.indexOf("停") >= 0 ||
            text.indexOf("站起来") >= 0 || text.indexOf("起立") >= 0 || text.indexOf("站") >= 0);
}

/**
 * @description: 获取动作指令的描述
 * @param {String} text - 输入文本
 * @return {String} 动作指令描述
 */
String getActionDescription(const String &text)
{
    if (text.indexOf("前进") >= 0 || text.indexOf("向前") >= 0 || text.indexOf("走") >= 0 || text.indexOf("向前走") >= 0)
        return "前进";
    else if (text.indexOf("后退") >= 0 || text.indexOf("向后") >= 0 || text.indexOf("退") >= 0 || text.indexOf("向后走") >= 0)
        return "后退";
    else if (text.indexOf("左转") >= 0 || text.indexOf("向左") >= 0 || text.indexOf("向左转") >= 0)
        return "左转";
    else if (text.indexOf("右转") >= 0 || text.indexOf("向右") >= 0 || text.indexOf("向右转") >= 0)
        return "右转";
    else if (text.indexOf("右手") >= 0 || text.indexOf("举右手") >= 0 || text.indexOf("抬右手") >= 0)
        return "举右手";
    else if (text.indexOf("左手") >= 0 || text.indexOf("举左手") >= 0 || text.indexOf("抬左手") >= 0)
        return "举左手";
    else if (text.indexOf("坐下") >= 0 || text.indexOf("蹲下") >= 0 || text.indexOf("坐") >= 0)
        return "坐下";
    else if (text.indexOf("躺下") >= 0 || text.indexOf("趴下") >= 0 || text.indexOf("躺") >= 0)
        return "躺下";
    else if (text.indexOf("睡觉") >= 0 || text.indexOf("休息") >= 0 || text.indexOf("睡") >= 0 || text.indexOf("sleep") >= 0)
        return "睡觉";
    else if (text.indexOf("跳舞") >= 0 || text.indexOf("舞") >= 0 || text.indexOf("摇摆") >= 0 || text.indexOf("dance") >= 0)
        return "跳舞";
    else if (text.indexOf("停止") >= 0 || text.indexOf("停下") >= 0 || text.indexOf("停") >= 0 || text.indexOf("stand") >= 0)
        return "停止";
    else if (text.indexOf("打招呼") >= 0 || text.indexOf("你好") >= 0 || text.indexOf("hi") >= 0 || text.indexOf("hello") >= 0)
        return "打招呼";
    else if (text.indexOf("趴下") >= 0 || text.indexOf("趴") >= 0 || text.indexOf("down") >= 0)
        return "趴下";
    else if (text.indexOf("站起来") >= 0 || text.indexOf("起立") >= 0 || text.indexOf("站") >= 0)
        return "站起来";
    else
        return "未知动作";
}

/**
 * @description: 处理reminder消息
 * @param {String} reminderText - 提醒文本
 * @return {void}
 */
void processReminder(const String &reminderText)
{
    Serial.println("[Reminder] 收到提醒消息: " + reminderText);

    // 设置提醒内容
    currentReminder = reminderText;
    reminderRepeatCount = 0;
    lastReminderTime = millis();

    // 立即播放第一次
    esp_ai.tts(reminderText);
    reminderRepeatCount++;

    // 更新表情显示
    if (face)
    {
        face->ShowNotification("收到提醒: " + reminderText);
        face->SetEmotion("专注");
    }

    Serial.println("[Reminder] 开始播放提醒，剩余次数: " + String(REMINDER_MAX_REPEAT - reminderRepeatCount));
}

/**
 * @description: 更新reminder状态
 * @return {void}
 */
void updateReminder()
{
    // 如果没有提醒内容或已经播放完毕，直接返回
    if (currentReminder.length() == 0 || reminderRepeatCount >= REMINDER_MAX_REPEAT)
    {
        return;
    }

    unsigned long currentTime = millis();

    // 检查是否到了播放下一遍的时间
    if (currentTime - lastReminderTime >= REMINDER_INTERVAL)
    {
        // 播放下一遍
        esp_ai.tts(currentReminder);
        reminderRepeatCount++;
        lastReminderTime = currentTime;

        Serial.println("[Reminder] 播放第" + String(reminderRepeatCount) + "遍，剩余次数: " + String(REMINDER_MAX_REPEAT - reminderRepeatCount));

        // 如果播放完毕，清空提醒内容
        if (reminderRepeatCount >= REMINDER_MAX_REPEAT)
        {
            Serial.println("[Reminder] 提醒播放完毕");
            currentReminder = "";
            reminderRepeatCount = 0;

            // 恢复表情
            if (face)
            {
                face->SetEmotion("无情绪");
            }
        }
    }
}

/**
 * @description: 动作关键词识别函数
 * @param {String} 输入文本
 * @param {bool} isRemote - 是否为远程指令（默认为false）
 * @return {void}
 */
void detectAction(const String &text, bool isRemote = false)
{
    // 获取互斥锁，保护舵机操作
    if (xSemaphoreTake(servoMutex, pdMS_TO_TICKS(100)) != pdTRUE)
    {
        Serial.println("获取舵机互斥锁失败，动作无法执行");
        return;
    }

    // 如果正在执行动作，先停止
    if (isActionRunning)
    {
        stopAction();
    }

    // 匹配前进相关关键词
    if (text.indexOf("前进") >= 0 || text.indexOf("向前") >= 0 || text.indexOf("走") >= 0 || text.indexOf("向前走") >= 0)
    {
        startAction(ACTION_FRONT);
    }
    // 匹配后退相关关键词
    else if (text.indexOf("后退") >= 0 || text.indexOf("向后") >= 0 || text.indexOf("退") >= 0 || text.indexOf("向后走") >= 0)
    {
        startAction(ACTION_BACK);
    }
    // 匹配左转相关关键词
    else if (text.indexOf("左转") >= 0 || text.indexOf("向左") >= 0 || text.indexOf("向左转") >= 0)
    {
        startAction(ACTION_LEFT);
    }
    // 匹配右转相关关键词
    else if (text.indexOf("右转") >= 0 || text.indexOf("向右") >= 0 || text.indexOf("向右转") >= 0)
    {
        startAction(ACTION_RIGHT);
    }
    // 匹配右手相关关键词
    else if (text.indexOf("右手") >= 0 || text.indexOf("举右手") >= 0 || text.indexOf("抬右手") >= 0)
    {
        startAction(ACTION_TOPLEFT);
    }
    // 匹配左手相关关键词
    else if (text.indexOf("左手") >= 0 || text.indexOf("举左手") >= 0 || text.indexOf("抬左手") >= 0)
    {
        startAction(ACTION_TOPRIGHT);
    }
    // 匹配坐下相关关键词
    else if (text.indexOf("坐下") >= 0 || text.indexOf("蹲下") >= 0 || text.indexOf("坐") >= 0)
    {
        startAction(ACTION_SITDOWN);
    }
    // 匹配躺下相关关键词
    else if (text.indexOf("躺下") >= 0 || text.indexOf("趴下") >= 0 || text.indexOf("躺") >= 0)
    {
        startAction(ACTION_LIE);
    }
    // 匹配睡觉相关关键词
    else if (text.indexOf("睡觉") >= 0 || text.indexOf("休息") >= 0 || text.indexOf("睡") >= 0)
    {
        startAction(ACTION_SLEEP);
    }
    // 匹配摇摆相关关键词
    else if (text.indexOf("跳舞") >= 0 || text.indexOf("舞") >= 0 || text.indexOf("摇摆") >= 0 || text.indexOf("dance") >= 0)
    {
        startAction(ACTION_SWING);
    }
    // 匹配停止相关关键词
    else if (text.indexOf("停止") >= 0 || text.indexOf("停下") >= 0 || text.indexOf("停") >= 0 || text.indexOf("stand") >= 0)
    {
        stopAction();
    }
    // 匹配睡觉相关关键词
    else if (text.indexOf("睡觉") >= 0 || text.indexOf("休息") >= 0 || text.indexOf("睡") >= 0 || text.indexOf("sleep") >= 0)
    {
        startAction(ACTION_SLEEP);
    }
    // 匹配打招呼相关关键词
    else if (text.indexOf("打招呼") >= 0 || text.indexOf("你好") >= 0 || text.indexOf("hi") >= 0 || text.indexOf("hello") >= 0)
    {
        startAction(ACTION_TOPLEFT); // 使用举手动作作为打招呼
    }
    // 匹配趴下相关关键词
    else if (text.indexOf("趴下") >= 0 || text.indexOf("趴") >= 0 || text.indexOf("down") >= 0)
    {
        startAction(ACTION_LIE); // 使用躺下动作作为趴下
    }
    // 匹配跳舞相关关键词
    else if (text.indexOf("跳舞") >= 0 || text.indexOf("舞") >= 0 || text.indexOf("摇摆") >= 0 || text.indexOf("dance") >= 0)
    {
        startAction(ACTION_SWING);
    }
    // 匹配站起来相关关键词
    else if (text.indexOf("站起来") >= 0 || text.indexOf("起立") >= 0 || text.indexOf("站") >= 0)
    {
        // 站起来动作可以复用坐下动作的反向执行
        startAction(ACTION_SITDOWN);
    }

    // 释放互斥锁
    xSemaphoreGive(servoMutex);
}

/**
 * @description: 语音识别结果回调
 * @param {String} command_id 命令ID
 * @param {String} data 识别结果数据
 * @return {void}
 */
void onAiEvent(String command_id, String data)
{
    if (command_id == "on_iat_cb")
    {
        lastAsrText = data;
        Serial.print("[" + String(DEVICE_NAME) + "-语音识别结果：] ");
        Serial.println(lastAsrText);

        // 如果在留言模式中，只做提示，不保存留言也不退出留言模式
        if (isInMessageMode)
        {
            if (lastAsrText.length() > 0)
            {
                Serial.println("[留言模式] " + String(DEVICE_NAME) + " 收到本地文本：" + lastAsrText);
                if (face)
                {
                    face->ShowNotification("正在识别...");
                    face->SetEmotion("专注");
                }
            }
            return; // 留言模式下不处理其他功能
        }

        // 如果等待用户对留言模式的响应
        if (waitingForMessageResponse)
        {
            processMessageResponse(lastAsrText);
            return; // 响应处理模式下不处理其他功能
        }

        // 本地聊天时，不根据用户输入更新表情，等待AI回复的情绪
        // 只有在异地交互时才根据用户输入更新表情
        if (isPaired)
        {
            // 检测是否包含情绪关键词
            String emotion = detectEmotion(lastAsrText);
            if (emotion != "")
            {
                // 异地交互时，根据对方说话更新表情
                if (face)
                {
                    face->SetEmotion(emotion);
                    face->ShowNotification("对方表情: " + emotion);
                    face->SetChatMessage(lastAsrText);
                }

                // 发送情绪指令到对方设备
                String jsonPayload = "{\"emotion\":\"" + emotion + "\"}";
                mqtt_send(jsonPayload);
            }
        }
        else
        {
            // 本地聊天时，只设置聊天消息，不更新表情
            if (face)
            {
                face->SetChatMessage(lastAsrText);
            }
        }

        // 检测并执行动作指令（不影响情感识别）
        detectAction(lastAsrText);

        // 注意：动作指令的远程发送已在讯飞语音识别模块中处理
        // 这里不再重复发送，避免重复发送动作指令
    }
    else if (command_id == "on_llm_cb")
    {
        // AI回复内容
        Serial.print("[" + String(DEVICE_NAME) + "-AI回复：] ");
        Serial.println(data);

        // 本地聊天时，根据AI回复内容更新表情
        if (!isPaired && face)
        {
            String emotion = detectEmotion(data);
            if (emotion != "")
            {
                face->SetEmotion(emotion);
                face->ShowNotification(emotion);
            }
            face->SetChatMessage(data);
        }
    }
    else if (command_id == "emotion")
    {
        // 收到情绪指令（来自异地交互）
        Serial.print("[" + String(DEVICE_NAME) + "-收到情绪：] ");
        Serial.println(data);

        if (face)
        {
            face->SetEmotion(data);
            face->ShowNotification("对方情绪: " + data);
        }
    }
}

/**
 * @description: 按钮回调函数集合
 */
// 单击回调：开始/停止录音
void onSingleClick()
{
    // 获取全局互斥锁
    if (xSemaphoreTake(globalMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        // 本地交互时禁用讯飞语音识别，但留言模式除外
        if (!isPaired && !waitingForMessageResponse && !isInMessageMode)
        {
            Serial.println("[STT] 本地交互模式，讯飞语音识别已禁用");
            if (face)
            {
                face->SetEmotion("无情绪");
            }
            xSemaphoreGive(globalMutex);
            return;
        }

        if (!isRecording)
        {
            // 开始录音
            Serial.println("[STT] 开始录音...");
            if (face)
            {
                face->ShowNotification("开始录音...");
                face->SetEmotion("聆听中");
            }
            startXFSTT_record_begin();
            isRecording = true;
        }
        else
        {
            // 结束录音
            Serial.println("[STT] 结束录音并识别...");
            if (face)
            {
                face->ShowNotification("识别中...");
            }
            startXFSTT_record_end();
            isRecording = false;
        }

        // 释放全局互斥锁
        xSemaphoreGive(globalMutex);
    }
}

// 双击回调：取消当前录音
void onDoubleClick()
{
    // 获取全局互斥锁
    if (xSemaphoreTake(globalMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        // 本地交互时禁用讯飞语音识别相关功能，但留言模式除外
        if (!isPaired && !waitingForMessageResponse && !isInMessageMode)
        {
            Serial.println("[STT] 本地交互模式，语音识别功能已禁用");
            if (face)
            {
                face->SetEmotion("无情绪");
            }
            xSemaphoreGive(globalMutex);
            return;
        }

        if (isRecording)
        {
            // 取消录音
            Serial.println("[STT] 取消录音");
            if (face)
            {
                face->ShowNotification("已取消录音");
                face->SetEmotion("无情绪");
            }
            // 这里需要一个取消录音的函数，如果没有可以自行实现
            // cancelXFSTT_record();
            isRecording = false;
            xSemaphoreGive(globalMutex);
            return;
        }

        // 释放全局互斥锁
        xSemaphoreGive(globalMutex);
    }
    // 非录音状态下，双击可自定义其他功能
}

// 长按回调：配对逻辑
void onLongPressStart()
{
    // 获取全局互斥锁
    if (xSemaphoreTake(globalMutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        if (isRecording)
        {
            Serial.println("[按钮] 正在录音，忽略按钮事件");
            xSemaphoreGive(globalMutex);
            return;
        }

#if DEVICE_ROLE_A
        if (!isPaired)
        {
            if (waitingPairRequest)
            {
                // 已收到B的配对请求，A同意配对
                esp_ai.tts("配对成功");
                String jsonPayload = "{\"pair_accept\":true}";
                mqtt_send(jsonPayload);
                isPaired = true;
                waitingPairRequest = false;

                // 配对成功后提示可用的动作指令
                if (face)
                {
                    face->ShowNotification("配对成功！");
                }
            }
            else
            {
                esp_ai.tts("异地模式已开启，正在等待配对");
                String jsonPayload = "{\"pair\":true}";
                mqtt_send(jsonPayload);
                // 记录配对请求时间，用于超时检测
                pairRequestTime = millis();
            }
        }
        else
        {
            esp_ai.tts("异地模式已关闭");
            String jsonPayload = "{\"remote_off\":true}";
            mqtt_send(jsonPayload);
            isPaired = false;
            waitingPairRequest = false;
        }
#else
        if (!isPaired)
        {
            if (!waitingPairRequest)
            {
                esp_ai.tts("异地模式已开启，正在等待配对");
                String jsonPayload = "{\"pair\":true}";
                mqtt_send(jsonPayload);
                // 记录配对请求时间，用于超时检测
                pairRequestTime = millis();
            }
            else
            {
                esp_ai.tts("配对成功");
                String jsonPayload = "{\"pair_accept\":true}";
                mqtt_send(jsonPayload);
                isPaired = true;
                waitingPairRequest = false;

                // 配对成功后提示可用的动作指令
                if (face)
                {
                    face->ShowNotification("配对成功！");
                }
            }
        }
        else
        {
            esp_ai.tts("异地模式已关闭");
            String jsonPayload = "{\"remote_off\":true}";
            mqtt_send(jsonPayload);
            isPaired = false;
            waitingPairRequest = false;
        }
#endif

        // 释放全局互斥锁
        xSemaphoreGive(globalMutex);
    }
}

/**
 * @description: 检查是否有新留言
 * @return {void}
 */
void checkForNewMessages()
{
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("WiFi未连接，无法检查留言");
        return;
    }

    HTTPClient http;
    http.begin(PROJECT_MESSAGE_GET_URL);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK)
    {
        String payload = http.getString();
        Serial.println("最新留言：" + payload);

        DynamicJsonDocument doc(4096);
        DeserializationError error = deserializeJson(doc, payload);
        if (error)
        {
            Serial.println("JSON解析失败：" + String(error.c_str()));
            http.end();
            return;
        }

        const char *msgPerson = doc["person"] | "";
        int isRead = doc["isRead"] | -1;
        String currentDeviceName = String(DEVICE_NAME);

        // 检查是否有未读留言，且留言不是自己发送的
        if (isRead == 0 && String(msgPerson) != currentDeviceName)
        {
            String message = doc["message"] | "";
            String sender = String(msgPerson);

            Serial.println("发现新留言，来自：" + sender + "，内容：" + message);

            // 播报留言
            esp_ai.tts("收到来自" + sender + "的留言：" + message);

            // 更新表情
            if (face)
            {
                face->ShowNotification("收到新留言");
                face->SetEmotion("专注");
            }

            // 标记为已读
            HTTPClient updateHttp;
            updateHttp.begin(PROJECT_MESSAGE_MARK_READ_URL);
            updateHttp.addHeader("Content-Type", "application/x-www-form-urlencoded");
            String putData = "isRead=1";
            updateHttp.PUT(putData);
            updateHttp.end();
        }
        else if (String(msgPerson) == currentDeviceName)
        {
            Serial.println("跳过自己发送的留言");
        }
        else if (isRead == 1)
        {
            Serial.println("留言已读，跳过");
        }
    }
    else
    {
        Serial.print("获取留言失败：");
        Serial.println(http.errorToString(httpCode).c_str());
    }
    http.end();
}

/**
 * @description: 进入留言模式
 * @return {void}
 */
void enterMessageMode()
{
    isInMessageMode = true;
    waitingForMessageResponse = false;

    Serial.println("[留言模式] 进入留言模式");
    esp_ai.tts("已进入留言模式，请说出您的留言内容");

    if (face)
    {
        face->ShowNotification("留言模式");
        face->SetEmotion("聆听中");
    }
}

/**
 * @description: 退出留言模式
 * @return {void}
 */
void exitMessageMode()
{
    isInMessageMode = false;
    waitingForMessageResponse = false;

    Serial.println("[留言模式] 退出留言模式");
    esp_ai.tts("留言完成，留言模式已退出");

    if (face)
    {
        face->ShowNotification("留言完成");
        face->SetEmotion("无情绪");
    }

    // 退出异地模式
    isPaired = false;
    waitingPairRequest = false;
}

/**
 * @description: 处理用户对留言模式的响应
 * @param {String} response 用户响应
 * @return {void}
 */
void processMessageResponse(const String &response)
{
    waitingForMessageResponse = false;

    if (isPositiveResponse(response))
    {
        Serial.println("[留言模式] 用户选择进入留言模式");
        enterMessageMode();
    }
    else if (isNegativeResponse(response))
    {
        Serial.println("[留言模式] 用户选择不留言");
        esp_ai.tts("好的，退出异地模式");
        exitMessageMode();
    }
    else
    {
        // 未识别清楚，重新询问
        Serial.println("[留言模式] 未识别清楚，重新询问");
        esp_ai.tts("请回答是或否，是否要进入留言模式？");
        waitingForMessageResponse = true;
    }
}

/**
 * @description: 判断是否为肯定回答
 * @param {String} text 输入文本
 * @return {bool} 是否为肯定回答
 */
bool isPositiveResponse(const String &text)
{
    return (text.indexOf("是") >= 0 || text.indexOf("要") >= 0 ||
            text.indexOf("好") >= 0 || text.indexOf("可以") >= 0 ||
            text.indexOf("行") >= 0 || text.indexOf("同意") >= 0 ||
            text.indexOf("进入") >= 0 || text.indexOf("留言") >= 0);
}

/**
 * @description: 判断是否为否定回答
 * @param {String} text 输入文本
 * @return {bool} 是否为否定回答
 */
bool isNegativeResponse(const String &text)
{
    return (text.indexOf("否") >= 0 || text.indexOf("不") >= 0 ||
            text.indexOf("不要") >= 0 || text.indexOf("算了") >= 0 ||
            text.indexOf("退出") >= 0 || text.indexOf("取消") >= 0);
}

/**
 * @description: 主任务，处理ESP-AI核心功能
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void MainTask(void *pvParameters)
{
    while (systemRunning)
    {
        // 获取主任务互斥锁，增加等待时间
        if (xSemaphoreTake(mainMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // ESP-AI主循环
            esp_ai.loop();

            // 更新reminder状态
            updateReminder();

            // 释放互斥锁
            xSemaphoreGive(mainMutex);
        }
        else
        {
            // 如果无法获取锁，等待更长时间
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }

        // 任务延时
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/**
 * @description: MQTT任务，处理MQTT消息
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void MQTTTask(void *pvParameters)
{
    while (systemRunning)
    {
        // 获取MQTT互斥锁，增加等待时间
        if (xSemaphoreTake(mqttMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // MQTT消息处理
            mqtt_poll();

            // 释放互斥锁
            xSemaphoreGive(mqttMutex);
        }
        else
        {
            // 如果无法获取锁，等待更长时间
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }

        // 任务延时
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/**
 * @description: 按钮任务，处理按钮状态更新
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void ButtonTask(void *pvParameters)
{
    while (systemRunning)
    {
        // 按钮状态更新
        button.tick();

        // 任务延时
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/**
 * @description: 串口任务，处理串口数据
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */

/**
 * @description: 留言功能任务，处理留言相关逻辑
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void MessageTask(void *pvParameters)
{
    while (systemRunning)
    {
        // 获取留言互斥锁，增加等待时间
        if (xSemaphoreTake(messageMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // 获取全局互斥锁保护共享变量
            if (xSemaphoreTake(globalMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                // 留言功能超时检测
                if (pairRequestTime > 0 && !isPaired && !waitingForMessageResponse && !isInMessageMode)
                {
                    unsigned long currentTime = millis();
                    if (currentTime - pairRequestTime > MESSAGE_TIMEOUT)
                    {
                        Serial.println("[留言功能] 对方配对超时，询问是否进入留言模式");
                        esp_ai.tts("对方配对超时，请回答是否要进入留言模式？");
                        waitingForMessageResponse = true;
                        pairRequestTime = 0; // 重置时间戳
                    }
                }

                // 释放全局互斥锁
                xSemaphoreGive(globalMutex);
            }

            // 释放留言互斥锁
            xSemaphoreGive(messageMutex);
        }
        else
        {
            // 如果无法获取锁，等待更长时间
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }

        // 任务延时
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/**
 * @description: 语音识别任务，处理讯飞语音识别
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void STTTask(void *pvParameters)
{
    while (systemRunning)
    {
        // 获取语音识别互斥锁，增加等待时间
        if (xSemaphoreTake(sttMutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            // 获取全局互斥锁保护共享变量
            if (xSemaphoreTake(globalMutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                // 处理语音识别循环 - 在配对模式或留言模式下启用
                if (isPaired || waitingForMessageResponse || isInMessageMode)
                {
                    startXFSTT_record_loop();
                }

                // 释放全局互斥锁
                xSemaphoreGive(globalMutex);
            }

            // 释放语音识别互斥锁
            xSemaphoreGive(sttMutex);
        }
        else
        {
            // 如果无法获取锁，等待更长时间
            vTaskDelay(10 / portTICK_PERIOD_MS);
        }

        // 任务延时
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

/**
 * @description: 系统初始化
 * @return {void}
 */
void setup()
{
    Serial.begin(115200);

    // 初始化看门狗
    esp_task_wdt_init(30, true); // 30秒超时，启用panic处理
    esp_task_wdt_add(NULL);      // 添加当前任务到看门狗

    // 创建FreeRTOS资源
    servoMutex = xSemaphoreCreateMutex();
    if (servoMutex == NULL)
    {
        Serial.println("错误: 创建舵机互斥锁失败");
    }

    // 创建其他互斥锁
    mainMutex = xSemaphoreCreateMutex();
    mqttMutex = xSemaphoreCreateMutex();
    messageMutex = xSemaphoreCreateMutex();
    sttMutex = xSemaphoreCreateMutex();
    globalMutex = xSemaphoreCreateMutex();

    if (mainMutex == NULL || mqttMutex == NULL ||
        messageMutex == NULL || sttMutex == NULL || globalMutex == NULL)
    {
        Serial.println("错误: 创建互斥锁失败");
        Serial.println("系统将在5秒后重启...");
        delay(5000);
        ESP.restart();
    }
    else
    {
        Serial.println("所有互斥锁创建成功");
    }

    // 初始化舵机控制
    setupServos(robot_servo_config);
    Serial.println("舵机系统已初始化");

    // [必填] 是否调试模式，会输出更多信息
    bool debug = true;

    // wifi 配置：{ wifi 账号，wifi 密码 }  注意：要用双引号！
    ESP_AI_wifi_config wifi_config = {"", "", DEVICE_NAME};

    // 服务地址，用开发者平台，只需要配置为空，需要在配网页面配置。
    ESP_AI_server_config server_config = {};

    // 离线唤醒方案：{ 方案, 识别阈值 }, "edge_impulse" | "diy"，为 "diy" 时可调用 esp_ai.wakeUp() 方法进行唤醒
    ESP_AI_wake_up_config wake_up_config = {};
    strcpy(wake_up_config.wake_up_scheme, "asrpro"); // 唤醒方案
    strcpy(wake_up_config.str, "start");             // 串口和天问asrpro 唤醒时需要配置的字符串，也就是从另一个开发版发送来的字符串

    // 开始运行 ESP-AI
    esp_ai.begin({debug, wifi_config, server_config, wake_up_config});
    esp_ai.setVolume(10);           // 设置音量，范围 0-10
    esp_ai.onEvent(onAiEvent);      // 注册语音识别回调
    esp_ai.onReady(onServiceReady); // 注册服务连接成功回调

    // 设备A/B参数配置
#if DEVICE_ROLE_A
    mqtt_set_params(String(DEVICE_NAME) + "_id", PROJECT_MQTT_USER_A, PROJECT_MQTT_PASS_A, PROJECT_MQTT_IN_TOPIC_A, PROJECT_MQTT_OUT_TOPIC_A);
#else
    mqtt_set_params(String(DEVICE_NAME) + "_id", PROJECT_MQTT_USER_B, PROJECT_MQTT_PASS_B, PROJECT_MQTT_IN_TOPIC_B, PROJECT_MQTT_OUT_TOPIC_B);
#endif

    // 等待WiFi连接成功
    Serial.println("正在连接WiFi...");
    unsigned long wifiStartTime = millis();
    while (!esp_ai.wifiIsConnected() && (millis() - wifiStartTime < 30000))
    {
        delay(100);
    }

    if (!esp_ai.wifiIsConnected())
    {
        Serial.println("WiFi连接失败，尝试继续运行...");
    }
    else
    {
        Serial.println("WiFi已连接,初始化MQTT...");
        mqtt_init();

        // 尝试连接MQTT，最多重试3次
        int mqttRetryCount = 0;
        while (!mqtt_connect() && mqttRetryCount < 3)
        {
            Serial.println("MQTT连接失败,重试中...");
            mqttRetryCount++;
            delay(2000);
        }

        if (mqttRetryCount < 3)
        {
            Serial.println("MQTT已连接");

            // 初始化NTP时间同步
            setup_ntp_client();
            Serial.println("NTP时间同步初始化完成");
        }
        else
        {
            Serial.println("MQTT连接失败，将在没有远程功能的情况下继续运行");
        }
    }

    // 按钮初始化
    button.attachClick(onSingleClick);             // 单击事件 - 开始/停止录音
    button.attachDoubleClick(onDoubleClick);       // 双击事件 - 取消录音
    button.attachLongPressStart(onLongPressStart); // 长按事件 - 配对

    // ========== OLED表情屏初始化 ==========
    face = new Face(); // 使用默认参数，适配OLED
    if (face)
    {
        face->SetEmotion("无情绪");
        face->ShowNotification("云萌宝伴-" + String(DEVICE_NAME));
    }
    else
    {
        Serial.println("错误: Face对象创建失败");
    }

    // 创建FreeRTOS任务
    xTaskCreatePinnedToCore(
        MainTask,        // 任务函数
        "MainTask",      // 任务名称
        8192,            // 任务堆栈大小
        NULL,            // 传递给任务的参数
        3,               // 任务优先级 (1-24，数字越大优先级越高)
        &mainTaskHandle, // 任务句柄
        0                // 运行任务的核心
    );

    xTaskCreatePinnedToCore(
        MQTTTask,        // 任务函数
        "MQTTTask",      // 任务名称
        4096,            // 任务堆栈大小
        NULL,            // 传递给任务的参数
        2,               // 任务优先级
        &mqttTaskHandle, // 任务句柄
        0                // 运行任务的核心
    );

    xTaskCreatePinnedToCore(
        ButtonTask,        // 任务函数
        "ButtonTask",      // 任务名称
        4096,              // 任务堆栈大小
        NULL,              // 传递给任务的参数
        2,                 // 任务优先级
        &buttonTaskHandle, // 任务句柄
        0                  // 运行任务的核心
    );

    xTaskCreatePinnedToCore(
        MessageTask,        // 任务函数
        "MessageTask",      // 任务名称
        4096,               // 任务堆栈大小
        NULL,               // 传递给任务的参数
        1,                  // 任务优先级
        &messageTaskHandle, // 任务句柄
        0                   // 运行任务的核心
    );

    xTaskCreatePinnedToCore(
        STTTask,        // 任务函数
        "STTTask",      // 任务名称
        4096,           // 任务堆栈大小
        NULL,           // 传递给任务的参数
        1,              // 任务优先级
        &sttTaskHandle, // 任务句柄
        0               // 运行任务的核心
    );

    xTaskCreatePinnedToCore(
        ServoTask,        // 任务函数
        "ServoTask",      // 任务名称
        4096,             // 任务堆栈大小
        NULL,             // 传递给任务的参数
        2,                // 任务优先级
        &servoTaskHandle, // 任务句柄
        1                 // 运行任务的核心
    );

    xTaskCreatePinnedToCore(
        FaceTask,        // 任务函数
        "FaceTask",      // 任务名称
        4096,            // 任务堆栈大小
        NULL,            // 传递给任务的参数
        1,               // 任务优先级
        &faceTaskHandle, // 任务句柄
        1                // 运行任务的核心
    );

    // 初始化留言功能相关变量
    isInMessageMode = false;
    waitingForMessageResponse = false;
    pairRequestTime = 0;
    hasCheckedMessagesOnBoot = false;

    Serial.println(String(DEVICE_NAME) + " 初始化完成，系统已启动");
}

/**
 * @description: 主循环
 * @return {void}
 */
void loop()
{
    // 喂看门狗
    esp_task_wdt_reset();

    // 主循环现在只负责基本的延时
    // 所有功能都已移至独立的任务中
    vTaskDelay(100 / portTICK_PERIOD_MS);
}

/**
 * @description: 舵机控制任务，独立线程运行
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void ServoTask(void *pvParameters)
{
    while (1)
    {
        // 获取互斥锁，保护舵机操作
        if (xSemaphoreTake(servoMutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            // 更新舵机状态（确保动作平滑执行）
            updateServos();
            // 释放互斥锁
            xSemaphoreGive(servoMutex);
        }

        // 为FreeRTOS任务添加适当的延时，避免占用过多CPU资源
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

/**
 * @description: 表情显示任务，独立线程运行
 * @param {void*} pvParameters - FreeRTOS任务参数
 * @return {void}
 */
void FaceTask(void *pvParameters)
{
    while (1)
    {
        // 刷新OLED表情屏
        if (face)
            face->Update();

        // 为FreeRTOS任务添加适当的延时
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
