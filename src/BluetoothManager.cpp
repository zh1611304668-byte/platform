#include "BluetoothManager.h"
#include "ConfigManager.h"
#include "TrainingMode.h" // 添加训练模式头文件
#include "UIManager.h"    // 添加UI管理器头文件
#include "esp_task_wdt.h" // 添加看门狗头文件
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <NimBLEDevice.h>


// 声明外部函数和变量
extern void safeLabelUpdate(lv_obj_t *label, const char *text,
                            const char *labelName);
extern TrainingMode training; // 声明全局训练模式对象

using namespace BT;

namespace {
// 常量定义
const int SCAN_DURATION = 2;
const int SCAN_INTERVAL = 60000;           // Screen1等其他界面60秒扫描一次
const int CONTINUOUS_SCAN_INTERVAL = 8000; // Screen3持续扫描间隔8秒
const int DATA_TIMEOUT = 15000;
const int MAX_CONNECTION_ATTEMPTS = 3;

BLEScan *pBLEScan = nullptr;
TaskHandle_t bleTaskHandle = nullptr;
BLEUUID hrServiceUUID((uint16_t)0x180D);
BLEUUID hrCharUUID((uint16_t)0x2A37);
// Battery Service/Characteristic
BLEUUID battServiceUUID((uint16_t)0x180F);
BLEUUID battLevelUUID((uint16_t)0x2A19);

// 简化的设备名称处理（仅用于回退情况）
String makeDisplayName(const char *raw) {
  if (!raw)
    return String("");
  String s(raw);
  s.trim();
  return s;
}

volatile bool scanning = false;
volatile int connectReqIdx = -1;
volatile bool manualScanRequested = false;
volatile bool autoScanEnabled = true;
volatile bool continuousScanMode = false;
volatile unsigned long lastContinuousScan = 0;
volatile bool autoConnectRequested = false;
static hr_device_t *active = nullptr;

// 预设设备表（扩展至7个）
hr_device_t presets[NUM_PRESETS] = {};

// 互斥锁保护presets数组
SemaphoreHandle_t presetsMutex = xSemaphoreCreateMutex();

// 电量通知回调
void batteryNotifyCallback(BLERemoteCharacteristic *ch, uint8_t *data,
                           size_t len, bool) {
  if (len < 1)
    return;
  BLEClient *srcClient = ch->getRemoteService()->getClient();

  // 使用互斥锁保护presets数组访问
  if (xSemaphoreTake(presetsMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    for (auto &d : presets) {
      if (d.client == srcClient) {
        d.batteryLevel = data[0];
        break;
      }
    }
    xSemaphoreGive(presetsMutex);
  }
}

// 新增：设备名称缓冲区池，避免动态内存分配
char deviceNamePool[NUM_PRESETS][32]; // 每个设备最多31字符 + 结束符
bool namePoolUsed[NUM_PRESETS] = {false, false, false, false,
                                  false, false, false};

// 连接数量变化标志（事件驱动）
volatile bool connectionCountChanged = false;
volatile int lastKnownConnectionCount = 0;

volatile bool disconnectEvents[NUM_PRESETS] = {false};
volatile bool cleanupRequested = false;

void setDevState(int i, dev_state_t s) {
  presets[i].state = s;
  presets[i].connected = (s == DEV_CONNECTED);
  presets[i].connecting = (s == DEV_CONNECTING);
}

void markConnectionCountDirtyImpl() {
  connectionCountChanged = true;
  lastKnownConnectionCount = -1;
}

void setDisconnectedState(int i) {
  presets[i].connected = false;
  presets[i].connecting = false;
  presets[i].uploaded = false;
  presets[i].lastHeartRate = 0;
  presets[i].batteryLevel = -1;
  presets[i].lastDataTime = 0;
  setDevState(i, DEV_IDLE);
}

void safeDestroyClient(int i) {
  if (!presets[i].client) {
    return;
  }
  NimBLEClient *c = presets[i].client;
  presets[i].client = nullptr;
  try {
    if (c->isConnected()) {
      c->disconnect();
    }
  } catch (...) {}
  NimBLEDevice::deleteClient(c);
}

void queueDisconnectEvent(int i) {
  if (i >= 0 && i < NUM_PRESETS) {
    disconnectEvents[i] = true;
  }
}

bool hasConnectingDevice() {
  for (int i = 0; i < NUM_PRESETS; ++i) {
    if (presets[i].state == DEV_CONNECTING || presets[i].connecting) {
      return true;
    }
  }
  return false;
}

bool hasUnconnectedApiDevice() {
  if (!configManager.isRowerListReady()) {
    return true;
  }
  const auto &rowerList = configManager.getRowerList();
  for (const auto &rower : rowerList) {
    String btAddr = rower.btAddr;
    btAddr.trim();
    if (btAddr.isEmpty() || btAddr.equalsIgnoreCase("null")) {
      continue;
    }

    bool isConnected = false;
    for (int j = 0; j < NUM_PRESETS; ++j) {
      if (presets[j].connected &&
          String(presets[j].address).equalsIgnoreCase(btAddr)) {
        isConnected = true;
        break;
      }
    }
    if (!isConnected) {
      return true;
    }
  }
  return false;
}

bool hasUndiscoveredApiDevice() {
  if (!configManager.isRowerListReady()) {
    return true;
  }

  const auto &rowerList = configManager.getRowerList();
  for (const auto &rower : rowerList) {
    String btAddr = rower.btAddr;
    btAddr.trim();
    if (btAddr.isEmpty() || btAddr.equalsIgnoreCase("null")) {
      continue;
    }

    bool discovered = false;
    for (int j = 0; j < NUM_PRESETS; ++j) {
      if (presets[j].address[0] != '\0' &&
          String(presets[j].address).equalsIgnoreCase(btAddr)) {
        discovered = presets[j].found;
        break;
      }
    }

    if (!discovered) {
      return true;
    }
  }
  return false;
}

// 前向声明：HR 事件推送函数（定义在文件后部）
void pushHREventInternal(int idx, int hr);

// 安全回调
class MySecurity : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() { return 0; }
  void onPassKeyNotify(uint32_t pass_key) {}
  bool onConfirmPIN(uint32_t pin) { return true; }
  bool onSecurityRequest() { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) {
    if (desc && desc->sec_state.encrypted)
      Serial.println("[SEC] Pairing success");
    else
      Serial.println("[SEC] Pairing failed");
  }
};

struct MyClientCB : public BLEClientCallbacks {
  int devIdx = -1;
  void onConnect(BLEClient *pclient) override {}
  void onDisconnect(BLEClient *pclient) override { queueDisconnectEvent(devIdx); }
};

MyClientCB cbPool[NUM_PRESETS];

// 心率通知回调
void notifyCallback(BLERemoteCharacteristic *ch, uint8_t *data, size_t len,
                    bool) {
  if (len < 2)
    return;
  uint8_t flags = data[0];
  int hr = (flags & 0x01 && len >= 3) ? ((data[2] << 8) | data[1]) : data[1];

  // 心率值范围检查
  if (hr < 30 || hr > 250)
    return; // 正常心率范围：30-250

  BLEClient *srcClient = ch->getRemoteService()->getClient();
  hr_device_t *srcDev = nullptr;

  for (auto &d : presets) {
    if (d.client == srcClient) {
      srcDev = &d;
      break;
    }
  }

  if (!srcDev)
    return;

  srcDev->lastHeartRate = hr;
  srcDev->lastDataTime = millis();
  srcDev->dataCount++;

  // 将心率事件推入 UI 事件缓冲，由主循环调用 pollUIEvents() 处理并更新 LVGL。
  int idx = srcDev - presets; // 计算设备索引
  if (idx >= 0 && idx < NUM_PRESETS) {
    pushHREventInternal(idx, hr);
  }
}

// 扫描回调
class PresetScanCb : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice *advertisedDevice) override {
    if (!advertisedDevice || !advertisedDevice->haveName())
      return;

