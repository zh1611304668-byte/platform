#include "ConfigManager.h"
#include "DataFlowManager.h"
#include "MQTTManager.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <lvgl.h>
#include <ui.h>

extern HardwareSerial Serial4G;

extern TinyGsm *hybridModem;
extern TinyGsmClient *hybridHttpClient;
extern bool hybridPppConnected;

extern SemaphoreHandle_t serial4GMutex;
extern bool configLoadingInProgress;
extern bool startMqttTaskIfReady(const char *sourceTag);

extern SemaphoreHandle_t dataMutex;
extern float strokeRate;
extern float speedMps;
extern int strokeCount;
extern float totalDistance;
extern String localTime;

const String ConfigManager::PLATFORM_API_PATH = "/crewtraining-api";

TaskHandle_t ConfigManager::configTaskHandle = nullptr;
bool ConfigManager::taskRunning = false;

ConfigManager configManager;
ConfigManager::ConfigManager() {

  httpConnectionOpen = false;
  serverConfigured = false;

  platformHost = "";
  platformPort = 0;
}
ConfigManager::~ConfigManager() {
  stopConfigLoading();

  closeHttpConnection();
}

void ConfigManager::startConfigLoading() {
  if (configLoadingInProgress) {
    Serial.println("[CONFIG] 配置加载已在进行中，跳过重复调用");
    return;
  }

  // 如果平台地址为空,打印提示并返?,等待SETAPI命令
  if (platformHost.isEmpty() || platformPort == 0) {
    // 即使没有平台地址，也尝试加载NVS缓存
    if (!deviceConfig.isValid) {
      loadConfigFromNVS();
    }
    updateScreen4Display();
    return;
  }

  closeHttpConnection();
  esp_task_wdt_reset();

  if (deviceIMEI.length() == 0) {
    deviceConfigStatus = CONFIG_FAILED;
    updateScreen4Display();
    return;
  }

  if (isConfigReady()) {
    Serial.println("[CONFIG] 配置已存在，跳过重复加载");
    return;
  }

  // 先尝试从NVS加载缓存配置（秒启动）
  if (!deviceConfig.isValid) {
    loadConfigFromNVS();
  }

  if (!loadAllConfigsInOneConnection()) {
    Serial.println("[CONFIG] 配置加载失败");
    if (!deviceConfig.isValid)
      deviceConfigStatus = CONFIG_FAILED;
    if (rowerList.empty())
      rowerListStatus = CONFIG_FAILED;
    if (!mqttConfig.isValid)
      mqttConfigStatus = CONFIG_FAILED;
    updateScreen4Display();
  }
}

void ConfigManager::stopConfigLoading() {
  if (!taskRunning)
    return;

  taskRunning = false;
  if (configTaskHandle != nullptr) {
    vTaskDelete(configTaskHandle);
    configTaskHandle = nullptr;
  }

  closeHttpConnection();
}

void ConfigManager::configTask(void *pvParameters) {
  Serial.println("[CONFIG] Config任务启动");
  ConfigManager *manager = static_cast<ConfigManager *>(pvParameters);

  manager->httpConnectionOpen = false;
  manager->serverConfigured = false;

  Serial.println("[CONFIG] 等待MQTT连接稳定...");

  for (int i = 0; i < 20; i++) {
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_task_wdt_reset();
    if (i % 4 == 3) {
      Serial.printf("[CONFIG] 等待?... %d/10秒\n", (i + 1) * 500 / 1000);
    }
  }
  Serial.println("[CONFIG] 开始配置加?");

  while (taskRunning) {
    bool needsUpdate = false;

    if (!manager->mqttConfig.isValid) {
      manager->mqttConfigStatus = CONFIG_LOADING;
      String response;
      String tenantCode = "default";
      String endpoint =
          "/basic/param/getParamByTenant?tenantCode=" + tenantCode;

      bool requestSuccess = false;
      for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.printf("[CONFIG] MQTT配置尝试 %d/3\n", attempt);

        if (manager->makeHttpRequest(endpoint, response)) {
          if (manager->parseMQTTConfigResponse(response)) {
            manager->mqttConfigStatus = CONFIG_SUCCESS;
            needsUpdate = true;
            requestSuccess = true;
            break;
          } else {
            Serial.println("[CONFIG] MQTT配置解析失败");
          }
        } else {
          Serial.println("[CONFIG] MQTT配置请求失败");

          manager->closeHttpConnection();
        }

        if (attempt < 3) {

          for (int j = 0; j < 5; j++) {
            vTaskDelay(pdMS_TO_TICKS(500));
            esp_task_wdt_reset();
          }
        }
      }

      if (!requestSuccess) {
        manager->mqttConfigStatus = CONFIG_FAILED;
      }
    }

    if (manager->mqttConfig.isValid && !manager->deviceConfig.isValid) {
      Serial.println("[CONFIG] API1: 获取设备配置");
      delay(100);

      manager->deviceConfigStatus = CONFIG_LOADING;

      manager->updateScreen4Display();

      String response;
      String endpoint =
          String("/basic/stroke-rate/infoByDeviceCode?deviceCode=") +
          manager->getDeviceIMEI();

      if (manager->makeHttpRequest(endpoint, response)) {
        if (manager->parseDeviceConfigResponse(response)) {
          manager->deviceConfigStatus = CONFIG_SUCCESS;
          needsUpdate = true;
        } else {
          manager->deviceConfigStatus = CONFIG_FAILED;
        }
      } else {
        manager->deviceConfigStatus = CONFIG_FAILED;
        manager->closeHttpConnection();
      }
    }

    if (manager->deviceConfig.isValid && manager->rowerList.empty()) {
      Serial.println("[CONFIG] API2: 获取心率带设备列表?");
      delay(100);

      manager->rowerListStatus = CONFIG_LOADING;
      manager->updateScreen4Display();

      String endpoint =
          "/basic/heart-rate-monitor/deviceRowerList?tenantCode=" +
          manager->deviceConfig.tenantCode +
          "&boatCode=" + manager->deviceConfig.boatCode;

      bool requestSuccess = false;
      const int maxRetries = 3;
      const int retryDelay = 2000;

      for (int attempt = 1; attempt <= maxRetries; attempt++) {
        Serial.printf("[CONFIG] API2: 尝试 %d/%d\n", attempt, maxRetries);

        String response;
        if (manager->makeHttpRequest(endpoint, response)) {
          if (manager->parseRowerListResponse(response)) {
            manager->rowerListStatus = CONFIG_SUCCESS;
            requestSuccess = true;
            break;
          } else {
            Serial.println("[CONFIG] API2: JSON解析失败");
          }
        } else {
          Serial.println("[CONFIG] API2: HTTP请求失败");
          manager->closeHttpConnection();
        }

        if (attempt < maxRetries) {
          Serial.println("[CONFIG] 重试心率带设备列表获取?...");
          vTaskDelay(pdMS_TO_TICKS(retryDelay));
        }
      }

      if (!requestSuccess) {
        manager->rowerListStatus = CONFIG_FAILED;
      }
    }

    if (needsUpdate) {
      manager->updateScreen4Display();
    }

    if (manager->mqttConfig.isValid && manager->deviceConfig.isValid &&
        !manager->rowerList.empty()) {
      break;
    }

    for (int j = 0; j < 10; j++) {
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_task_wdt_reset();
    }
  }

  taskRunning = false;

  Serial.println("[CONFIG] 配置任务完成，关闭HTTP连接");
  manager->closeHttpConnection();

  vTaskDelete(nullptr);
}

