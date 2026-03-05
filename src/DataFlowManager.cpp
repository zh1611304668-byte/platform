#include "DataFlowManager.h"
#include "BluetoothManager.h"
#include "ConfigManager.h"
#include "MQTTManager.h"
#include "TrainingMode.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <ArduinoJson.h>

DataFlowManager dataFlow;
DataFlowMonitor dataFlowMonitor;

DataFlowManager::DataFlowManager()
    : sensorMutex(nullptr), uploadMutex(nullptr), configMutex(nullptr),
      isInitializing(false), networkAvailable(false), maxQueueSize(500) {
  memset(&stats, 0, sizeof(stats));
  stats.lastResetTime = millis();
}

DataFlowManager::~DataFlowManager() {
  if (sensorMutex)
    vSemaphoreDelete(sensorMutex);
  if (uploadMutex)
    vSemaphoreDelete(uploadMutex);
  if (configMutex)
    vSemaphoreDelete(configMutex);
}

bool DataFlowManager::begin() {
  isInitializing = true;

  sensorMutex = xSemaphoreCreateMutex();
  uploadMutex = xSemaphoreCreateMutex();
  configMutex = xSemaphoreCreateMutex();

  if (!sensorMutex || !uploadMutex || !configMutex) {
    Serial.println("[DataFlow] 互斥锁创建失败");
    isInitializing = false;
    return false;
  }

  BaseType_t result = xTaskCreatePinnedToCore(
      dataProcessTask, "DataProcessTask", 6144, this, 1, nullptr, 1);

  if (result != pdPASS) {
    Serial.println("[DataFlow] 数据处理任务创建失败");
    isInitializing = false;
    return false;
  }

  isInitializing = false;
  Serial.println("[DataFlow] 数据流管理器初始化完成");
  return true;
}

bool DataFlowManager::pushSensorData(const String &data, DataType type) {
  if (!sensorMutex) {
    Serial.println("[DataFlow] 传感器互斥锁未初始化，推送传感器数据失败");
    return false;
  }

  if (xSemaphoreTake(sensorMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!isQueueFull(sensorQueue)) {
      DataPacket packet(type, data, "", PRIORITY_NORMAL, false);
      sensorQueue.push(packet);
      stats.totalProcessed++;
      xSemaphoreGive(sensorMutex);
      return true;
    }
    xSemaphoreGive(sensorMutex);
  }
  return false;
}

bool DataFlowManager::pushTrainingData(const String &data,
                                       const String &topic) {
  if (!uploadMutex) {
    Serial.println("[DataFlow] 上传互斥锁未初始化，推送训练数据失败");
    return false;
  }

  if (xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!isQueueFull(uploadQueue)) {
      DataPacket packet(DATA_TRAINING, data, topic, PRIORITY_HIGH, true);
      uploadQueue.push(packet);
      xSemaphoreGive(uploadMutex);
      return true;
    }
    xSemaphoreGive(uploadMutex);
  }
  return false;
}

bool DataFlowManager::pushHeartRateData(const String &data,
                                        const String &topic) {
  if (!uploadMutex) {
    Serial.println("[DataFlow] 上传互斥锁未初始化，推送心率数据失败");
    return false;
  }

  if (xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!isQueueFull(uploadQueue)) {
      DataPacket packet(DATA_HEARTRATE, data, topic, PRIORITY_NORMAL, true);
      uploadQueue.push(packet);
      xSemaphoreGive(uploadMutex);
      return true;
    }
    xSemaphoreGive(uploadMutex);
  }
  return false;
}

bool DataFlowManager::pushStatusData(const String &data, const String &topic) {
  if (!uploadMutex) {
    Serial.println("[DataFlow] 上传互斥锁未初始化，推送状态数据失败");
    return false;
  }

  if (xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!isQueueFull(uploadQueue)) {
      DataPacket packet(DATA_STATUS, data, topic, PRIORITY_LOW, true);
      uploadQueue.push(packet);
      xSemaphoreGive(uploadMutex);
      return true;
    }
    xSemaphoreGive(uploadMutex);
  }
  return false;
}

bool DataFlowManager::popUploadData(DataPacket &packet) {
  if (!uploadMutex) {
    Serial.println("[DataFlow] 上传互斥锁未初始化，无法弹出上传数据");
    return false;
  }

  if (xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (dataFlowPaused) {
      xSemaphoreGive(uploadMutex);
      return false;
    }

    if (!uploadQueue.empty()) {
      packet = uploadQueue.front();
      uploadQueue.pop();
      xSemaphoreGive(uploadMutex);
      return true;
    }
    xSemaphoreGive(uploadMutex);
  }
  return false;
}
bool DataFlowManager::popConfigData(DataPacket &packet) {
  if (!configMutex) {
    Serial.println("[DataFlow] 配置互斥锁未初始化，无法弹出配置数据");
    return false;
  }

  if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    if (!configQueue.empty()) {
      packet = configQueue.front();
      configQueue.pop();
      xSemaphoreGive(configMutex);
      return true;
    }
    xSemaphoreGive(configMutex);
  }
  return false;
}