    String addr = advertisedDevice->getAddress().toString().c_str();

    // 检查是否在API心率带设备白名单中
    if (!BT::isAddressInWhitelist(addr))
      return;

    bool deviceExists = false;
    int freeSlot = -1;

    // 检查设备是否已存在
    for (int i = 0; i < NUM_PRESETS; ++i) {
      if (presets[i].address[0] != '\0' &&
          strcmp(presets[i].address, addr.c_str()) == 0) {
        deviceExists = true;
        presets[i].found = true;
        presets[i].addrType = advertisedDevice->getAddressType();
        break;
      }
      if (presets[i].address[0] == '\0' && freeSlot == -1) {
        freeSlot = i;
      }
    }

    // 添加新设备
    if (!deviceExists && freeSlot != -1) {
      // 从API获取设备信息
      String deviceCode, rowerName;
      if (BT::getDeviceInfoByAddress(addr, deviceCode, rowerName)) {
        // 处理null值：如果deviceCode是"null"或空，则显示"-"
        if (deviceCode.isEmpty() || deviceCode.equalsIgnoreCase("null")) {
          deviceCode = "-";
        }
        if (rowerName.isEmpty() || rowerName.equalsIgnoreCase("null")) {
          rowerName = "-";
        }
        // 构造显示名称：deviceCode(rowerName)
        String displayName = deviceCode + "(" + rowerName + ")";

        size_t nameLen = displayName.length();

        // 严格的设备名称长度检查，防止缓冲区溢出
        if (nameLen > 0 && nameLen <= 30) { // 最大30字符（留1个结束符）
          // 使用固定缓冲区池，避免动态内存分配
          if (!namePoolUsed[freeSlot]) {
            if (xSemaphoreTake(presetsMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
              return;
            }

            // 安全的字符串复制，确保不会溢出
            size_t copyLen = (nameLen < 30) ? nameLen : 30;
            memcpy(deviceNamePool[freeSlot], displayName.c_str(), copyLen);
            deviceNamePool[freeSlot][copyLen] = '\0'; // 确保结束符

            presets[freeSlot].name = deviceNamePool[freeSlot];
            namePoolUsed[freeSlot] = true;

            // 安全的地址复制
            size_t addrLen = addr.length();
            size_t maxAddrLen = sizeof(presets[freeSlot].address) - 1;
            size_t copyAddrLen = (addrLen < maxAddrLen) ? addrLen : maxAddrLen;
            memcpy(presets[freeSlot].address, addr.c_str(), copyAddrLen);
            presets[freeSlot].address[copyAddrLen] = '\0';

            presets[freeSlot].addrType = advertisedDevice->getAddressType();
            presets[freeSlot].found = true;
            xSemaphoreGive(presetsMutex);

            Serial.printf("[SCAN] Added: %s (%s)\n", presets[freeSlot].name,
                          presets[freeSlot].address);
          } else {
            Serial.printf("[SCAN] Name pool slot %d already used\n", freeSlot);
          }
        } else {
          Serial.printf(
              "[SCAN] Skipped device with invalid name length: %zu (max 30)\n",
              nameLen);
        }
      }
    }
  }
};