bool ConfigManager::parseDeviceConfigResponse(const String &json) {
  Serial.printf("[JSON-DEBUG] 收到设备配置JSON: %s\n", json.c_str());

  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.printf("[JSON-DEBUG] JSON解析失败: %s\n", error.c_str());
    return false;
  }

  if (!doc["success"].as<bool>()) {
    Serial.println("[JSON-DEBUG] success字段为false或不存在");
    return false;
  }

  JsonObject data = doc["data"];
  deviceConfig.tenantCode = data["tenantCode"].as<String>();
  deviceConfig.boatCode = data["boatCode"].as<String>();
  deviceConfig.boatName = data["boatName"].as<String>();
  deviceConfig.stdId = data["stdId"].as<String>();
  deviceConfig.isValid = true;

  Serial.printf("[JSON] 设备配置: 租户=%s 船只=%s 名称=%s\n",
                deviceConfig.tenantCode.c_str(), deviceConfig.boatCode.c_str(),
                deviceConfig.boatName.c_str());
  return true;
}

bool ConfigManager::parseMQTTConfigResponse(const String &json) {
  Serial.printf("[JSON-DEBUG] 收到MQTT配置JSON: %s\n", json.c_str());

  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.printf("[JSON-DEBUG] MQTT JSON解析失败: %s\n", error.c_str());
    return false;
  }

  if (!doc["success"].as<bool>()) {
    Serial.println("[JSON-DEBUG] MQTT success字段为false或不存在");
    return false;
  }

  JsonObject data = doc["data"];
  JsonObject mcu = data["mcu"];

  if (mcu) {
    mqttConfig.host = mcu["host"].as<String>();
    mqttConfig.port = mcu["port"].as<int>();
    mqttConfig.username = mcu["username"].as<String>();
    mqttConfig.password = mcu["password"].as<String>();
    mqttConfig.cleanSession = mcu["cleanSession"].as<bool>();
    mqttConfig.keepAliveInterval = mcu["keepAliveInterval"].as<int>();

    rtkConfig.isValid = false;
  } else {
    return false;
  }

  mqttConfig.isValid = (mqttConfig.host.length() > 0 && mqttConfig.port > 0);

  Serial.printf("[JSON] MQTT: %s:%d user=%s\n", mqttConfig.host.c_str(),
                mqttConfig.port, mqttConfig.username.c_str());

  return mqttConfig.isValid;
}

bool ConfigManager::parseRowerListResponse(const String &json) {
  Serial.printf("[JSON-DEBUG] 收到心率设备JSON长度: %d\n", json.length());
  Serial.printf("[JSON-DEBUG] JSON200字符: %s\n",
                json.substring(0, 200).c_str());
  Serial.printf("[JSON-DEBUG] JSON100字符: %s\n",
                json.substring(json.length() - 100).c_str());

  if (json.length() == 0) {
    Serial.println("[JSON-DEBUG] JSON为空");
    return false;
  }

  if (!json.startsWith("{") || !json.endsWith("}")) {
    Serial.println("[JSON-DEBUG] JSON格式不正确，不是以{}包围");
    return false;
  }

  DynamicJsonDocument doc(24576);
  DeserializationError error = deserializeJson(doc, json);
  if (error) {
    Serial.printf("[JSON-DEBUG] 心率设备JSON解析失败: %s\n", error.c_str());
    Serial.printf("[JSON-DEBUG] 错误位置: %d\n", error.c_str());
    Serial.printf("[JSON-DEBUG] JSON中间部分: %s\n",
                  json.substring(200, 400).c_str());
    return false;
  }

  if (!doc["success"].as<bool>()) {
    Serial.println("[JSON-DEBUG] 心率设备success字段为false或不存在");
    return false;
  }

  rowerList.clear();
  for (JsonObject item : doc["data"].as<JsonArray>()) {
    RowerInfo rower;
    rower.rowerId = item["rowerId"].as<String>();
    rower.rowerName = item["rowerName"].as<String>();
    rower.btAddr = item["btAddr"].as<String>();
    rower.deviceCode = item["deviceCode"].as<String>();
    rowerList.push_back(rower);
  }

  Serial.printf("[JSON] 心率 %d 个\n", rowerList.size());
  return true;
}

void ConfigManager::printHeartRateDevices() {}

void ConfigManager::updateScreen4Display() {

  extern bool ui_initialized;
  if (!ui_initialized) {

    return;
  }

  if (deviceConfigStatus == CONFIG_SUCCESS && deviceConfig.isValid) {
    // 4G模块IMEI设备 -> ui_Label81
    lv_label_set_text(ui_Label81, deviceIMEI.c_str());
    // 基站ID -> ui_Label84
    lv_label_set_text(ui_Label84, deviceConfig.stdId.c_str());

    // 租户编号 -> ui_Label83
    lv_label_set_text(ui_Label83, deviceConfig.tenantCode.c_str());

    // 船组编号+船组名称 -> ui_Label82
    String boatInfo = deviceConfig.boatCode + " " + deviceConfig.boatName;
    lv_label_set_text(ui_Label82, boatInfo.c_str());

    // Screen1 船号 -> ui_Label33
    lv_label_set_text(ui_Label33, deviceConfig.boatCode.c_str());

  } else if (deviceConfigStatus == CONFIG_FAILED) {
    lv_label_set_text(ui_Label81, "加载失败");
    lv_label_set_text(ui_Label84, "加载失败");
    lv_label_set_text(ui_Label83, "加载失败");
    lv_label_set_text(ui_Label82, "加载失败");
    lv_label_set_text(ui_Label33, "");
  }

  if (!platformHost.isEmpty() && platformPort > 0) {
    String platformInfo = platformHost + ":" + String(platformPort);
    lv_label_set_text(ui_Label80, platformInfo.c_str());
  } else {
    lv_label_set_text(ui_Label80, "未配置");
  }
}

void ConfigManager::refreshConfig() {

  deviceConfig.isValid = false;
  mqttConfig.isValid = false;
  rtkConfig.isValid = false;

  deviceConfigStatus = CONFIG_IDLE;
  mqttConfigStatus = CONFIG_IDLE;
  rtkConfigStatus = CONFIG_IDLE;

  closeHttpConnection();
}