bool DataFlowManager::pushConfigUpdate(const String &configData,
                                       DataType configType) {
  if (!configMutex) {
    Serial.println("[DataFlow] 配置互斥锁未初始化，推送配置数据失败");
    return false;
  }

  if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    if (!isQueueFull(configQueue)) {
      DataPacket packet(configType, configData, "", PRIORITY_HIGH, false);
      configQueue.push(packet);
      xSemaphoreGive(configMutex);
      return true;
    }
    xSemaphoreGive(configMutex);
  }
  return false;
}

void DataFlowManager::setNetworkStatus(bool available) {
  networkAvailable = available;
}

size_t DataFlowManager::getSensorQueueSize() const {
  return sensorQueue.size();
}

size_t DataFlowManager::getUploadQueueSize() const {
  return uploadQueue.size();
}

void DataFlowManager::getStatistics(Statistics &outStats) const {
  outStats = stats;
}

void DataFlowManager::resetStatistics() {
  memset(&stats, 0, sizeof(stats));
  stats.lastResetTime = millis();
}

void DataFlowManager::incrementUploadCount() { stats.totalUploaded++; }

void DataFlowManager::incrementFailureCount() { stats.uploadFailures++; }

void DataFlowManager::incrementProcessedCount() { stats.totalProcessed++; }

bool DataFlowManager::isQueueFull(const std::queue<DataPacket> &queue) const {
  return queue.size() >= maxQueueSize;
}