// 内部连接实现
bool internalConnect(int idx) {
  if (idx < 0 || idx >= NUM_PRESETS || !presets[idx].found)
    return false;

  auto &dev = presets[idx];
  if (dev.client && dev.client->isConnected()) {
    setDevState(idx, DEV_CONNECTED);
    return true;
  }
  setDevState(idx, DEV_CONNECTING);
  dev.connectionAttempts++;
  Serial.printf("[CON] Connecting %s | heap=%u min=%u\n", dev.name,
                heap_caps_get_free_size(MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));

  if (pBLEScan)
    pBLEScan->stop();

  // 清理旧连接
  if (!dev.client) {
    dev.client = BLEDevice::createClient();
  } else {
    try {
      if (dev.client->isConnected()) {
        dev.client->disconnect();
      }
    } catch (...) {}
  }

  BLEClient *client = dev.client;
  if (!client) {
    setDevState(idx, DEV_BACKOFF);
    dev.nextRetryAtMs = millis() +
        (unsigned long)min(30000UL, (1000UL << min(dev.connectionAttempts, 5)));
    markConnectionCountDirtyImpl();
    return false;
  }

  // 安全设置
  cbPool[idx].devIdx = idx;
  client->setClientCallbacks(&cbPool[idx], false);

  auto failExit = [&](const char *reason) {
    Serial.printf("[CON] Failed %s(%s): %s (attempt=%d)\n", dev.name,
                  dev.address, reason ? reason : "unknown",
                  dev.connectionAttempts);
    safeDestroyClient(idx);
    setDevState(idx, DEV_BACKOFF);
    dev.nextRetryAtMs = millis() +
        (unsigned long)min(30000UL, (1000UL << min(dev.connectionAttempts, 5)));
    markConnectionCountDirtyImpl();
  };

  BLEAddress addr(dev.address);
  if (!client->connect(addr, dev.addrType)) {
    failExit("connect");
    return false;
  }

  BLERemoteService *svc = nullptr;
  try {
    svc = client->getService(hrServiceUUID);
  } catch (...) {
  }

  if (!svc) {
    failExit("hr_service_0x180D_not_found");
    return false;
  }

  BLERemoteCharacteristic *ch = nullptr;
  try {
    ch = svc->getCharacteristic(hrCharUUID);
  } catch (...) {
  }

  if (!ch) {
    failExit("hr_char_0x2A37_not_found");
    return false;
  }

  if (ch->canNotify()) {
    try {
      ch->subscribe(true, notifyCallback, false);

      // 启用通知
      BLERemoteDescriptor *cccd = ch->getDescriptor(BLEUUID((uint16_t)0x2902));
      if (cccd) {
        uint8_t enableValue[2] = {0x01, 0x00};
        if (ch->canIndicate())
          enableValue[0] = 0x02;
        cccd->writeValue(enableValue, 2, true);
      }
    } catch (...) {
      // 通知注册失败，断开连接
      failExit("notify_subscribe_exception");
      return false;
    }
  } else {
    failExit("hr_char_not_notifiable");
    return false;
  }

  // 只有在所有步骤都成功后才标记为已连接
  setDevState(idx, DEV_CONNECTED);
  dev.lastDataTime = millis();
  dev.connectionAttempts = 0;
  dev.nextRetryAtMs = 0;
  dev.batteryLevel = -1; // 初始化为-1，表示尚未获取到电量数据

  // Battery Service: 读取一次电量并尝试订阅
  try {
    BLERemoteService *bSrv = client->getService(battServiceUUID);
    if (bSrv) {
      BLERemoteCharacteristic *bCh = bSrv->getCharacteristic(battLevelUUID);
      if (bCh) {
        if (bCh->canRead()) {
          std::string v = bCh->readValue();
          if (!v.empty())
            dev.batteryLevel = (uint8_t)v[0];
        }
        if (bCh->canNotify() || bCh->canIndicate()) {
          bCh->subscribe(true, batteryNotifyCallback, false);
          try {
            BLERemoteDescriptor *cccd =
                bCh->getDescriptor(BLEUUID((uint16_t)0x2902));
            if (cccd) {
              uint8_t enableValue[2] = {0x01, 0x00};
              if (bCh->canIndicate())
                enableValue[0] = 0x02;
              cccd->writeValue(enableValue, 2, true);
            }
          } catch (...) {
          }
        }
      }
    }
  } catch (...) {
  }

  // 如果是第一个连接的设备，自动设为上传源
  if (!active) {
    setUploadSource(&dev);
  }

  markConnectionCountDirtyImpl();
  Serial.printf("[CON] Connected %s | heap=%u min=%u\n", dev.name,
                heap_caps_get_free_size(MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  return true;
}

// 执行扫描和连接
void performScanAndConnect() {
  if (training.isActive()) {
    return;
  }
  if (!pBLEScan) {
    return;
  }

  auto tryConnectFromCache = [&]() -> bool {
    if (configManager.isRowerListReady()) {
      const auto &rowerList = configManager.getRowerList();
      for (size_t apiIdx = 0; apiIdx < rowerList.size(); apiIdx++) {
        const auto &rower = rowerList[apiIdx];
        String btAddr = rower.btAddr;
        btAddr.trim();
        if (btAddr.isEmpty() || btAddr.equalsIgnoreCase("null")) {
          continue;
        }

        bool alreadyConnected = false;
        for (int j = 0; j < NUM_PRESETS; ++j) {
          if (presets[j].connected &&
              String(presets[j].address).equalsIgnoreCase(btAddr)) {
            alreadyConnected = true;
            break;
          }
        }
        if (alreadyConnected) {
          continue;
        }

        for (int i = 0; i < NUM_PRESETS; ++i) {
          if (presets[i].found && !presets[i].connected && !presets[i].connecting &&
              presets[i].connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
            if (String(presets[i].address).equalsIgnoreCase(btAddr)) {
              internalConnect(i);
              return true;
            }
          }
        }
      }
    } else {
      for (int i = 0; i < NUM_PRESETS; ++i) {
        if (presets[i].found && !presets[i].connected && !presets[i].connecting &&
            presets[i].connectionAttempts < MAX_CONNECTION_ATTEMPTS) {
          internalConnect(i);
          return true;
        }
      }
    }
    return false;
  };

  if (tryConnectFromCache()) {
    return;
  }
  if (configManager.isRowerListReady() && !hasUndiscoveredApiDevice()) {
    return;
  }

  scanning = true;
  pBLEScan->clearResults();
  pBLEScan->start(SCAN_DURATION, false);
  scanning = false;

  (void)tryConnectFromCache();
}

void performAutoConnect() {
  Serial.println("[AUTO] 开始自动连接API设备");

  // 首先触发一次扫描以发现设备
  performScanAndConnect();

  Serial.printf("[AUTO] 自动连接完成，已连接设备数: %d\n",
                BT::getConnectedCount());
}

// 安全清理BLE客户端连接
void cleanupDevice(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= NUM_PRESETS) {
    return;
  }

  safeDestroyClient(deviceIndex);
  setDisconnectedState(deviceIndex);
  if (active == &presets[deviceIndex]) {
    setUploadSource(nullptr);
  }
  markConnectionCountDirtyImpl();
  Serial.printf("[CLEANUP] Device %d cleanup completed\n", deviceIndex);
}

void checkDataTimeouts() {
  unsigned long now = millis();
  static uint8_t disconnectProbeMisses[NUM_PRESETS] = {0};

  for (int i = 0; i < NUM_PRESETS; ++i) {
    if (presets[i].connected) {
      // 检查连接是否实际有效
      bool actuallyConnected = false;
      if (presets[i].client) {
        try {
          actuallyConnected = presets[i].client->isConnected();
        } catch (...) {
          actuallyConnected = false;
        }
      }

      // NimBLE在短时间窗口内可能返回瞬时false，这里做二次确认，避免误判。
      if (!actuallyConnected) {
        if (++disconnectProbeMisses[i] >= 2) {
          Serial.printf("[TIMEOUT] Device %s(%s) BLE disconnect confirmed\n",
                        presets[i].name, presets[i].address);
          queueDisconnectEvent(i);
          disconnectProbeMisses[i] = 0;
        }
        continue;
      }
      disconnectProbeMisses[i] = 0;

      // 数据超时检查：如果连接有效但长时间无数据，给出警告但不断开
      if (now - presets[i].lastDataTime > DATA_TIMEOUT) {
        // 增加数据超时时间到60秒，并且只输出警告，不断开连接
        if (now - presets[i].lastDataTime > 60000) { // 60秒无数据警告
          static unsigned long lastWarningTime[NUM_PRESETS] = {0};
          if (now - lastWarningTime[i] > 30000) { // 30秒警告一次
            Serial.printf("[WARNING] Device %s(%s) no data for %lu seconds\n",
                          presets[i].name, presets[i].address,
                          (now - presets[i].lastDataTime) / 1000);
            lastWarningTime[i] = now;
          }
        }
        // 不断开连接，让设备有机会恢复数据传输
      }
    }
  }
}

// 后台任务
void bleTask(void *) {
  Serial.println("[BT] BLE task started");

  unsigned long lastScanTime = 0;
  unsigned long lastTimeoutCheck = 0;

  while (true) {
    esp_task_wdt_reset();
    unsigned long now = millis();
    bool attemptedConnectThisLoop = false;

    for (int i = 0; i < NUM_PRESETS; ++i) {
      if (disconnectEvents[i]) {
        disconnectEvents[i] = false;
        safeDestroyClient(i);
        setDevState(i, DEV_BACKOFF);
        presets[i].connectionAttempts = max(presets[i].connectionAttempts, 1);
        presets[i].nextRetryAtMs = now +
            (unsigned long)min(30000UL, (1000UL << min(presets[i].connectionAttempts, 5)));
        if (active == &presets[i]) {
          active = nullptr;
        }
        markConnectionCountDirtyImpl();
        Serial.printf("[CB] Device %d disconnected -> BACKOFF\n", i);
      }
    }

    if (cleanupRequested) {
      cleanupRequested = false;
      for (int i = 0; i < NUM_PRESETS; ++i) {
        if (presets[i].client && !presets[i].client->isConnected()) {
          queueDisconnectEvent(i);
        }
      }
    }

    if (connectReqIdx >= 0 && !hasConnectingDevice()) {
      int req = connectReqIdx;
      connectReqIdx = -1;
      if (req >= 0 && req < NUM_PRESETS && presets[req].found &&
          !presets[req].connected) {
        internalConnect(req);
        attemptedConnectThisLoop = true;
        esp_task_wdt_reset();
      }
    }

    if (!attemptedConnectThisLoop) {
      for (int i = 0; i < NUM_PRESETS; ++i) {
        if (presets[i].state == DEV_BACKOFF && presets[i].found) {
          if (presets[i].connectionAttempts >= MAX_CONNECTION_ATTEMPTS) {
            setDevState(i, DEV_IDLE);
            continue;
          }
          if (now >= presets[i].nextRetryAtMs) {
            Serial.printf("[BT] BACKOFF retry device %d (%s) attempt=%d | heap=%u\n",
                          i, presets[i].name, presets[i].connectionAttempts,
                          heap_caps_get_free_size(MALLOC_CAP_8BIT));
            internalConnect(i);
            attemptedConnectThisLoop = true;
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(200));
            break;
          }
        }
      }
    }

    // Fast lane: once devices are discovered, chain-connect remaining devices
    // without waiting for the next scan interval.
    if (!attemptedConnectThisLoop && !hasConnectingDevice()) {
      if (configManager.isRowerListReady()) {
        const auto &rowerList = configManager.getRowerList();
        for (const auto &rower : rowerList) {
          String btAddr = rower.btAddr;
          btAddr.trim();
          if (btAddr.isEmpty() || btAddr.equalsIgnoreCase("null")) {
            continue;
          }

          bool alreadyConnected = false;
          for (int j = 0; j < NUM_PRESETS; ++j) {
            if (presets[j].connected &&
                String(presets[j].address).equalsIgnoreCase(btAddr)) {
              alreadyConnected = true;
              break;
            }
          }
          if (alreadyConnected) {
            continue;
          }

          for (int i = 0; i < NUM_PRESETS; ++i) {
            if (!presets[i].found || presets[i].connected || presets[i].connecting) {
              continue;
            }
            if (presets[i].state == DEV_BACKOFF ||
                presets[i].connectionAttempts >= MAX_CONNECTION_ATTEMPTS) {
              continue;
            }
            if (String(presets[i].address).equalsIgnoreCase(btAddr)) {
              internalConnect(i);
              attemptedConnectThisLoop = true;
              esp_task_wdt_reset();
              vTaskDelay(pdMS_TO_TICKS(150));
              break;
            }
          }
          if (attemptedConnectThisLoop) {
            break;
          }
        }
      } else {
        for (int i = 0; i < NUM_PRESETS; ++i) {
          if (!presets[i].found || presets[i].connected || presets[i].connecting) {
            continue;
          }
          if (presets[i].state == DEV_BACKOFF ||
              presets[i].connectionAttempts >= MAX_CONNECTION_ATTEMPTS) {
            continue;
          }
          internalConnect(i);
          attemptedConnectThisLoop = true;
          esp_task_wdt_reset();
          vTaskDelay(pdMS_TO_TICKS(150));
          break;
        }
      }
    }

    if (!attemptedConnectThisLoop && manualScanRequested) {
      performScanAndConnect();
      esp_task_wdt_reset();
      manualScanRequested = false;
      lastScanTime = now;
    }
    else if (!attemptedConnectThisLoop && autoConnectRequested) {
      performAutoConnect();
      esp_task_wdt_reset();
      autoConnectRequested = false;
      lastScanTime = now;
    }
    else if (!attemptedConnectThisLoop &&
             continuousScanMode && !scanning && !hasConnectingDevice() &&
             (now - lastContinuousScan > CONTINUOUS_SCAN_INTERVAL)) {
      if (configManager.isRowerListReady() && hasUndiscoveredApiDevice()) {
        performScanAndConnect();
        esp_task_wdt_reset();
        lastContinuousScan = now;
      } else {
        lastContinuousScan = now - CONTINUOUS_SCAN_INTERVAL + 3000;
      }
    }
    else if (!attemptedConnectThisLoop &&
             autoScanEnabled && !continuousScanMode && !scanning &&
             !hasConnectingDevice() &&
             (now - lastScanTime > SCAN_INTERVAL)) {
      if (configManager.isRowerListReady()) {
        if (hasUndiscoveredApiDevice()) {
          performScanAndConnect();
          esp_task_wdt_reset();
        }
        lastScanTime = now;
      } else {
        lastScanTime = now - SCAN_INTERVAL + 10000;
      }
    }

    if (now - lastTimeoutCheck > 5000) {
      checkDataTimeouts();
      lastTimeoutCheck = now;
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

int getConnectedCountImpl() {
  int count = 0;
  for (int i = 0; i < NUM_PRESETS; ++i) {
    if (presets[i].connected)
      count++;
  }
  return count;
}

// HR UI event buffer (ring buffer)
struct hr_event_t {
  int idx;
  int hr;
};
constexpr int HR_EVENT_BUF_SIZE = 32;
hr_event_t hrEventBuf[HR_EVENT_BUF_SIZE];
volatile int hrEventHead = 0; // next write
volatile int hrEventTail = 0; // next read
volatile int hrEventDropped = 0;

// 推送心率事件（可从 BLE 线程调用）
void pushHREventInternal(int idx, int hr) {
  int next = (hrEventHead + 1) % HR_EVENT_BUF_SIZE;
  if (next == hrEventTail) {
    // buffer full, drop oldest and count drop
    hrEventTail = (hrEventTail + 1) % HR_EVENT_BUF_SIZE;
    hrEventDropped++;
  }
  hrEventBuf[hrEventHead].idx = idx;
  hrEventBuf[hrEventHead].hr = hr;
  hrEventHead = next;
}

// 由主循环调用，处理并更新UI - 修复版：只更新心率值，不更新设备名称
void pollUIEventsInternal() {
  int processed = 0;
  while (hrEventTail != hrEventHead) {
    hr_event_t ev = hrEventBuf[hrEventTail];
    hrEventTail = (hrEventTail + 1) % HR_EVENT_BUF_SIZE;

    if (ev.idx >= 0 && ev.idx < NUM_PRESETS) {
      // 仅当该设备被设置为上传源（uploaded == true）时，才更新主显示标签
      if (presets[ev.idx].uploaded) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", ev.hr);
        // 只更新心率值，不更新设备名称，避免冲突
        safeLabelUpdate(ui_Label50, buf, "Label50(P1HR)");
        safeLabelUpdate(ui_Label4, buf, "Label4(P8HR)");
        processed++;
      } else {
        // 非上传源设备的心率事件仅忽略，不更新主标签
      }
    }
  }
  if (processed > 0 && hrEventDropped > 0) {
    // 丢弃计数被维护，但不打印到串口以减少日志杂音
    hrEventDropped = 0;
  }
}
} // namespace

// ===================== 对外函数实现 =====================
// 对外提供的短名称助手（简化版，仅用于回退情况）
String BT::shortName(const char *raw) { return makeDisplayName(raw); }

void BT::begin() {
  Serial.printf("[BT] Reset reason: %d\n", (int)esp_reset_reason());
  Serial.printf("[BT] Free heap: %u bytes, min: %u bytes\n",
                heap_caps_get_free_size(MALLOC_CAP_8BIT),
                heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT));
  Serial.printf("[BT] NimBLE max connections: %d\n",
                (int)CONFIG_BT_NIMBLE_MAX_CONNECTIONS);

  BLEDevice::init("RowingMonitor");

  static MySecurity securityCallbacks;
  BLEDevice::setSecurityCallbacks(&securityCallbacks);
  static BLESecurity security;
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  security.setCapability(ESP_IO_CAP_NONE);

  for (int i = 0; i < NUM_PRESETS; ++i) {
    presets[i].name = "";
    presets[i].address[0] = '\0';
    presets[i].addrType = BLE_ADDR_TYPE_PUBLIC;
    presets[i].found = false;
    presets[i].uploaded = false;
    presets[i].client = nullptr;
    presets[i].lastHeartRate = 0;
    presets[i].lastDataTime = 0;
    presets[i].lastPrintTime = 0;
    presets[i].connectionAttempts = 0;
    presets[i].dataCount = 0;
    presets[i].batteryLevel = -1;
    presets[i].nextRetryAtMs = 0;
    setDevState(i, DEV_IDLE);
    disconnectEvents[i] = false;
  }

  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new PresetScanCb());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  markConnectionCountDirtyImpl();
}