bool ConfigManager::send4GATCommand(String command, String expectedResponse,
                                    unsigned long timeout) {

  if (!serial4GMutex ||
      xSemaphoreTake(serial4GMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    Serial.println("[CONFIG-AT] 无法获取串口互斥");
    return false;
  }

  Serial4G.println(command);

  String resp = "";
  unsigned long startTime = millis();
  while (millis() - startTime < timeout) {
    if (Serial4G.available()) {
      resp += Serial4G.readString();
      if (resp.indexOf(expectedResponse) != -1) {
        xSemaphoreGive(serial4GMutex);
        return true;
      }
    }
    delay(2);
  }

  Serial.println("[CONFIG] AT命令超时");
  xSemaphoreGive(serial4GMutex);
  return false;
}

bool ConfigManager::make4GHttpRequest(const String &endpoint,
                                      String &response) {

  return make4GHttpRequestHTTP(endpoint, response);
}

bool ConfigManager::makeHttpRequest(const String &endpoint, String &response) {
  if (!hybridPppConnected || !hybridHttpClient || !hybridModem) {
    Serial.println("[CONFIG] PPP连接或TinyGSM对象未就");
    return false;
  }

  if (!serial4GMutex ||
      xSemaphoreTake(serial4GMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
    Serial.println("[HTTP] 无法获取串口互斥");
    return false;
  }

  Serial.printf("[HTTP] === 开始HTTP请求 ===\n");
  Serial.printf("[HTTP] URL: %s\n", buildFullUrl(endpoint).c_str());

  esp_task_wdt_reset();

  const char *SERVER_HOST = platformHost.c_str();
  const int SERVER_PORT = platformPort;

  Serial.printf("[HTTP] 📶 信号: %d\n", hybridModem->getSignalQuality());

  Serial.printf("[HTTP] 🔌 连接服务 %s:%d...\n", SERVER_HOST, SERVER_PORT);
  if (!hybridHttpClient->connect(SERVER_HOST, SERVER_PORT)) {
    Serial.println("[HTTP]  Socket连接失败");
    xSemaphoreGive(serial4GMutex);
    return false;
  }
  Serial.println("[HTTP] Socket连接建立成功");

  Serial.print("[HTTP] 📤 发送HTTP请求...");
  size_t requestSize = 0;
  requestSize += hybridHttpClient->print("GET " + String(PLATFORM_API_PATH) +
                                         endpoint + " HTTP/1.1\r\n");
  requestSize += hybridHttpClient->print("Host: ");
  requestSize += hybridHttpClient->print(SERVER_HOST);
  requestSize += hybridHttpClient->print("\r\n");
  requestSize += hybridHttpClient->print("Accept: application/json\r\n");
  requestSize += hybridHttpClient->print("Connection: close\r\n\r\n");
  Serial.printf(" %d字节已发送\n", requestSize);

  response = "";
  unsigned long startTime = millis();
  unsigned long lastDataTime = millis();
  const size_t maxResponseSize = 12288;
  unsigned long lastYieldTime = millis();
  unsigned long lastStatusPrint = millis();

  Serial.println("[HTTP] 🔄 等待服务器响应...");

  while (millis() - startTime < 5000) {

    bool hasData = false;
    int bytesThisLoop = 0;

    while (hybridHttpClient->available() &&
           response.length() < maxResponseSize) {
      char c = hybridHttpClient->read();
      response += c;
      bytesThisLoop++;
      hasData = true;
      lastDataTime = millis();
    }

    if (bytesThisLoop > 0) {
      Serial.printf("[HTTP] 📥 收到 %d 字节，总计 %d 字节\n", bytesThisLoop,
                    response.length());
    }

    if (!hybridHttpClient->connected()) {
      Serial.printf("[HTTP] 🔌 服务器关闭连接，已收到 %d 字节\n",
                    response.length());
      break;
    }

    if (hasData && millis() - lastDataTime > 400) {
      Serial.printf("[HTTP]  数据接收完成，总计 %d 字节\n", response.length());
      break;
    }

    if (millis() - lastStatusPrint > 3000) {
      Serial.printf("[HTTP]等待...已收到 %d 字节，连接状态: %s\n",
                    response.length(),
                    hybridHttpClient->connected() ? "连接" : "断开");
      lastStatusPrint = millis();
    }

    if (millis() - lastYieldTime > 50) {
      vTaskDelay(1);
      esp_task_wdt_reset();
      lastYieldTime = millis();

      if (millis() - startTime > 60000) {
        Serial.println("[HTTP]  HTTP请求超时，优雅退出避免看门狗重启");
        break;
      }
    }

    vTaskDelay(1);
  }

  if (millis() - startTime >= 5000) {
    Serial.printf("[HTTP] 超时，已收到 %d 字节\n", response.length());
  }

  hybridHttpClient->stop();

  if (response.length() == 0) {
    Serial.println("[HTTP]  未收到任何数");
    xSemaphoreGive(serial4GMutex);
    return false;
  }
  Serial.printf("[HTTP]  原始响应长度: %d\n", response.length());

  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart != -1) {
    String body = response.substring(bodyStart + 4);
    int s = body.indexOf("{");
    int e = body.lastIndexOf("}");
    Serial.printf("[HTTP]  Body长度: %d, JSON起止: %d..%d\n", body.length(), s,
                  e);
    if (s != -1 && e != -1 && e > s) {
      response = body.substring(s, e + 1);
      Serial.printf("[HTTP] 提取JSON长度: %d\n", response.length());
      Serial.println("[HTTP] === 结束HTTP请求 ===");
      xSemaphoreGive(serial4GMutex);
      return true;
    } else {
      Serial.println("[HTTP]  无法提取JSON数据");
      xSemaphoreGive(serial4GMutex);
      return false;
    }
  } else {
    Serial.println("[HTTP] 无效的HTTP响应（无空行分隔）");
    xSemaphoreGive(serial4GMutex);
    return false;
  }
}

void ConfigManager::stripHttpUrcs(String &response) {

  int pos = 0;
  while ((pos = response.indexOf("$HTTPRECV")) != -1) {
    int lineEnd = response.indexOf('\n', pos);
    if (lineEnd == -1)
      lineEnd = response.length();
    response.remove(pos,
                    lineEnd - pos + (lineEnd < (int)response.length() ? 1 : 0));
  }

  pos = 0;
  while ((pos = response.indexOf("$HTTPACTION")) != -1) {
    int lineEnd = response.indexOf('\n', pos);
    if (lineEnd == -1)
      lineEnd = response.length();
    response.remove(pos,
                    lineEnd - pos + (lineEnd < (int)response.length() ? 1 : 0));
  }

  pos = 0;
  while ((pos = response.indexOf("HTTP/1.1")) != -1) {
    int lineEnd = response.indexOf('\n', pos);
    if (lineEnd == -1)
      break;

    String statusLine = response.substring(pos, lineEnd + 1);
    if (statusLine.indexOf("200") != -1 || statusLine.indexOf("OK") != -1) {
      response.remove(pos, lineEnd - pos + 1);
    } else {
      break;
    }
  }

  response.replace("\r\n", "\n");
  response.replace("\r", "");

  while (response.indexOf("\n\n") != -1) {
    response.replace("\n\n", "\n");
  }

  response.trim();
}

bool ConfigManager::make4GHttpRequestHTTP(const String &endpoint,
                                          String &response) {

  if (!httpConnectionOpen) {
    send4GATCommand("AT$HTTPCLOSE", "OK", 300);
    delay(50);
    while (Serial4G.available())
      Serial4G.read();

    if (!send4GATCommand("AT$HTTPOPEN", "OK", 2000)) {
      Serial.println("[CONFIG] HTTP服务开启失败");
      return false;
    }
    delay(20);
    httpConnectionOpen = true;
    serverConfigured = false;

  } else {
  }

  if (!serverConfigured) {
    String hostCmd = "AT$HTTPPARA=" + platformHost + "," + String(platformPort);
    if (!send4GATCommand(hostCmd, "OK", 800)) {
      Serial.println("[CONFIG] 服务器设置失败");
      send4GATCommand("AT$HTTPCLOSE", "OK", 300);
      httpConnectionOpen = false;
      serverConfigured = false;
      return false;
    }
    serverConfigured = true;

  } else {
  }

  String uri = PLATFORM_API_PATH + endpoint;
  String fullUrl = "http://" + platformHost + ":" + String(platformPort) + uri;
  String urlCmd = "AT$HTTPPARA=" + fullUrl + "," + String(platformPort);
  if (!send4GATCommand(urlCmd, "OK", 800)) {
    Serial.println("[CONFIG] URL设置失败");
    send4GATCommand("AT$HTTPCLOSE", "OK", 300);
    httpConnectionOpen = false;
    serverConfigured = false;
    return false;
  }

  Serial4G.println("AT$HTTPACTION=0");

  response = "";
  unsigned long startTime = millis();
  unsigned long lastDataTime = millis();
  bool foundJson = false;
  const size_t maxResponseSize = 10240;

  while (millis() - startTime < 2000) {
    if (Serial4G.available()) {
      String newData = Serial4G.readString();

      if (response.length() + newData.length() > maxResponseSize) {

        newData = newData.substring(0, maxResponseSize - response.length());
      }
      response += newData;
      lastDataTime = millis();

      if (!foundJson && response.indexOf("{") != -1) {
        foundJson = true;
      }

      if (foundJson && response.indexOf("}") != -1 &&
          response.indexOf("success") != -1) {
        delay(30);
        break;
      }
    }

    if (millis() - lastDataTime > 200) {
      break;
    }

    delay(3);
  }

  stripHttpUrcs(response);

  int s = response.indexOf("{");
  int e = response.lastIndexOf("}");
  if (s != -1 && e != -1 && e > s) {
    String json = response.substring(s, e + 1);

    if (json.indexOf("success") != -1 && json.indexOf("data") != -1) {

      int openBraces = 0;
      int closeBraces = 0;
      for (int i = 0; i < json.length(); i++) {
        if (json.charAt(i) == '{')
          openBraces++;
        if (json.charAt(i) == '}')
          closeBraces++;
      }

      if (openBraces == closeBraces && openBraces > 0) {
        response = json;

        return true;
      }
    }
  }

  Serial.println("[CONFIG] 响应无效，重置HTTP连接");
  httpConnectionOpen = false;
  serverConfigured = false;
  return false;
}

void ConfigManager::closeHttpConnection() {

  if (hybridHttpClient && hybridHttpClient->connected()) {
    hybridHttpClient->stop();
    Serial.println("[CONFIG] 🔌 关闭HTTP连接");
  }

  if (httpConnectionOpen) {
    send4GATCommand("AT$HTTPCLOSE", "OK", 500);
    httpConnectionOpen = false;
    serverConfigured = false;
  }
}

bool ConfigManager::make4GHttpRequestWithPort(const String &endpoint,
                                              String &response, int port) {

  closeHttpConnection();

  send4GATCommand("AT$HTTPCLOSE", "OK", 300);
  delay(50);
  if (!send4GATCommand("AT$HTTPOPEN", "OK", 2000)) {
    Serial.println("[CONFIG] HTTP服务开启失败");
    return false;
  }
  String hostCmd = "AT$HTTPPARA=" + platformHost + "," + String(platformPort);
  if (!send4GATCommand(hostCmd, "OK", 800)) {
    Serial.println("[CONFIG] 主机配置失败");
    send4GATCommand("AT$HTTPCLOSE", "OK", 300);
    return false;
  }

  String uri = PLATFORM_API_PATH + endpoint;
  String fullUrl = "http://" + platformHost + ":" + String(platformPort) + uri;
  String urlCmd = "AT$HTTPPARA=" + fullUrl + "," + String(platformPort);
  if (!send4GATCommand(urlCmd, "OK", 800)) {
    Serial.println("[CONFIG] URL配置失败");
    send4GATCommand("AT$HTTPCLOSE", "OK", 300);
    return false;
  }

  Serial4G.println("AT$HTTPACTION=0");

  response = "";
  unsigned long startTime = millis();
  unsigned long lastDataTime = millis();
  bool foundJson = false;

  while (millis() - startTime < 2000) {
    if (Serial4G.available()) {
      response += Serial4G.readString();
      lastDataTime = millis();

      if (!foundJson && response.indexOf("{") != -1) {
        foundJson = true;
      }

      if (foundJson && response.indexOf("}") != -1 &&
          response.indexOf("success") != -1) {
        delay(30);
        break;
      }
    }

    if (millis() - lastDataTime > 200) {
      break;
    }

    delay(3);
  }

  send4GATCommand("AT$HTTPCLOSE", "OK", 300);
  if (response.indexOf("200 OK") != -1) {
    int s = response.indexOf("{");
    if (s != -1) {
      response = response.substring(s);
      int e = response.lastIndexOf("}");
      if (e != -1) {
        response = response.substring(0, e + 1);

        return true;
      }
    }
  }
  Serial.println("[CONFIG] 请求失败或无数据");
  return false;
}

bool ConfigManager::isAddressInWhitelist(const String &address) const {
  for (const RowerInfo &rower : rowerList) {
    if (rower.btAddr.equalsIgnoreCase(address)) {
      return true;
    }
  }
  return false;
}

bool ConfigManager::getDeviceInfoByAddress(const String &address,
                                           String &deviceCode,
                                           String &rowerName) const {
  for (const RowerInfo &rower : rowerList) {
    if (rower.btAddr.equalsIgnoreCase(address)) {
      deviceCode = rower.deviceCode;
      rowerName = rower.rowerName;
      return true;
    }
  }
  return false;
}

void ConfigManager::setDeviceIMEI(const String &imei) {
  deviceIMEI = imei;
  Serial.printf("[CONFIG] 设置IMEI: %s\n", imei.c_str());
}

String ConfigManager::getDeviceIMEI() const { return deviceIMEI; }

bool ConfigManager::setTimeFromNITZ(const String &nitzTimeStr) {

  if (nitzTimeStr.length() < 17) {
    Serial.println("[CONFIG] NITZ时间格式错误");
    return false;
  }

  int commaIndex = nitzTimeStr.indexOf(',');
  if (commaIndex == -1) {
    Serial.println("[CONFIG] NITZ时间格式错误");
    return false;
  }

  String datePart = nitzTimeStr.substring(0, commaIndex);
  String timePart = nitzTimeStr.substring(commaIndex + 1);

  int plusIndex = timePart.indexOf('+');
  if (plusIndex != -1) {
    timePart = timePart.substring(0, plusIndex);
  }

  String year = "20" + datePart.substring(0, 2);
  String month = datePart.substring(3, 5);
  String day = datePart.substring(6, 8);

  if (month.length() == 1)
    month = "0" + month;
  if (day.length() == 1)
    day = "0" + day;

  String formattedDate = year + "-" + month + "-" + day;
  String formattedTime = timePart;
  String formattedDateTime = formattedDate + " " + formattedTime;

  setDateTime(formattedDate, formattedTime, formattedDateTime);

  return true;
}

void ConfigManager::enableNITZTime() {}

void ConfigManager::processNITZTime(const String &nitzData) {

  int firstComma = nitzData.indexOf(',');
  int secondComma = nitzData.indexOf(',', firstComma + 1);
  int thirdComma = nitzData.indexOf(',', secondComma + 1);

  if (firstComma == -1 || secondComma == -1 || thirdComma == -1) {
    return;
  }

  globalDate = nitzData.substring(secondComma + 2, thirdComma - 1);
  globalTime = nitzData.substring(thirdComma + 2, nitzData.length() - 1);

  if (globalDate.length() == 8) {
    String year = "20" + globalDate.substring(0, 2);
    String month = globalDate.substring(3, 5);
    String day = globalDate.substring(6, 8);

    if (month.length() == 1)
      month = "0" + month;
    if (day.length() == 1)
      day = "0" + day;

    globalDateTime = year + "-" + month + "-" + day + " " + globalTime + ":00";
  }

  nitzTimeValid = true;
  lastNITZUpdate = millis();
}

String ConfigManager::getFormattedDateTime() const {

  if (!nitzTimeValid || globalDateTime.isEmpty()) {
    return "";
  }

  if (millis() - lastNITZUpdate > 3600000) {
    return "";
  }

  return globalDateTime;
}

bool ConfigManager::isTimeValid() const {
  return nitzTimeValid && !globalDateTime.isEmpty() &&
         (millis() - lastNITZUpdate < 3600000);
}

String ConfigManager::getTimeHHMM() const {

  if (!nitzTimeValid || globalTime.isEmpty()) {
    return "--:--";
  }

  if (millis() - lastNITZUpdate > 3600000) {
    return "--:--";
  }

  if (globalTime.length() >= 5) {
    return globalTime.substring(0, 5);
  }

  return "--:--";
}

void ConfigManager::setDateTime(const String &date, const String &time,
                                const String &dateTime) {
  globalDate = date;
  globalTime = time;
  globalDateTime = dateTime;
  nitzTimeValid = true;
  lastNITZUpdate = millis();

  updateTimeBaseline(dateTime, millis());
}

void ConfigManager::updateTimeBaseline(const String &dateTime,
                                       unsigned long currentMillis) {
  baselineDateTime = dateTime;
  baselineMillis = currentMillis;
}

String ConfigManager::getCurrentFormattedDateTime() {
  if (baselineDateTime.isEmpty()) {
    return getFormattedDateTime();
  }

  unsigned long elapsedMillis = millis() - baselineMillis;
  unsigned long elapsedSeconds = elapsedMillis / 1000;

  if (baselineDateTime.length() < 19) {
    return baselineDateTime;
  }

  int year = baselineDateTime.substring(0, 4).toInt();
  int month = baselineDateTime.substring(5, 7).toInt();
  int day = baselineDateTime.substring(8, 10).toInt();
  int hour = baselineDateTime.substring(11, 13).toInt();
  int minute = baselineDateTime.substring(14, 16).toInt();
  int second = baselineDateTime.substring(17, 19).toInt();

  second += elapsedSeconds;

  if (second >= 60) {
    minute += second / 60;
    second = second % 60;
  }
  if (minute >= 60) {
    hour += minute / 60;
    minute = minute % 60;
  }
  if (hour >= 24) {
    day += hour / 24;
    hour = hour % 24;

    if (day > 31) {
      month++;
      day = 1;
    }
    if (month > 12) {
      year++;
      month = 1;
    }
  }

  char timeStr[20];
  snprintf(timeStr, sizeof(timeStr), "%04d-%02d-%02d %02d:%02d:%02d", year,
           month, day, hour, minute, second);

  return String(timeStr);
}

void ConfigManager::safeUpdateSensorData(float newStrokeRate, float newSpeedMps,
                                         int newStrokeCount,
                                         float newTotalDistance) {
  if (dataMutex && xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    strokeRate = newStrokeRate;
    speedMps = newSpeedMps;
    strokeCount = newStrokeCount;
    totalDistance = newTotalDistance;
    xSemaphoreGive(dataMutex);
  }
}

void ConfigManager::safeGetSensorData(float &outStrokeRate, float &outSpeedMps,
                                      int &outStrokeCount,
                                      float &outTotalDistance) {
  if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10))) {
    outStrokeRate = strokeRate;
    outSpeedMps = speedMps;
    outStrokeCount = strokeCount;
    outTotalDistance = totalDistance;
    xSemaphoreGive(dataMutex);
  }
}

