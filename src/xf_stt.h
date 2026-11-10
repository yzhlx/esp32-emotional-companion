#ifndef XF_STT_H
#define XF_STT_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @description: 初始化NTP客户端，用于获取时间戳
 * @return {void}
 */
void setup_ntp_client();

/**
 * @description: 启动一次性语音识别
 * @return {void}
 */
void startXFSTT();

/**
 * @description: 开始录音采集
 * @return {void}
 */
void startXFSTT_record_begin();

/**
 * @description: 结束录音并上传识别
 * @return {void}
 */
void startXFSTT_record_end();

/**
 * @description: 录音数据采集循环，需在主循环中调用
 * @return {void}
 */
void startXFSTT_record_loop();

/**
 * @description: 添加留言到服务器
 * @param {String} person_username 发送者用户名
 * @param {String} message 留言内容
 * @return {void}
 */
void addMessage(const String &person_username, const String &message);

#ifdef __cplusplus
}
#endif 

#endif // XF_STT_H 