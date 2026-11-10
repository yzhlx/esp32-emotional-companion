#include "ServoControl.h"
#include <Arduino.h>

// 舵机对象
Servo servo1; // 左前腿舵机
Servo servo2; // 左后腿舵机
Servo servo3; // 右前腿舵机
Servo servo4; // 右后腿舵机

// 动作控制内部变量
static unsigned long previousMillis = 0; // 上次动作时间
static const long actionInterval = 100;  // 动作间隔时间(ms)
static int currentAction = ACTION_IDLE;  // 当前动作状态
static int actionStep = 0;               // 当前动作步骤
static int actionRepeat = 0;             // 动作重复次数
bool isActionRunning = false;            // 是否有动作正在执行（全局可访问）

// 舵机配置
ServoConfig servo_config;

/**
 * @description: 设置舵机到指定角度，带边界检查
 * @param {Servo&} servo - 舵机对象
 * @param {int} angle - 角度值（0-180）
 * @return {void}
 */
static void safeServoWrite(Servo &servo, int angle)
{
    // 边界检查
    if (angle < 0)
        angle = 0;
    if (angle > 180)
        angle = 180;
    servo.write(angle);
}

/**
 * @description: 初始化舵机
 * @param {ServoConfig} config - 舵机配置参数
 * @return {void}
 */
void setupServos(const ServoConfig &config)
{
    // 参数校验
    if (config.fl_pin < 0 || config.fl_pin > 45 || config.bl_pin < 0 || config.bl_pin > 45 ||
        config.fr_pin < 0 || config.fr_pin > 45 || config.br_pin < 0 || config.br_pin > 45)
    {
        Serial.println("错误: 无效的舵机引脚!"); // 检查舵机引脚是否在有效范围内
        return;
    }
    if (config.min_pulse < 500 || config.max_pulse > 2500 || config.min_pulse >= config.max_pulse)
    {
        Serial.println("错误: 无效的脉冲范围!"); // 检查脉冲范围是否有效
        return;
    }
    // 保存参数
    servo_config = config;

    // 舵机初始化
    bool success = true;
    success &= servo1.attach(config.fl_pin, config.min_pulse, config.max_pulse);
    success &= servo2.attach(config.bl_pin, config.min_pulse, config.max_pulse);
    success &= servo3.attach(config.fr_pin, config.min_pulse, config.max_pulse);
    success &= servo4.attach(config.br_pin, config.min_pulse, config.max_pulse);

    if (!success)
    {
        Serial.println("警告: 部分舵机初始化失败！");
    }

    // 舵机归中
    safeServoWrite(servo1, 90);
    safeServoWrite(servo2, 90);
    safeServoWrite(servo3, 90);
    safeServoWrite(servo4, 90);
}

/**
 * @description: 开始执行指定动作
 * @param {int} action - 动作编号
 * @return {void}
 */
void startAction(int action)
{
    // 检查动作参数是否合法
    if (action < ACTION_IDLE || action > ACTION_SWING)
    {
        Serial.println("错误: 无效的动作ID!");
        return;
    }

    currentAction = action;
    actionStep = 0;
    actionRepeat = 0;
    isActionRunning = true;
    previousMillis = millis();

    Serial.print("开始执行动作: ");
    Serial.println(action);
}

/**
 * @description: 停止当前动作
 * @return {void}
 */
void stopAction()
{
    isActionRunning = false;
    currentAction = ACTION_IDLE;
    // 恢复初始位置
    safeServoWrite(servo1, 90);
    safeServoWrite(servo2, 90);
    safeServoWrite(servo3, 90);
    safeServoWrite(servo4, 90);
}

/**
 * @description: 更新舵机状态，由ServoTask调用
 * @return {void}
 */