void ConfigManager::safeUpdateTimeData(const String &newLocalTime) {
  if (dataMutex && xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    localTime = newLocalTime;
    xSemaphoreGive(dataMutex);
  }
}

void ConfigManager::safeGetTimeData(String &outLocalTime) {
  if (dataMutex && xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10))) {
    outLocalTime = localTime;
    xSemaphoreGive(dataMutex);
  }
}

void ConfigManager::loadPlatformAddressFromStorage() {
  Preferences prefs;
  prefs.begin("platform", true);

  platformHost = prefs.getString("apiHost", "");
  platformPort = prefs.getUShort("apiPort", 0);

  prefs.end();

  if (platformHost.isEmpty() || platformPort == 0) {
    Serial.println("[CONFIG] ⚠️  平台地址未配置，请使用SETAPI 命令设置");
    Serial.println("[CONFIG] 示例: SETAPI=117.83.111.19:10033");
  } else {
    Serial.printf("[CONFIG] 平台地址已加: %s:%d\n", platformHost.c_str(),
                  platformPort);
  }
}

void ConfigManager::savePlatformAddressToStorage() {
  Preferences prefs;
  prefs.begin("platform", false);

  prefs.putString("apiHost", platformHost);
  prefs.putUShort("apiPort", platformPort);

  prefs.end();

  Serial.printf("[CONFIG] 平台地址已保存: %s:%d\n", platformHost.c_str(),
                platformPort);
}

