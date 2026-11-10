#ifndef SERVOCONTROL_H
#define SERVOCONTROL_H
#include <Arduino.h>
#include <ESP32Servo.h>

// 动作状态定义
#define ACTION_IDLE 0     // 空闲状态
#define ACTION_FRONT 1    // 前进
#define ACTION_BACK 2     // 后退
#define ACTION_LEFT 3     // 左转
#define ACTION_RIGHT 4    // 右转
#define ACTION_SITDOWN 5  // 坐下
#define ACTION_LIE 6      // 躺下
#define ACTION_TOPLEFT 7  // 左上(握手)
#define ACTION_TOPRIGHT 8 // 右上(握手)
#define ACTION_SLEEP 9    // 睡觉
#define ACTION_SWING 10   // 摇摆

// 舵机参数结构体
struct ServoConfig
{
    int fl_pin;    // 左前腿舵机引脚
    int bl_pin;    // 左后腿舵机引脚
    int fr_pin;    // 右前腿舵机引脚
    int br_pin;    // 右后腿舵机引脚
    int min_pulse; // 最小脉冲宽度
    int max_pulse; // 最大脉冲宽度
};

// 外部变量声明
extern Servo servo1; // 左前腿舵机
extern Servo servo2; // 左后腿舵机
extern Servo servo3; // 右前腿舵机
extern Servo servo4; // 右后腿舵机

extern bool isActionRunning;     // 是否有动作正在执行
extern ServoConfig servo_config; // 舵机配置

/**
 * @description: 更新舵机状态
 * @return {void}
 */
void updateServos();

/**
 * @description: 初始化舵机
 * @param {ServoConfig} config - 舵机配置参数
 * @return {void}
 */
void setupServos(const ServoConfig &config);

/**
 * @description: 开始执行指定动作
 * @param {int} action - 动作编号
 * @return {void}
 */
void startAction(int action);

/**
 * @description: 停止当前动作
 * @return {void}
 */
void stopAction();

#endif // STEERING_H