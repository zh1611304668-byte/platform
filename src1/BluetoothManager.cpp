/*
 * File: BluetoothManager.cpp
 * Purpose: Implements runtime logic for the Bluetooth Manager module.
 */
#include "BluetoothManager.h"
#include "ConfigManager.h"
#include "TrainingMode.h" // 添加训练模式头文件
#include "UIManager.h"    // 添加UI管理器头文件
#include "esp_task_wdt.h" // 添加看门狗头文件
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEUtils.h>


// 声明外部函数和变量
extern void safeLabelUpdate(lv_obj_t *label, const char *text,
                            const char *labelName);
extern TrainingMode training; // 声明全局训练模式对象

using namespace BT;

namespace {
// 常量定义
const int SCAN_DURATION = 5;
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
hr_device_t presets[NUM_PRESETS] = {{"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0},
                                    {"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0},
                                    {"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0},
                                    {"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0},
                                    {"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0},
                                    {"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0},
                                    {"", "", BLE_ADDR_TYPE_PUBLIC, false, false,
                                     false, nullptr, 0, false, 0, 0, 0, 0, 0}};

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

// 前向声明：HR 事件推送函数（定义在文件后部）
void pushHREventInternal(int idx, int hr);

// 安全回调
class MySecurity : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest() { return 0; }
  void onPassKeyNotify(uint32_t pass_key) {}
  bool onConfirmPIN(uint32_t pin) { return true; }
  bool onSecurityRequest() { return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl) {
    if (cmpl.success)
      Serial.println("[SEC] Pairing success");
    else
      Serial.println("[SEC] Pairing failed");
  }
};

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
  void onResult(BLEAdvertisedDevice advertisedDevice) override {
    if (!advertisedDevice.haveName())
      return;

    String addr = advertisedDevice.getAddress().toString().c_str();

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
        presets[i].addrType = advertisedDevice.getAddressType();
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
            xSemaphoreTake(presetsMutex, portMAX_DELAY);

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

            presets[freeSlot].addrType = advertisedDevice.getAddressType();
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
  dev.connecting = true;
  dev.connectionAttempts++;

  if (pBLEScan)
    pBLEScan->stop();

  // 清理旧连接
  if (dev.client) {
    if (dev.client->isConnected())
      dev.client->disconnect();
    delete dev.client;
    dev.client = nullptr;
  }

  BLEClient *client = BLEDevice::createClient();
  dev.client = client;

  // 安全设置
  BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT);
  BLEDevice::setSecurityCallbacks(new MySecurity());
  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
  pSecurity->setCapability(ESP_IO_CAP_NONE);

  BLEAddress addr(dev.address);
  if (!client->connect(addr, dev.addrType)) {
    delete client;
    dev.client = nullptr;
    dev.connecting = false;
    return false;
  }

  BLERemoteService *svc = nullptr;
  try {
    svc = client->getService(hrServiceUUID);
  } catch (...) {
  }

  if (!svc) {
    client->disconnect();
    delete client;
    dev.client = nullptr;
    dev.connecting = false;
    return false;
  }

  BLERemoteCharacteristic *ch = nullptr;
  try {
    ch = svc->getCharacteristic(hrCharUUID);
  } catch (...) {
  }

  if (!ch) {
    client->disconnect();
    delete client;
    dev.client = nullptr;
    dev.connecting = false;
    return false;
  }

  if (ch->canNotify()) {
    try {
      ch->registerForNotify(notifyCallback);

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
      client->disconnect();
      delete client;
      dev.client = nullptr;
      dev.connecting = false;
      return false;
    }
  }

  // 只有在所有步骤都成功后才标记为已连接
  dev.connected = true;
  dev.connecting = false;
  dev.lastDataTime = millis();
  dev.connectionAttempts = 0;
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
          bCh->registerForNotify(batteryNotifyCallback);
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

  Serial.printf("[CON] Connected to %s\n", dev.name);
  return true;
}

// 执行扫描和连接
void performScanAndConnect() {
  // 训练时禁止扫描和连接新设备，避免BLE连接操作阻塞CPU影响MQTT任务
  // 注意：已连接的设备不受影响，心率/电量读取和上传继续正常工作
  if (training.isActive()) {

    return;
  }

  scanning = true;

  pBLEScan->clearResults();
  pBLEScan->start(SCAN_DURATION, false); // 同步阻塞5秒
  scanning = false;

  // 按照API设备列表顺序连接设备（从上到下） - 修复版：避免重复连接
  // 首先获取API设备列表
  if (configManager.isRowerListReady()) {
    const auto &rowerList = configManager.getRowerList();

    // 按API列表顺序查找并连接设备
    for (size_t apiIdx = 0; apiIdx < rowerList.size(); apiIdx++) {
      const auto &rower = rowerList[apiIdx];

      // 检查该API设备是否已连接（通过地址匹配）
      bool alreadyConnected = false;
      for (int j = 0; j < NUM_PRESETS; ++j) {
        if (presets[j].connected &&
            String(presets[j].address).equalsIgnoreCase(rower.btAddr)) {
          alreadyConnected = true;

          break;
        }
      }

      if (alreadyConnected)
        continue; // 跳过已连接的设备

      // 在扫描结果中查找对应的设备
      for (int i = 0; i < NUM_PRESETS; ++i) {
        if (presets[i].found && !presets[i].connected &&
            !presets[i].connecting &&
            presets[i].connectionAttempts < MAX_CONNECTION_ATTEMPTS) {

          String scannedAddr = String(presets[i].address);
          if (scannedAddr.equalsIgnoreCase(rower.btAddr)) {

            internalConnect(i);
            break; // 找到匹配设备后跳出内层循环
          }
        }
      }
    }
  } else {
    // 如果API列表未就绪，降级使用原有的槽位顺序连接（但仍避免重复连接）

    for (int i = 0; i < NUM_PRESETS; ++i) {
      if (presets[i].found && !presets[i].connected && !presets[i].connecting &&
          presets[i].connectionAttempts < MAX_CONNECTION_ATTEMPTS) {

        internalConnect(i);
      }
    }
  }
}

// 自动连接API设备
void performAutoConnect() {
  Serial.println("[AUTO] 开始自动连接API设备");

  // 首先触发一次扫描以发现设备
  performScanAndConnect();

  Serial.printf("[AUTO] 自动连接完成，已连接设备数: %d\n",
                BT::getConnectedCount());
}

// 安全清理BLE客户端连接
void cleanupDevice(int deviceIndex) {
  if (deviceIndex < 0 || deviceIndex >= NUM_PRESETS)
    return;

  hr_device_t &dev = presets[deviceIndex];

  Serial.printf("[CLEANUP] Cleaning up device %d: %s\n", deviceIndex, dev.name);

  // 先断开连接
  if (dev.client) {
    try {
      if (dev.client->isConnected()) {
        dev.client->disconnect();
        // 短暂延时确保断开完成
        vTaskDelay(pdMS_TO_TICKS(100));
      }
    } catch (...) {
      Serial.printf("[CLEANUP] Exception during disconnect for device %d\n",
                    deviceIndex);
    }

    // 安全删除客户端对象
    try {
      delete dev.client;
      dev.client = nullptr;
    } catch (...) {
      Serial.printf(
          "[CLEANUP] Exception during client deletion for device %d\n",
          deviceIndex);
      dev.client = nullptr; // 设为null避免悬垂指针
    }
  }

  // 重置设备状态
  dev.connected = false;
  dev.connecting = false;
  dev.uploaded = false;
  dev.lastHeartRate = 0;
  dev.batteryLevel = -1;
  dev.lastDataTime = 0;
  dev.lastPrintTime = 0;
  dev.connectionAttempts = 0;
  dev.dataCount = 0;

  // 如果这是活动设备，清除活动源
  if (active == &dev) {
    setUploadSource(nullptr);
  }

  Serial.printf("[CLEANUP] Device %d cleanup completed\n", deviceIndex);
}

// 检查数据超时 - 修复版：区分连接超时和数据超时，保持设备信息持久性
void checkDataTimeouts() {
  unsigned long now = millis();
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

      // 如果BLE连接实际已断开，清理连接状态但保持设备信息
      if (!actuallyConnected) {
        Serial.printf("[TIMEOUT] Device %s(%s) BLE connection lost, cleaning "
                      "connection only\n",
                      presets[i].name, presets[i].address);

        // 只清理连接状态，不清理设备发现信息
        if (presets[i].client) {
          try {
            delete presets[i].client;
          } catch (...) {
          }
          presets[i].client = nullptr;
        }

        // 重置连接状态但保持设备信息
        presets[i].connected = false;
        presets[i].connecting = false;
        presets[i].uploaded = false;
        presets[i].lastHeartRate = 0;
        presets[i].batteryLevel = -1;
        presets[i].lastDataTime = 0;
        presets[i].connectionAttempts = 0;

        // 如果这是活动设备，清除活动源但保持UI按钮
        if (active == &presets[i]) {
          setUploadSource(nullptr);
        }

        // 重要：保持found状态和设备信息，让按钮继续显示
        // presets[i].found = true;  // 保持发现状态
        // 不清理地址和名称，让设备在Screen3中保持可见

        continue; // 跳过数据超时检查
      }

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
  Serial.println("[BT] BLE任务启动 (无看门狗保护)");

  unsigned long lastScanTime = 0;
  unsigned long lastTimeoutCheck = 0;

  while (true) {
    unsigned long now = millis();

    // 处理手动扫描请求
    if (manualScanRequested) {
      performScanAndConnect();
      manualScanRequested = false;
      lastScanTime = now; // 重置自动扫描计时器
    }
    // 处理自动连接请求
    else if (autoConnectRequested) {
      performAutoConnect();
      autoConnectRequested = false;
      lastScanTime = now; // 重置自动扫描计时器
    }
    // Screen3持续扫描模式
    // Screen3等待API配置加载完成后再开始持续扫描
    else if (continuousScanMode && !scanning &&
             (now - lastContinuousScan > CONTINUOUS_SCAN_INTERVAL)) {
      // 检查API配置是否已就绪
      if (configManager.isRowerListReady()) {
        performScanAndConnect();
        lastContinuousScan = now;
      } else {
        // API配置未就绪，延长扫描间隔
        lastContinuousScan =
            now - CONTINUOUS_SCAN_INTERVAL + 3000; // 3秒后再次检查
      }
    }
    // 定期扫描（获取到API数据后在所有界面都进行后台扫描） - 修复版：智能扫描
    // 只要有心率带设备白名单就进行定期扫描，但避免不必要的扫描
    else if (autoScanEnabled && !continuousScanMode && !scanning &&
             (now - lastScanTime > SCAN_INTERVAL)) {
      // 检查API配置是否已就绪
      if (configManager.isRowerListReady()) {
        // 检查是否有需要连接的设备（智能扫描）
        bool hasDeviceToConnect = false;
        const auto &rowerList = configManager.getRowerList();

        for (const auto &rower : rowerList) {
          bool isConnected = false;
          // 检查该API设备是否已连接
          for (int j = 0; j < NUM_PRESETS; ++j) {
            if (presets[j].connected &&
                String(presets[j].address).equalsIgnoreCase(rower.btAddr)) {
              isConnected = true;
              break;
            }
          }
          if (!isConnected) {
            hasDeviceToConnect = true;
            break;
          }
        }

        if (hasDeviceToConnect) {

          performScanAndConnect();
        } else {
        }
        lastScanTime = now;
      } else {
        // API配置未就绪，延长扫描间隔，避免频繁检查
        lastScanTime = now - SCAN_INTERVAL + 10000; // 10秒后再次检查
        static uint32_t lastApiWaitMsg = 0;
        if (now - lastApiWaitMsg > 30000) { // 30秒提示一次
          Serial.println(
              "[SCAN] 等待API心率带设备列表加载完成后开始后台扫描...");
          lastApiWaitMsg = now;
        }
      }
    }

    // 处理连接请求
    if (connectReqIdx >= 0) {
      internalConnect(connectReqIdx);
      connectReqIdx = -1;
    }

    // 检查数据超时
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
  BLEDevice::init("RowingMonitor");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new PresetScanCb());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
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

    Serial.printf("[UPLOAD] Set upload source to raw='%s' display='%s'\n",
                  dev->name, displayNameBuffer);
    // 注意：心率值更新由心率事件处理机制负责，这里不重复更新避免冲突
  } else {
    safeLabelUpdate(ui_Label14, "未选择", "Label14(NoSource)");
    safeLabelUpdate(ui_Label53, "未选择", "Label53(NoSource)");
    Serial.println("[UPLOAD] No upload source selected");
    // 注意：心率值清理由主循环的心率UI更新逻辑负责，避免冲突
  }
}

bool BT::connectToPreset(int idx) {
  xSemaphoreTake(presetsMutex, portMAX_DELAY);
  bool ret = internalConnect(idx);
  xSemaphoreGive(presetsMutex);
  return ret;
}
void BT::requestConnect(int idx) {
  xSemaphoreTake(presetsMutex, portMAX_DELAY);
  connectReqIdx = idx;
  xSemaphoreGive(presetsMutex);
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
  int currentCount = getConnectedCountImpl();

  // 检查连接数量是否变化（事件驱动）
  if (currentCount != lastKnownConnectionCount) {
    connectionCountChanged = true;
    lastKnownConnectionCount = currentCount;
  }

  return currentCount;
}

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
  static uint32_t lastCleanupTime = 0;
  uint32_t now = millis();

  // 限制清理频率，避免过度清理影响性能
  if (now - lastCleanupTime < 5000) { // 最少5秒间隔
    return;
  }
  lastCleanupTime = now;

  int cleanedCount = 0;

  for (int i = 0; i < NUM_PRESETS; ++i) {
    // 先获取信号量检查状态
    if (xSemaphoreTake(presetsMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
      Serial.printf("[BT] Failed to acquire mutex for device %d, skipping\n",
                    i);
      continue;
    }

    // 添加设备有效性检查
    if (i < 0 || i >= NUM_PRESETS) {
      Serial.printf("[BT] Invalid device index %d, skipping\n", i);
      xSemaphoreGive(presetsMutex);
      continue;
    }

    // 检查设备是否需要清理
    bool needsCleanup = false;
    BLEClient *clientToCleanup = nullptr;
    bool wasActive = false;
    String deviceName = ""; // 安全复制设备名称

    // 安全地检查设备状态
    try {
      // 复制设备名称用于日志输出
      if (presets[i].name != nullptr && strlen(presets[i].name) > 0) {
        deviceName = String(presets[i].name);
      } else {
        deviceName = "未知设备";
      }

      // 检查客户端指针的有效性 - 增强内存安全检查
      if (presets[i].client != nullptr) {
        // 保存客户端指针并验证内存地址有效性
        BLEClient *tempClient = presets[i].client;

        // 检查指针是否在合理的内存范围内
        if ((uintptr_t)tempClient < 0x1000 ||
            (uintptr_t)tempClient > 0x50000000) {
          Serial.printf(
              "[BT] Invalid client pointer 0x%08x for device %d, skipping\n",
              (uintptr_t)tempClient, i);
          presets[i].client = nullptr;
          presets[i].connected = false;
          presets[i].connecting = false;
          xSemaphoreGive(presetsMutex);
          continue;
        }

        // 进一步验证客户端对象是否有效
        try {
          bool isConnected = tempClient->isConnected();

          if (!presets[i].connected && tempClient) {
            needsCleanup = true;
            clientToCleanup = tempClient;
          } else if (!isConnected && presets[i].connected) {
            needsCleanup = true;
            clientToCleanup = tempClient;
            presets[i].connected = false;
          }
        } catch (...) {
          // 客户端对象已损坏，安全清理
          Serial.printf(
              "[BT] Client object corrupted for device %d, safe cleanup\n", i);
          presets[i].client = nullptr;
          presets[i].connected = false;
          presets[i].connecting = false;
          // 不进行delete操作，避免崩溃
          needsCleanup = false;
          clientToCleanup = nullptr;
        }
      }

      // 检查是否是活动设备
      wasActive = (active == &presets[i]);

    } catch (...) {
      Serial.printf(
          "[BT] Exception while checking device %d status, skipping\n", i);
      xSemaphoreGive(presetsMutex);
      continue;
    }

    // 修复版：保持设备信息持久性 - 不清理发现状态
    // 即使断连也保持设备在Screen3中可见，方便重连
    if (!presets[i].connected && presets[i].found) {
      Serial.printf("[BT] Device %d (%s) disconnected but keeping discovery "
                    "info for reconnection\n",
                    i, deviceName.c_str());
      // 不清理found状态、地址和名称，让设备保持在Screen3列表中
      // presets[i].found = false;        // 保持true
      // presets[i].address[0] = '\0';    // 保持地址
      // namePoolUsed[i] = false;         // 保持名称池
      // presets[i].name = "";            // 保持名称
    }

    // 如果需要清理，先重置状态，然后释放信号量
    if (needsCleanup) {
      Serial.printf("[BT] Cleaning up device %d: %s\n", i, deviceName.c_str());

      // 重置状态（除了client指针，稍后处理）
      presets[i].connected = false;
      presets[i].connecting = false;
      presets[i].lastHeartRate = 0;
      presets[i].batteryLevel = -1;
      // 注意：不在这里设置client = nullptr，避免影响clientToCleanup

      // 如果这是活动设备，清除活动源
      if (wasActive) {
        try {
          setUploadSource(nullptr);
        } catch (...) {
          Serial.printf(
              "[BT] Exception while clearing active source for device %d\n", i);
        }
      }
    }

    // 释放信号量
    xSemaphoreGive(presetsMutex);

    // 在信号量外进行耗时的客户端清理操作
    if (needsCleanup && clientToCleanup) {
      try {
        // 再次验证客户端指针的有效性
        if (clientToCleanup != nullptr &&
            (uintptr_t)clientToCleanup >= 0x1000 &&
            (uintptr_t)clientToCleanup <= 0x50000000) {

          try {
            // 尝试安全断开连接
            if (clientToCleanup->isConnected()) {
              clientToCleanup->disconnect();
              vTaskDelay(pdMS_TO_TICKS(50));
            }
          } catch (...) {
            Serial.printf("[BT] Exception during disconnect for device %d\n",
                          i);
          }

          // 使用更安全的删除方式，增加额外检查
          try {
            // 再次检查内存有效性
            volatile uint32_t *testPtr = (volatile uint32_t *)clientToCleanup;
            uint32_t testValue = *testPtr; // 尝试读取内存
            (void)testValue;               // 避免未使用变量警告

            delete clientToCleanup;

            // 只有在成功删除后才设置为nullptr
            if (xSemaphoreTake(presetsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
              presets[i].client = nullptr;
              xSemaphoreGive(presetsMutex);
            }

            cleanedCount++;
            Serial.printf("[BT] Successfully cleaned device %d: %s\n", i,
                          deviceName.c_str());
          } catch (...) {
            Serial.printf("[BT] Memory access failed during deletion for "
                          "device %d, memory may be corrupted\n",
                          i);
            // 不执行delete，但仍需清空指针避免重复访问
            if (xSemaphoreTake(presetsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
              presets[i].client = nullptr;
              xSemaphoreGive(presetsMutex);
            }
          }
        } else {
          Serial.printf("[BT] Invalid client pointer detected during cleanup "
                        "for device %d\n",
                        i);
          // 清空无效指针
          if (xSemaphoreTake(presetsMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            presets[i].client = nullptr;
            xSemaphoreGive(presetsMutex);
          }
        }
      } catch (...) {
        Serial.printf("[BT] General exception during cleanup of device %d\n",
                      i);
      }
    }

    // 强制垃圾回收和内存检查
    if (needsCleanup) {
      vTaskDelay(pdMS_TO_TICKS(10)); // 给系统一点时间处理内存释放
    }
  }

  if (cleanedCount > 0) {
    Serial.printf("[BT] Cleanup completed: %d devices cleaned\n", cleanedCount);

    // 强制垃圾回收以释放内存
    // ESP.gc();  // 如果支持的话
  }
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