// ==================== NVS 配置缓存 ====================

void ConfigManager::saveConfigToNVS() {
  Preferences prefs;
  prefs.begin("cfgcache", false); // 读写模式

  // 保存 DeviceConfig
  if (deviceConfig.isValid) {
    prefs.putString("dc_tenant", deviceConfig.tenantCode);
    prefs.putString("dc_boat", deviceConfig.boatCode);
    prefs.putString("dc_name", deviceConfig.boatName);
    prefs.putString("dc_stdId", deviceConfig.stdId);
    prefs.putBool("dc_valid", true);
  }

  // 保存 MQTTConfig
  if (mqttConfig.isValid) {
    prefs.putString("mq_host", mqttConfig.host);
    prefs.putUShort("mq_port", (uint16_t)mqttConfig.port);
    prefs.putString("mq_user", mqttConfig.username);
    prefs.putString("mq_pass", mqttConfig.password);
    prefs.putBool("mq_clean", mqttConfig.cleanSession);
    prefs.putUShort("mq_keepalive", (uint16_t)mqttConfig.keepAliveInterval);
    prefs.putBool("mq_valid", true);
  }

  // 保存 RowerList（最多5个）
  int count = min((int)rowerList.size(), 5);
  prefs.putInt("rw_count", count);
  for (int i = 0; i < count; i++) {
    String prefix = "rw" + String(i) + "_";
    prefs.putString((prefix + "id").c_str(), rowerList[i].rowerId);
    prefs.putString((prefix + "name").c_str(), rowerList[i].rowerName);
    prefs.putString((prefix + "dev").c_str(), rowerList[i].deviceCode);
    prefs.putString((prefix + "bt").c_str(), rowerList[i].btAddr);
  }

  prefs.end();
  Serial.printf("[CONFIG] ✅ 配置已缓存到NVS (设备=%d, MQTT=%d, 心率=%d)\n",
                deviceConfig.isValid, mqttConfig.isValid, count);
}