void BT::startTask() {
  if (!bleTaskHandle) {
    // 优化任务配置：栈大小6KB，移至核心1，优先级0
    xTaskCreatePinnedToCore(bleTask, "BLE Task", 6144, nullptr, 0,
                            &bleTaskHandle,
                            1); // 优先级0（最低优先级），核心1（后台处理）
  }
}

void BT::loop() {}
bool BT::isScanning() { return scanning; }
hr_device_t *BT::devices() {
  xSemaphoreTake(presetsMutex, portMAX_DELAY);
  hr_device_t *devs = presets;
  xSemaphoreGive(presetsMutex);
  return devs;
}
int BT::activeIndex() { return active ? (active - presets) : -1; }
hr_device_t *BT::activeDevice() { return active; }

void BT::setUploadSource(hr_device_t *dev) {
  // 清除之前的上传源状态
  if (active) {
    active->uploaded = false;
  }

  // 设置新的上传源
  active = dev;
  if (dev) {
    dev->uploaded = true;

    // 使用静态缓冲区避免内存问题
    static char displayNameBuffer[64];

    // 尝试从API获取设备信息，只使用运动员名称保持一致性
    String deviceCode, rowerName;
    if (BT::getDeviceInfoByAddress(String(dev->address), deviceCode,
                                   rowerName)) {
      // 只使用运动员名称，与主循环心率UI更新逻辑保持一致
      snprintf(displayNameBuffer, sizeof(displayNameBuffer), "%s",
               rowerName.c_str());
    } else {
      // 回退到处理后的蓝牙名称
      String shortName = BT::shortName(dev->name);
      snprintf(displayNameBuffer, sizeof(displayNameBuffer), "%s",
               shortName.c_str());
    }

    // 使用安全的Label更新函数
    safeLabelUpdate(ui_Label14, displayNameBuffer, "Label14(UploadSource)");
    safeLabelUpdate(ui_Label53, displayNameBuffer, "Label53(UploadSource)");

    // 压测阶段静默：不打印上传源切换日志
    // 注意：心率值更新由心率事件处理机制负责，这里不重复更新避免冲突
  } else {
    safeLabelUpdate(ui_Label14, "未选择", "Label14(NoSource)");
    safeLabelUpdate(ui_Label53, "未选择", "Label53(NoSource)");
    // 压测阶段静默：不打印上传源清空日志
    // 注意：心率值清理由主循环的心率UI更新逻辑负责，避免冲突
  }
}