void DataFlowManager::dataProcessTask(void *pvParameters) {
  DataFlowManager *manager = static_cast<DataFlowManager *>(pvParameters);

  Serial.println("[DataFlow] 数据处理任务启动");

  unsigned long lastStatsPrint = 0;

  while (true) {
    manager->processHighPriorityData();

    if (millis() - lastStatsPrint > 60000) {
      lastStatsPrint = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

void DataFlowManager::processHighPriorityData() {
  DataPacket packet;

  if (popConfigData(packet)) {
    processDataPacket(packet);
  }

  if (popUploadData(packet)) {
    if (networkAvailable) {
      stats.totalUploaded++;
    } else {
      Serial.println("[DataFlow] 网络不可用，数据丢弃");
      stats.uploadFailures++;
    }
  }
}

void DataFlowManager::processDataPacket(const DataPacket &packet) {
  switch (packet.type) {
  case DATA_CONFIG:
    Serial.printf("[DataFlow] 处理配置数据: %s\n", packet.payload.c_str());
    break;

  case DATA_TRAINING:
  case DATA_HEARTRATE:
  case DATA_STATUS:
    break;

  default:
    Serial.printf("[DataFlow] 未知数据类型: %d\n", packet.type);
    break;
  }
}

bool DataFlowManager::validateDataPacket(const DataPacket &packet) {
  if (packet.payload.isEmpty()) {
    return false;
  }

  if (packet.type != DATA_CONFIG) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, packet.payload);
    if (error) {
      Serial.printf("[DataFlow] JSON验证失败: %s\n", error.c_str());
      return false;
    }
  }

  return true;
}

void DataFlowMonitor::setState(DataFlowState newState) {
  if (currentState != newState) {
    currentState = newState;
    stateChangeTime = millis();

    static unsigned long lastLogTime = 0;
    static DataFlowState lastLoggedState = FLOW_IDLE;
    static unsigned long stateChangeCount = 0;
    static unsigned long lastSummaryTime = 0;

    if (newState != lastLoggedState) {
      stateChangeCount++;

      if (millis() - lastLogTime > 2000) {
        const char *stateNames[] = {"IDLE", "COLLECTING", "PROCESSING",
                                    "UPLOADING", "ERROR"};

        if (stateChangeCount > 1) {
          // Serial.printf("[DataFlow] 状态变更: %s (%lu次变更)\n",
          //              stateNames[newState], stateChangeCount);
        } else {
          // Serial.printf("[DataFlow] 状态变更: %s\n", stateNames[newState]);
        }

        lastLogTime = millis();
        lastLoggedState = newState;
        stateChangeCount = 0;
      }
    }

    if (millis() - lastSummaryTime > 30000 && stateChangeCount > 0) {
      // Serial.printf("[DataFlow] 30秒汇总: %lu次状态变更\n",
      // stateChangeCount);
      stateChangeCount = 0;
      lastSummaryTime = millis();
    }
  }
}

void DataFlowMonitor::updateProcessingRate(unsigned long packetsProcessed) {
  static unsigned long lastUpdate = 0;
  static unsigned long lastPacketCount = 0;

  unsigned long currentTime = millis();
  if (currentTime - lastUpdate >= 1000) {
    processingRate = packetsProcessed - lastPacketCount;
    lastPacketCount = packetsProcessed;
    lastUpdate = currentTime;
  }
}

void DataFlowMonitor::printStatus() const {
  Serial.printf("[DataFlow] 状态: %d, 持续时间: %lu ms, 处理速率: %lu pps\n",
                currentState, getStateTime(), processingRate);
}

bool DataFlowManager::collectAllCurrentData(CollectionResult &result) {
  if (isInitializing) {
    Serial.println("[DataFlow] 警告：正在初始化中，跳过数据收集");
    return false;
  }

  LockGuard lock(sensorMutex);

  result.hasStatusData = false;
  result.hasTrainingData = false;
  result.hasHeartRateData = false;
  result.statusPayload = "";
  result.trainingPayload = "";
  result.heartRatePayload = "";
  result.collectionTime = millis();

  unsigned long currentTime = millis();
  bool useCache = false;

  if (currentTime - dataCache.statusTime < DataCache::CACHE_VALID_MS &&
      !dataCache.statusPayload.isEmpty()) {
    result.statusPayload = dataCache.statusPayload;
    result.hasStatusData = true;
    useCache = true;
  } else {
    extern String buildStatusPayload();
    result.statusPayload = buildStatusPayload();
    result.hasStatusData = !result.statusPayload.isEmpty();
    if (result.hasStatusData) {
      dataCache.statusPayload = result.statusPayload;
      dataCache.statusTime = currentTime;
    }
  }

  extern TrainingMode training;
  if (training.isActive() && training.getElapsedSeconds() > 0) {
    if (currentTime - dataCache.trainingTime < DataCache::CACHE_VALID_MS &&
        !dataCache.trainingPayload.isEmpty()) {
      result.trainingPayload = dataCache.trainingPayload;
      result.hasTrainingData = true;
      useCache = true;
    } else {
      extern String generateTrainingDataJSON(int customStrokeCount);
      result.trainingPayload = generateTrainingDataJSON(-1);
      result.hasTrainingData = !result.trainingPayload.isEmpty();
      if (result.hasTrainingData) {
        dataCache.trainingPayload = result.trainingPayload;
        dataCache.trainingTime = currentTime;
      }
    }
  }

  if (currentTime - dataCache.heartRateTime < DataCache::CACHE_VALID_MS &&
      !dataCache.heartRatePayload.isEmpty()) {
    result.heartRatePayload = dataCache.heartRatePayload;
    result.hasHeartRateData = true;
    useCache = true;
  } else {
    extern String buildHeartRatePayload();
    String heartRateData = buildHeartRatePayload();
    if (!heartRateData.isEmpty() && heartRateData.indexOf("\"hr\":[") != -1 &&
        heartRateData.indexOf("\"hr\":[]") == -1) {
      result.heartRatePayload = heartRateData;
      result.hasHeartRateData = true;
      dataCache.heartRatePayload = heartRateData;
      dataCache.heartRateTime = currentTime;
    }
  }

  result.fromCache = useCache;
  if (useCache) {
    result.cacheAge =
        currentTime - std::min({dataCache.statusTime, dataCache.trainingTime,
                                dataCache.heartRateTime});
  }

  return true;
}

void DataFlowManager::pauseDataFlow() {
  if (isInitializing) {
    Serial.println("[DataFlow] 警告：正在初始化中，跳过暂停");
    return;
  }

  if (!uploadMutex) {
    Serial.println("[DataFlow] 暂停失败：uploadMutex 尚未创建");
    return;
  }

  if (xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    dataFlowPaused = true;
    xSemaphoreGive(uploadMutex);
    Serial.println("[DataFlow] 数据流已暂停（保留队列）");
  } else {
    Serial.println("[DataFlow] 暂停失败：无法获取互斥锁");
  }
}

void DataFlowManager::resumeDataFlow() {
  if (isInitializing) {
    Serial.println("[DataFlow] 警告：正在初始化中，跳过恢复");
    return;
  }

  if (!uploadMutex) {
    Serial.println("[DataFlow] 恢复失败：uploadMutex 尚未创建");
    return;
  }

  if (xSemaphoreTake(uploadMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
    dataFlowPaused = false;
    xSemaphoreGive(uploadMutex);
    Serial.println("[DataFlow] 数据流已恢复");
  } else {
    Serial.println("[DataFlow] 恢复失败：无法获取互斥锁");
  }
}
