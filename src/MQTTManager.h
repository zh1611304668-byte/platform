#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <ArduinoJson.h>
#include <MQTT.h>

#define TINY_GSM_MODEM_BG96
#include "StrokeDataManager.h"
#include <TinyGsmClient.h>

constexpr int TXD1 = 43;
constexpr int RXD1 = 44;

extern const char *apn;
extern TinyGsm *hybridModem;
extern TinyGsmClient *hybridHttpClient;
extern MQTTClient *hybridMqttClient;
extern bool hybridPppConnected;
extern String mqtt_server;

extern int mqttRetryCount;
extern unsigned long mqttRetryStartTime;
extern bool mqttGracefullyDegraded;
extern const int MAX_MQTT_RETRIES;
extern const unsigned long RETRY_RESET_INTERVAL;
extern int networkRetryCount;
extern bool networkInitGracefullyDegraded;
extern const int MAX_NETWORK_RETRIES;
extern const unsigned long NETWORK_RETRY_RESET_INTERVAL;
extern unsigned long networkRetryStartTime;
extern int mqtt_port;
extern String mqtt_user;
extern String mqtt_pass;
extern String deviceIMEI;
extern String topicTrainStroke;
extern String topicStatus;
extern String topicHR;

enum NetworkType {

  NETWORK_NONE,

  NETWORK_4G

};

extern NetworkType currentNetwork;
extern unsigned long lastMqttPost;
extern const unsigned long MQTT_POST_INTERVAL_MS;
extern String getRTCFullDateTime();
extern bool rtcInitialized;
extern bool rtcTimeSynced;

enum NetworkStage {

  NET_STAGE_AT_INIT,

  NET_STAGE_PPP_DIAL,

  NET_STAGE_API_FETCH,

  NET_STAGE_TIME_SYNC

};

struct NetworkStageState {

  NetworkStage stage;

  int attempt;

  bool success;
};

bool runAtInitStep(uint32_t timeoutMs, int maxRetries,
                   NetworkStageState &state);
bool runPppDialStep(uint32_t timeoutMs, int maxRetries,
                    NetworkStageState &state);
bool runApiFetchStep(uint32_t timeoutMs, int maxRetries,
                     NetworkStageState &state);
bool runTimeSyncStep(uint32_t timeoutMs, int maxRetries,
                     NetworkStageState &state);
void setup_network();
bool connectHybridSocketMQTT();
bool publishHybridSocketMQTT(const String &topic, const String &payload,
                             uint8_t qos = 0);
bool publishStrokeMQTT(const String &topic, const String &payload);
void cleanupHybridNetwork();
bool requestNetworkSuspend();
bool requestNetworkResume();
bool isNetworkTaskSuspended();
bool disconnectMQTTGracefully();

bool disconnectPPPCompletely();

void configureMQTTClient();

bool sendATCommand(String command, String expectedResponse,
                   unsigned long timeout = 5000, String *response = nullptr);

bool init4GModule();

String getValidTimestamp();

void mqttCallback(char *topic, byte *payload, unsigned int length);

void mqttReconnect();

String buildMqttPayload();

String buildStatusPayload();
String buildHeartRatePayload();
String generateTrainingDataJSON(int customStrokeCount = -1);
String generateStrokeJSON(StrokeSnapshot snapshot);

void updateMQTTConfig(const String &host, int port, const String &username,
                      const String &password);

void publishStrokeEvent();

void mqttTask(void *pvParameters);

void get4GTimeAndSync();

// 时间戳解析工具函数：将时间戳字符串转换为Unix秒数
unsigned long parseTimestampToSeconds(const String &timestamp);
// 计算两个时间戳之间的时间差（秒，含毫秒精度）
float calculateTimeDifference(const String &timestamp1,
                              const String &timestamp2);

#endif