bool BT::connectToPreset(int idx) {
  connectReqIdx = idx;
  return true;
}
void BT::requestConnect(int idx) {
  connectReqIdx = idx;
}

void BT::triggerScan() { manualScanRequested = true; }

void BT::triggerAutoConnect() {
  autoConnectRequested = true;
  Serial.println("[AUTO] Auto connect requested");
}

void BT::setAutoScanEnabled(bool enabled) { autoScanEnabled = enabled; }

void BT::setContinuousScan(bool enabled) {
  continuousScanMode = enabled;
  if (enabled) {
    lastContinuousScan = millis();

  } else {
  }
}

void BT::refreshScreen3UI(lv_obj_t *btns[], lv_obj_t *batteryObjs[],
                          int selectedIdx, int maxButtons,
                          const int devIdxMap[]) {
  for (int uiIdx = 0; uiIdx < maxButtons; ++uiIdx) {
    int devIdx = devIdxMap ? devIdxMap[uiIdx] : -1;

    // 检查是否为API设备（索引>=1000）
    bool isApiDevice = (devIdx >= 1000);
    bool valid = false;

    if (isApiDevice) {
      // API设备总是有效的，不需要检查扫描状态
      valid = true;
    } else {
      // 扫描设备需要检查是否有效
      valid = (devIdx >= 0 && devIdx < NUM_PRESETS &&
               presets[devIdx].address[0] != '\0');
    }

    if (!valid) {
      if (btns && btns[uiIdx])
        lv_obj_add_flag(btns[uiIdx], LV_OBJ_FLAG_HIDDEN);
      if (batteryObjs && batteryObjs[uiIdx])
        lv_obj_add_flag(batteryObjs[uiIdx], LV_OBJ_FLAG_HIDDEN);
      continue;
    }

    // 显示按钮
    if (btns && btns[uiIdx])
      lv_obj_clear_flag(btns[uiIdx], LV_OBJ_FLAG_HIDDEN);

    // 对于API设备，跳过状态和电量更新（由updateScreen3ButtonStates处理）
    if (isApiDevice) {
      // 聚焦样式
      if (btns && btns[uiIdx]) {
        if (uiIdx == selectedIdx)
          lv_obj_add_state(btns[uiIdx], LV_STATE_FOCUSED);
        else
          lv_obj_clear_state(btns[uiIdx], LV_STATE_FOCUSED);
      }
      continue;
    }

    // 扫描设备的电量文本（仅已连接且有电量数据时显示）
    if (batteryObjs && batteryObjs[uiIdx]) {
      if (presets[devIdx].connected && presets[devIdx].batteryLevel >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", presets[devIdx].batteryLevel);
        lv_label_set_text(batteryObjs[uiIdx], buf);
        lv_obj_clear_flag(batteryObjs[uiIdx], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_label_set_text(batteryObjs[uiIdx], "");
        lv_obj_add_flag(batteryObjs[uiIdx], LV_OBJ_FLAG_HIDDEN);
      }
    }

    // 聚焦样式
    if (btns && btns[uiIdx]) {
      if (uiIdx == selectedIdx)
        lv_obj_add_state(btns[uiIdx], LV_STATE_FOCUSED);
      else
        lv_obj_clear_state(btns[uiIdx], LV_STATE_FOCUSED);
    }
  }
}

int BT::getFoundDeviceCount() {
  int count = 0;
  for (int i = 0; i < NUM_PRESETS; ++i) {
    if (presets[i].found)
      count++;
  }
  return count;
}

int BT::getConnectedCount() {
  return getConnectedCountImpl();
}

void BT::markConnectionCountDirty() { markConnectionCountDirtyImpl(); }

// 检查连接数量是否发生变化（事件驱动）
bool BT::hasConnectionCountChanged() { return connectionCountChanged; }

// 清除连接数量变化标志
void BT::clearConnectionCountChange() { connectionCountChanged = false; }

int BT::getBatteryLevel(int idx) {
  if (idx >= 0 && idx < NUM_PRESETS && presets[idx].connected) {
    return presets[idx].batteryLevel;
  }
  return 0;
}

void BT::cleanupDisconnectedDevices() {
  cleanupRequested = true;
}

void BT::pushHREvent(int idx, int hr) { pushHREventInternal(idx, hr); }
void BT::pollUIEvents() { pollUIEventsInternal(); }

// API白名单相关函数实现
bool BT::isAddressInWhitelist(const String &addr) {
  return configManager.isAddressInWhitelist(addr);
}

bool BT::getDeviceInfoByAddress(const String &addr, String &deviceCode,
                                String &rowerName) {
  return configManager.getDeviceInfoByAddress(addr, deviceCode, rowerName);
}
