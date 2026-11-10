#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#define DEVICE_ROLE_A 0 // 1为A，0为B，编译时切换

// 设备名称定义
#if DEVICE_ROLE_A
#define DEVICE_NAME "锡宝"
#else
#define DEVICE_NAME "京宝"
#endif

#include <Arduino.h>

/**
 * @description: 设置MQTT连接参数
 * @param {String} clientid - 客户端ID
 * @param {String} user - 用户名
 * @param {String} pass - 密码
 * @param {String} inTopic - 订阅主题
 * @param {String} outTopic - 发布主题
 * @return {void}
 */
void mqtt_set_params(const String &clientid, const String &user, const String &pass, const String &inTopic, const String &outTopic);

/**
 * @description: 初始化MQTT客户端
 * @return {void}
 */
void mqtt_init();

/**
 * @description: 连接MQTT服务器
 * @return {bool} 连接是否成功
 */
bool mqtt_connect();

/**
 * @description: 定期处理MQTT消息
 * @return {void}
 */
void mqtt_poll();

/**
 * @description: 发送MQTT消息
 * @param {String} payload - 消息内容
 * @return {void}
 */
void mqtt_send(const String &payload);

#endif // MQTT_H