#pragma once
// Copy this example to include/project_config.h and fill it in locally.
// NEVER commit project_config.h or any real credential to Git.
// It is normal that the placeholder values below cannot access the actual services.

#define PROJECT_XF_APPID "REPLACE_WITH_NEW_XFYUN_APPID"
#define PROJECT_XF_API_SECRET "REPLACE_WITH_NEW_XFYUN_API_SECRET"
#define PROJECT_XF_API_KEY "REPLACE_WITH_NEW_XFYUN_API_KEY"

#define PROJECT_MQTT_BROKER "your-broker.example.com"
#define PROJECT_MQTT_PORT 8883

// A/B identity and topic names must match your broker account configuration.
#define PROJECT_MQTT_USER_A "REPLACE_WITH_MQTT_USER_A"
#define PROJECT_MQTT_PASS_A "REPLACE_WITH_MQTT_PASS_A"
#define PROJECT_MQTT_IN_TOPIC_A "devices/a/in"
#define PROJECT_MQTT_OUT_TOPIC_A "devices/a/out"

#define PROJECT_MQTT_USER_B "REPLACE_WITH_MQTT_USER_B"
#define PROJECT_MQTT_PASS_B "REPLACE_WITH_MQTT_PASS_B"
#define PROJECT_MQTT_IN_TOPIC_B "devices/b/in"
#define PROJECT_MQTT_OUT_TOPIC_B "devices/b/out"

// The old source used a separate message service with hard-coded http:// endpoints.
// Replace with a trusted HTTPS service you control, with authentication and access controls.
#define PROJECT_MESSAGE_GET_URL "https://example.invalid/api/getLatestMessage/"
#define PROJECT_MESSAGE_MARK_READ_URL "https://example.invalid/api/updateLatestIsRead/"
#define PROJECT_MESSAGE_POST_URL "https://example.invalid/api/addMessage/"