void ConfigManager::loadConfigFromNVS() {
  Preferences prefs;
  prefs.begin("cfgcache", true); // 只读模式

  // 读取 DeviceConfig
  if (prefs.getBool("dc_valid", false)) {
    deviceConfig.tenantCode = prefs.getString("dc_tenant", "");
    deviceConfig.boatCode = prefs.getString("dc_boat", "");
    deviceConfig.boatName = prefs.getString("dc_name", "");
    deviceConfig.stdId = prefs.getString("dc_stdId", "");
    deviceConfig.isValid = true;
    deviceConfigStatus = CONFIG_SUCCESS;
    Serial.printf("[CONFIG] 📦 NVS缓存: 设备 租户=%s 船只=%s\n",
                  deviceConfig.tenantCode.c_str(),
                  deviceConfig.boatCode.c_str());
  }

  // 读取 MQTTConfig
  if (prefs.getBool("mq_valid", false)) {
    mqttConfig.host = prefs.getString("mq_host", "");
    mqttConfig.port = prefs.getUShort("mq_port", 1883);
    mqttConfig.username = prefs.getString("mq_user", "");
    mqttConfig.password = prefs.getString("mq_pass", "");
    mqttConfig.cleanSession = prefs.getBool("mq_clean", true);
    mqttConfig.keepAliveInterval = prefs.getUShort("mq_keepalive", 10);
    mqttConfig.isValid = (mqttConfig.host.length() > 0 && mqttConfig.port > 0);
    if (mqttConfig.isValid) {
      mqttConfigStatus = CONFIG_SUCCESS;
      Serial.printf("[CONFIG] 📦 NVS缓存: MQTT %s:%d\n",
                    mqttConfig.host.c_str(), mqttConfig.port);
    }
  }

  // 读取 RowerList
  int count = prefs.getInt("rw_count", 0);
  if (count > 0) {
    rowerList.clear();
    for (int i = 0; i < count; i++) {
      String prefix = "rw" + String(i) + "_";
      RowerInfo rower;
      rower.rowerId = prefs.getString((prefix + "id").c_str(), "");
      rower.rowerName = prefs.getString((prefix + "name").c_str(), "");
      rower.deviceCode = prefs.getString((prefix + "dev").c_str(), "");
      rower.btAddr = prefs.getString((prefix + "bt").c_str(), "");
      rowerList.push_back(rower);
    }
    rowerListStatus = CONFIG_SUCCESS;
    Serial.printf("[CONFIG] 📦 NVS缓存: 心率设备 %d 个\n", count);
  }

  prefs.end();

  if (deviceConfig.isValid || mqttConfig.isValid) {
    Serial.println("[CONFIG] ✅ NVS缓存加载完成，设备可立即使用");
    updateScreen4Display();
  } else {
    Serial.println("[CONFIG] ℹ️ NVS无缓存，等待网络获取");
  }
}

bool ConfigManager::setPlatformAddress(const String &address) {

  int colonPos = address.indexOf(':');
  if (colonPos <= 0 || colonPos == (int)address.length() - 1) {
    Serial.println("[CONFIG] 错误: 平台地址格式不正确，应为 IP:PORT");
    return false;
  }

  String host = address.substring(0, colonPos);
  String portStr = address.substring(colonPos + 1);

  for (unsigned int i = 0; i < host.length(); i++) {
    char c = host.charAt(i);
    if (!isalnum(c) && c != '.' && c != '-') {
      Serial.printf("[CONFIG] 错误: 主机名包含非法字符: %c\n", c);
      return false;
    }
  }

  int port = portStr.toInt();
  if (port < 1 || port > 65535) {
    Serial.printf("[CONFIG] 错误: 端口范围必须1-65535之间，当: %d\n", port);
    return false;
  }

  Serial.println("[CONFIG] 停止当前配置加载任务...");
  stopConfigLoading();

  closeHttpConnection();

  extern bool configLoadingInProgress;
  configLoadingInProgress = false;

  platformHost = host;
  platformPort = (uint16_t)port;

  savePlatformAddressToStorage();

  refreshConfig();

  updateScreen4Display();

  Serial.printf("[CONFIG]  平台地址已更改: %s:%d\n", platformHost.c_str(),
                platformPort);

  extern bool hybridPppConnected;
  if (hybridPppConnected) {

    Serial.println("[CONFIG] PPP已连接，立即使用新地址重新获取配置...");
    delay(100);
    startConfigLoading();
  } else {

    Serial.println(
        "[CONFIG] PPP未连接，地址已保存，等待网络连接后自动获取配置");
    Serial.println("[CONFIG] 如果网络任务已完成，配置将在10秒后自动重试");
  }

  return true;
}

String ConfigManager::getPlatformAddress() const {
  return platformHost + ":" + String(platformPort);
}

void ConfigManager::requestReloadCancel() {
  if (currentReloadPhase != RELOAD_IDLE) {
    reloadCancelRequested = true;
    Serial.println("[CONFIG] 收到取消当前重载的请");
  }
}

String ConfigManager::buildFullUrl(const String &endpoint) const {
  return "http://" + platformHost + ":" + String(platformPort) +
         PLATFORM_API_PATH + endpoint;
}