void updateServos()
{
    if (!isActionRunning)
        return;

    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis < actionInterval)
        return;

    previousMillis = currentMillis;

    switch (currentAction)
    {
    case ACTION_FRONT:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo3, 140);
            safeServoWrite(servo2, 40);
            break;
        case 1:
            safeServoWrite(servo4, 40);
            safeServoWrite(servo1, 140);
            break;
        case 2:
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            break;
        case 3:
            safeServoWrite(servo4, 90);
            safeServoWrite(servo1, 90);
            break;
        case 4:
            safeServoWrite(servo4, 140);
            safeServoWrite(servo1, 40);
            break;
        case 5:
            safeServoWrite(servo3, 40);
            safeServoWrite(servo2, 140);
            break;
        case 6:
            safeServoWrite(servo4, 90);
            safeServoWrite(servo1, 90);
            break;
        case 7:
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            actionRepeat++;
            if (actionRepeat >= 5)
            {
                stopAction();
                return;
            }
            actionStep = -1; // 下一步会+1，变为0，重新开始
            break;
        }
        actionStep++;
        break;

    case ACTION_BACK:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo2, 140);
            safeServoWrite(servo3, 40);
            break;
        case 1:
            safeServoWrite(servo1, 40);
            safeServoWrite(servo4, 140);
            break;
        case 2:
            safeServoWrite(servo2, 90);
            safeServoWrite(servo3, 90);
            break;
        case 3:
            safeServoWrite(servo1, 90);
            safeServoWrite(servo4, 90);
            break;
        case 4:
            safeServoWrite(servo1, 140);
            safeServoWrite(servo4, 40);
            break;
        case 5:
            safeServoWrite(servo2, 40);
            safeServoWrite(servo3, 140);
            break;
        case 6:
            safeServoWrite(servo1, 90);
            safeServoWrite(servo4, 90);
            break;
        case 7:
            safeServoWrite(servo2, 90);
            safeServoWrite(servo3, 90);
            actionRepeat++;
            if (actionRepeat >= 5)
            {
                stopAction();
                return;
            }
            actionStep = -1;
            break;
        }
        actionStep++;
        break;

    case ACTION_LEFT:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo1, 80);
            safeServoWrite(servo4, 80);
            break;
        case 1:
            safeServoWrite(servo3, 120);
            safeServoWrite(servo2, 120);
            break;
        case 2:
            safeServoWrite(servo1, 40);
            safeServoWrite(servo4, 40);
            break;
        case 3:
            safeServoWrite(servo3, 140);
            safeServoWrite(servo2, 140);
            break;
        case 4:
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            safeServoWrite(servo1, 90);
            safeServoWrite(servo4, 90);
            break;
        case 5:
            safeServoWrite(servo1, 100);
            safeServoWrite(servo4, 100);
            break;
        case 6:
            safeServoWrite(servo3, 60);
            safeServoWrite(servo2, 60);
            break;
        case 7:
            safeServoWrite(servo1, 90);
            safeServoWrite(servo4, 90);
            break;
        case 8:
            safeServoWrite(servo3, 40);
            safeServoWrite(servo2, 40);
            break;
        case 9:
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            actionRepeat++;
            if (actionRepeat >= 3)
            {
                stopAction();
                return;
            }
            actionStep = -1;
            break;
        }
        actionStep++;
        break;

    case ACTION_RIGHT:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo1, 100);
            safeServoWrite(servo4, 100);
            break;
        case 1:
            safeServoWrite(servo3, 60);
            safeServoWrite(servo2, 60);
            break;
        case 2:
            safeServoWrite(servo1, 140);
            safeServoWrite(servo4, 140);
            break;
        case 3:
            safeServoWrite(servo3, 40);
            safeServoWrite(servo2, 40);
            break;
        case 4:
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            safeServoWrite(servo1, 90);
            safeServoWrite(servo4, 90);
            break;
        case 5:
            safeServoWrite(servo1, 80);
            safeServoWrite(servo4, 80);
            break;
        case 6:
            safeServoWrite(servo3, 120);
            safeServoWrite(servo2, 120);
            break;
        case 7:
            safeServoWrite(servo1, 90);
            safeServoWrite(servo4, 90);
            break;
        case 8:
            safeServoWrite(servo3, 140);
            safeServoWrite(servo2, 140);
            break;
        case 9:
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            actionRepeat++;
            if (actionRepeat >= 3)
            {
                stopAction();
                return;
            }
            actionStep = -1;
            break;
        }
        actionStep++;
        break;

    case ACTION_SITDOWN:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo2, 150);
            safeServoWrite(servo4, 30);
            break;
        case 1:
            vTaskDelay(pdMS_TO_TICKS(1500)); // 使用FreeRTOS延时代替delay
            break;
        case 2:
            safeServoWrite(servo2, 90);
            safeServoWrite(servo4, 90);
            stopAction();
            return;
        }
        actionStep++;
        break;

    case ACTION_LIE:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo1, 10);
            safeServoWrite(servo3, 170);
            safeServoWrite(servo2, 170);
            safeServoWrite(servo4, 10);
            break;
        case 1:
            vTaskDelay(pdMS_TO_TICKS(1500)); // 使用FreeRTOS延时代替delay
            break;
        case 2:
            safeServoWrite(servo1, 90);
            safeServoWrite(servo3, 90);
            safeServoWrite(servo2, 90);
            safeServoWrite(servo4, 90);
            stopAction();
            return;
        }
        actionStep++;
        break;

    case ACTION_TOPLEFT:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo3, 180);
            break;
        case 1:
            safeServoWrite(servo3, 150);
            break;
        case 2:
            actionRepeat++;
            if (actionRepeat >= 3)
            {
                safeServoWrite(servo3, 90);
                stopAction();
                return;
            }
            actionStep = -1;
            break;
        }
        actionStep++;
        break;

    case ACTION_TOPRIGHT:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo1, 0);
            break;
        case 1:
            safeServoWrite(servo1, 30);
            break;
        case 2:
            actionRepeat++;
            if (actionRepeat >= 3)
            {
                safeServoWrite(servo1, 90);
                stopAction();
                return;
            }
            actionStep = -1;
            break;
        }
        actionStep++;
        break;

    case ACTION_SLEEP:
        // 直接设置睡眠姿势，不通过动作步骤控制
        safeServoWrite(servo1, 10);
        safeServoWrite(servo3, 170);
        safeServoWrite(servo2, 170);
        safeServoWrite(servo4, 10);
        // 保持当前状态，不自动恢复
        isActionRunning = false;
        currentAction = ACTION_SLEEP;
        break;

    case ACTION_SWING:
        switch (actionStep)
        {
        case 0:
            safeServoWrite(servo1, 120);
            safeServoWrite(servo2, 120);
            safeServoWrite(servo3, 60);
            safeServoWrite(servo4, 60);
            break;
        case 1:
            safeServoWrite(servo1, 60);
            safeServoWrite(servo2, 60);
            safeServoWrite(servo3, 120);
            safeServoWrite(servo4, 120);
            break;
        case 2:
            actionRepeat++;
            if (actionRepeat >= 5)
            {
                stopAction();
                return;
            }
            actionStep = -1;
            break;
        }
        actionStep++;
        break;

    default:
        // 无效动作，恢复空闲状态
        stopAction();
        break;
    }
}