bool ConfigManager::loadAllConfigsInOneConnection() {

  if (platformHost.isEmpty() || platformPort == 0) {
    Serial.println("[CONFIG] ⚠️  平台地址未配置，跳过API获取");
    return false;
  }

  if (!hybridPppConnected || !hybridHttpClient || !hybridModem) {
    Serial.println("[CONFIG] PPP连接未建");
    return false;
  }

  if (!serial4GMutex ||
      xSemaphoreTake(serial4GMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
    Serial.println("[CONFIG] 无法获取串口互斥");
    return false;
  }

  Serial.println("[CONFIG] === 开始一次连接获取所有配 ===");
  esp_task_wdt_reset();

  const char *SERVER_HOST = platformHost.c_str();
  const int SERVER_PORT = platformPort;

  Serial.printf("[CONFIG] 🔌 连接服务 %s:%d...\n", SERVER_HOST, SERVER_PORT);
  if (!hybridHttpClient->connect(SERVER_HOST, SERVER_PORT)) {
    Serial.println("[CONFIG]  Socket连接失败");
    xSemaphoreGive(serial4GMutex);
    return false;
  }
  Serial.println("[CONFIG] Socket连接建立成功");

  bool allSuccess = true;

  Serial.println("[CONFIG] 📡 API1: 获取设备配置");
  deviceConfigStatus = CONFIG_LOADING;
  updateScreen4Display();

  String api1Response;
  String api1Endpoint =
      String(PLATFORM_API_PATH) +
      "/basic/stroke-rate/infoByDeviceCode?deviceCode=" + getDeviceIMEI();

  if (makeHttpRequestKeepAlive(api1Endpoint, api1Response)) {
    if (parseDeviceConfigResponse(api1Response)) {
      deviceConfigStatus = CONFIG_SUCCESS;
      Serial.println("[CONFIG]  API1: 设备配置获取成功");
    } else {
      Serial.println("[CONFIG]  API1: JSON解析失败");
      allSuccess = false;
    }
  } else {
    Serial.println("[CONFIG] API1: HTTP请求失败");
    allSuccess = false;
  }

  if (!allSuccess || !deviceConfig.isValid) {
    hybridHttpClient->stop();
    xSemaphoreGive(serial4GMutex);
    deviceConfigStatus = CONFIG_FAILED;
    updateScreen4Display();
    return false;
  }

  updateScreen4Display();
  vTaskDelay(pdMS_TO_TICKS(100));

  Serial.println("[CONFIG] 📡 API2: 获取心率设备列表");
  rowerListStatus = CONFIG_LOADING;
  updateScreen4Display();

  String api2Endpoint =
      String(PLATFORM_API_PATH) +
      "/basic/heart-rate-monitor/deviceRowerList?tenantCode=" +
      deviceConfig.tenantCode + "&boatCode=" + deviceConfig.boatCode;
  String api2Response;

  bool api2Success = false;
  for (int retry = 0; retry < 3 && !api2Success; retry++) {
    if (retry > 0) {
      Serial.printf("[CONFIG] 🔄 API2: 重试%d次获取心率设备列表\n", retry);

      vTaskDelay(pdMS_TO_TICKS(500));
      esp_task_wdt_reset();
      vTaskDelay(pdMS_TO_TICKS(500));
      esp_task_wdt_reset();
    }

    if (makeHttpRequestKeepAlive(api2Endpoint, api2Response)) {
      if (parseRowerListResponse(api2Response)) {
        rowerListStatus = CONFIG_SUCCESS;
        Serial.printf("[CONFIG]  API2: 心率设备列表获取成功 (%d个设置)\n",
                      rowerList.size());
        api2Success = true;
      } else {
        Serial.printf("[CONFIG] API2: JSON解析失败 (尝试%d/3)\n", retry + 1);
        if (retry == 2) {
          rowerListStatus = CONFIG_FAILED;
        }
      }
    } else {
      Serial.printf("[CONFIG]  API2: HTTP请求失败 (尝试%d/3)\n", retry + 1);
      if (retry == 2) {
        rowerListStatus = CONFIG_FAILED;
      }
    }
  }

  updateScreen4Display();
  vTaskDelay(pdMS_TO_TICKS(100));

  Serial.println("[CONFIG] 📡 API3: 获取MQTT配置");
  mqttConfigStatus = CONFIG_LOADING;
  updateScreen4Display();

  String api3Endpoint =
      String(PLATFORM_API_PATH) +
      "/basic/param/getParamByTenant?tenantCode=" + deviceConfig.tenantCode;
  String api3Response;

  if (makeHttpRequestKeepAlive(api3Endpoint, api3Response)) {
    if (parseMQTTConfigResponse(api3Response)) {
      mqttConfigStatus = CONFIG_SUCCESS;
      Serial.println("[CONFIG] API3: MQTT配置获取成功");
    } else {
      Serial.println("[CONFIG]  API3: JSON解析失败");
      mqttConfigStatus = CONFIG_FAILED;
      allSuccess = false;
    }
  } else {
    Serial.println("[CONFIG] API3: HTTP请求失败");
    mqttConfigStatus = CONFIG_FAILED;
    allSuccess = false;
  }

  hybridHttpClient->stop();
  xSemaphoreGive(serial4GMutex);

  updateScreen4Display();

  if (allSuccess && deviceConfig.isValid && mqttConfig.isValid) {
    // API 获取成功，保存到 NVS 供下次快速启动
    saveConfigToNVS();
    return true;
  } else {
    Serial.println("[CONFIG] ⚠️ 部分配置加载失败");
    return false;
  }
}

bool ConfigManager::makeHttpRequestKeepAlive(const String &endpoint,
                                             String &response) {
  Serial.printf("[HTTP] 📤 Keep-Alive请求: %s\n", endpoint.c_str());

  hybridHttpClient->print("GET " + endpoint + " HTTP/1.1\r\n");
  hybridHttpClient->print("Host: " + platformHost + "\r\n");
  hybridHttpClient->print("Accept: application/json\r\n");
  hybridHttpClient->print("Connection: keep-alive\r\n\r\n");

  response = "";
  unsigned long startTime = millis();

  while (millis() - startTime < 8000) {

    while (hybridHttpClient->available()) {
      char c = hybridHttpClient->read();
      response += c;
    }

    if (response.indexOf("\r\n\r\n") != -1 && response.length() > 100) {

      int headerEnd = response.indexOf("\r\n\r\n");
      String headers = response.substring(0, headerEnd);

      if (headers.indexOf("Content-Length:") != -1) {

        int clPos = headers.indexOf("Content-Length:");
        int clEnd = headers.indexOf("\r\n", clPos);
        String clStr = headers.substring(clPos + 15, clEnd);
        clStr.trim();
        int contentLength = clStr.toInt();

        int bodyStart = headerEnd + 4;
        int currentBodyLength = response.length() - bodyStart;

        if (currentBodyLength >= contentLength) {
          Serial.printf("[HTTP]  完整响应接收完成: %d字节\n",
                        response.length());
          break;
        }
      } else if (headers.indexOf("chunked") != -1) {

        if (response.endsWith("0\r\n\r\n")) {
          Serial.printf("[HTTP]  Chunked响应接收完成: %d字节\n",
                        response.length());
          break;
        }
      } else {

        if (!hybridHttpClient->connected()) {
          Serial.printf("[HTTP]  连接关闭，响应接收完整: %d字节\n",
                        response.length());
          break;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(50));
    esp_task_wdt_reset();
  }

  if (response.length() == 0) {
    Serial.println("[HTTP] 未收到任何数据");
    return false;
  }

  int bodyStart = response.indexOf("\r\n\r\n");
  if (bodyStart != -1) {
    String body = response.substring(bodyStart + 4);
    int jsonStart = body.indexOf("{");
    int jsonEnd = body.lastIndexOf("}");
    if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
      response = body.substring(jsonStart, jsonEnd + 1);
      Serial.printf("[HTTP] JSON提取成功: %d字节\n", response.length());
      return true;
    }
  }

  Serial.println("[HTTP]  JSON提取失败");
  return false;
}

bool ConfigManager::safeReloadPlatformAddress(const String &newAddress) {
  Serial.println("\n[CONFIG] ===== 开始安全地址重载 =====");

  if (currentReloadPhase != RELOAD_IDLE) {
    Serial.printf("[CONFIG] 地址重载正在进行 (阶段: %d)，请勿重复调用\n",
                  currentReloadPhase);
    Serial.println("[CONFIG] 提示：请等待当前重载完成后再");
    return false;
  }
  reloadCancelRequested = false;

  int colonIndex = newAddress.indexOf(':');
  if (colonIndex == -1) {
    Serial.println("[CONFIG] 地址格式错误：缺失':'");
    return false;
  }

  String host = newAddress.substring(0, colonIndex);
  String portStr = newAddress.substring(colonIndex + 1);
  host.trim();
  portStr.trim();

  int port = portStr.toInt();
  if (host.isEmpty() || port <= 0 || port > 65535) {
    Serial.println("[CONFIG]  地址格式错误：IP或端口无");
    return false;
  }

  if (host == platformHost && port == platformPort) {
    Serial.println("[CONFIG] ⚠️  新地址与当前地址相同，跳过重");
    return true;
  }

  pendingPlatformAddress = newAddress;
  Serial.printf("[CONFIG] 新地址验证通过: %s\n", newAddress.c_str());

  currentReloadPhase = RELOAD_STOPPING_TASKS;
  Serial.println("[CONFIG] [1/4] 停止依赖任务...");

  extern TaskHandle_t mqttTaskHandle;
  bool mqttTaskRunning = (mqttTaskHandle != nullptr);

  if (mqttTaskRunning) {

    extern bool requestNetworkSuspend();
    if (!requestNetworkSuspend()) {
      Serial.println("[CONFIG] ⚠️  MQTT任务暂停超时，但继续重载");
    }

    extern DataFlowManager dataFlow;
    bool dataFlowReady = dataFlow.isInitialized();
    if (dataFlowReady) {
      dataFlow.pauseDataFlow();
      Serial.println("[CONFIG]   - DataFlow 已暂停");
    } else {
      Serial.println("[CONFIG]   - DataFlow 未初始化，跳过暂停");
    }

    stopConfigLoading();
    Serial.println("[CONFIG]   - 配置加载任务已停止");

    extern bool disconnectPPPCompletely();
    disconnectPPPCompletely();
    Serial.println("[CONFIG]   - PPP连接已断开，资源已清理");
  } else {

    Serial.println("[CONFIG]   - MQTT任务未启动，跳过网络断开");
    stopConfigLoading();
    Serial.println("[CONFIG]   - 配置加载任务已停");
  }

  currentReloadPhase = RELOAD_WAITING_RELEASE;
  Serial.println("[CONFIG] [2/4] 等待资源释放...");

  extern SemaphoreHandle_t serial4GMutex;
  unsigned long waitStart = millis();
  const unsigned long maxWaitMs = 10000;

  while (millis() - waitStart < maxWaitMs) {

    if (serial4GMutex &&
        xSemaphoreTake(serial4GMutex, pdMS_TO_TICKS(100)) == pdTRUE) {

      xSemaphoreGive(serial4GMutex);
      Serial.println("[CONFIG]   - serial4GMutex 已释放");
      break;
    }

    if ((millis() - waitStart) % 1000 < 100) {
      Serial.printf("[CONFIG]   - 等待串口互斥锁释... %lu/%lu秒\n",
                    (millis() - waitStart) / 1000, maxWaitMs / 1000);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    esp_task_wdt_reset();
  }

  if (millis() - waitStart >= maxWaitMs) {
    Serial.println("[CONFIG]  等待串口互斥锁超时，放弃重载");
    currentReloadPhase = RELOAD_IDLE;
    pendingPlatformAddress = "";

    if (mqttTaskRunning) {
      extern DataFlowManager dataFlow;
      bool dataFlowReady = dataFlow.isInitialized();
      if (dataFlowReady) {
        dataFlow.resumeDataFlow();
        Serial.println("[CONFIG]   - DataFlow 已恢复");
      } else {
        Serial.println("[CONFIG]   - DataFlow 未初始化，跳过恢复");
      }

      extern bool requestNetworkResume();
      requestNetworkResume();
      Serial.println("[CONFIG]   - MQTT任务已恢复");
    }

    return false;
  }

  vTaskDelay(pdMS_TO_TICKS(500));
  esp_task_wdt_reset();

  Serial.println("[CONFIG]   - 所有资源已释放");

  currentReloadPhase = RELOAD_UPDATING_CONFIG;
  Serial.println("[CONFIG] [3/4] 更新平台地址...");

  closeHttpConnection();

  extern bool configLoadingInProgress;
  configLoadingInProgress = false;

  platformHost = host;
  platformPort = (uint16_t)port;

  savePlatformAddressToStorage();

  refreshConfig();

  updateScreen4Display();

  Serial.printf("[CONFIG]   - 平台地址已更改: %s:%d\n", platformHost.c_str(),
                platformPort);

  currentReloadPhase = RELOAD_RESTARTING;
  Serial.println("[CONFIG] [4/4] 重启任务...");

  Serial.println("[CONFIG]   - 开始重新拨号PPP...");

  extern bool runPppDialStep(uint32_t timeoutMs, int maxRetries,
                             struct NetworkStageState &state);
  struct NetworkStageState dialState;
  dialState.stage = (enum NetworkStage)1;
  dialState.attempt = 0;
  dialState.success = false;

  bool waitCancelled = false;
  bool reloadSuccess = false;
  if (runPppDialStep(60000, 3, dialState)) {
    Serial.println("[CONFIG]   - PPP重新拨号成功");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_task_wdt_reset();

    Serial.println("[CONFIG]   - 重新获取配置...");
    startConfigLoading();

    unsigned long configWaitStart = millis();
    const unsigned long configMaxWait = 10000;

    while (millis() - configWaitStart < configMaxWait) {
      if (reloadCancelRequested) {
        Serial.println("[CONFIG]   检测到取消请求，终止配置等");
        waitCancelled = true;
        break;
      }
      if (isConfigReady()) {
        Serial.println("[CONFIG]   配置加载成功");
        reloadSuccess = true;
        break;
      }

      if ((millis() - configWaitStart) % 2000 < 100) {
        Serial.printf("[CONFIG]   - 等待配置... %lu/%lu秒\n",
                      (millis() - configWaitStart) / 1000,
                      configMaxWait / 1000);
      }

      vTaskDelay(pdMS_TO_TICKS(500));
      esp_task_wdt_reset();
    }

    if (waitCancelled) {
      stopConfigLoading();
      closeHttpConnection();
      Serial.println("[CONFIG] ⚠️  配置加载被取消，准备切换新地址");
    } else if (!reloadSuccess) {
      Serial.println("[CONFIG] ⚠️  配置加载超时，但继续恢复任务");
    }

  } else {
    Serial.println("[CONFIG] ⚠️  PPP重新拨号失败，地址已保存，等待网络任务重试");
  }

  if (mqttTaskRunning) {
    extern DataFlowManager dataFlow;
    bool dataFlowReady = dataFlow.isInitialized();
    if (dataFlowReady) {
      if (dataFlowReady) {
        dataFlow.resumeDataFlow();
        Serial.println("[CONFIG]   - DataFlow 已恢复");
      }
    } else {
      Serial.println("[CONFIG]   - DataFlow 未初始化，跳过恢复");
    }

    extern bool requestNetworkResume();
    requestNetworkResume();

    extern int mqttRetryCount;
    extern bool mqttGracefullyDegraded;
    mqttRetryCount = 0;
    mqttGracefullyDegraded = false;
    Serial.println("[CONFIG]   - MQTT任务已恢复，将自动重复");
  } else {
    Serial.println("[CONFIG]   - MQTT task not running, attempting to start");
    if (!startMqttTaskIfReady("[CONFIG]")) {
      Serial.println("[CONFIG]   - MQTT task start failed or config not ready, "
                     "waiting for network task retry");
    }
  }

  reloadCancelRequested = false;
  reloadSuccess = reloadSuccess || isConfigReady();
  currentReloadPhase = RELOAD_IDLE;
  pendingPlatformAddress = "";

  Serial.println("[CONFIG] ===== 安全地址重载完成 =====");
  Serial.printf("[CONFIG] 新平台地址: %s:%d\n\n", platformHost.c_str(),
                platformPort);

  return reloadSuccess;
}
