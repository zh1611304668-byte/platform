#include <Arduino.h>
#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <esp_system.h>

/*
  ESP32 BLE Heart Rate Peripheral Simulator
  - Heart Rate Service (0x180D), Measurement Characteristic (0x2A37, Notify)
  - Battery Service (0x180F), Battery Level Characteristic (0x2A19, Read+Notify)
  - Configurable device name, HR range, interval, optional drop simulation
*/

// ======================= User Config =======================
#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "HRM_SIM_01"
#endif
static const char *kDeviceName = BLE_DEVICE_NAME;

static const uint16_t kHrMin = 95;
static const uint16_t kHrMax = 155;
static const uint32_t kNotifyIntervalMs = 1000;

#ifndef BLE_USE_CUSTOM_BASE_MAC
#define BLE_USE_CUSTOM_BASE_MAC 0
#endif

#ifndef BLE_BASE_MAC_0
#define BLE_BASE_MAC_0 0x02
#endif
#ifndef BLE_BASE_MAC_1
#define BLE_BASE_MAC_1 0xAA
#endif
#ifndef BLE_BASE_MAC_2
#define BLE_BASE_MAC_2 0xBB
#endif
#ifndef BLE_BASE_MAC_3
#define BLE_BASE_MAC_3 0xCC
#endif
#ifndef BLE_BASE_MAC_4
#define BLE_BASE_MAC_4 0xDD
#endif
#ifndef BLE_BASE_MAC_5
#define BLE_BASE_MAC_5 0x01
#endif

static const bool kUseCustomBaseMac = (BLE_USE_CUSTOM_BASE_MAC != 0);
static uint8_t kCustomBaseMac[6] = {
    BLE_BASE_MAC_0, BLE_BASE_MAC_1, BLE_BASE_MAC_2,
    BLE_BASE_MAC_3, BLE_BASE_MAC_4, BLE_BASE_MAC_5
};

// Simulate unstable peripheral by random restart after connected for a while.
static const bool kEnableRandomDrop = false;
static const uint32_t kDropMinMs = 30000;
static const uint32_t kDropMaxMs = 90000;

// Simulate battery drain.
static const bool kEnableBatteryDrain = true;
static const uint8_t kBatteryStart = 98;
static const uint32_t kBatteryDrainStepMs = 60000;
// ===================== End User Config =====================

static BLEServer *gServer = nullptr;
static BLECharacteristic *gHrChar = nullptr;
static BLECharacteristic *gBattChar = nullptr;
static bool gConnected = false;
static uint16_t gConnId = 0;

static uint16_t gCurrentHr = kHrMin;
static int8_t gHrDeltaDirection = 1;

static uint8_t gBatteryLevel = kBatteryStart;
static uint32_t gLastHrNotifyMs = 0;
static uint32_t gLastBatteryDrainMs = 0;
static uint32_t gLastHeartbeatMs = 0;

static uint32_t gConnectedAtMs = 0;
static uint32_t gDropAtMs = 0;

static void printBleAddress() {
  uint8_t btMac[6] = {0};
  esp_read_mac(btMac, ESP_MAC_BT);
  Serial.printf("[SIM] BLE MAC=%02X:%02X:%02X:%02X:%02X:%02X\n",
                btMac[0], btMac[1], btMac[2], btMac[3], btMac[4], btMac[5]);
}

static uint32_t randInRange(uint32_t lo, uint32_t hi) {
  if (hi <= lo) {
    return lo;
  }
  return lo + (uint32_t)random((long)(hi - lo + 1));
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server, esp_ble_gatts_cb_param_t *param) override {
    gConnected = true;
    gConnId = param->connect.conn_id;
    gConnectedAtMs = millis();

    if (kEnableRandomDrop) {
      gDropAtMs = gConnectedAtMs + randInRange(kDropMinMs, kDropMaxMs);
    }

    Serial.printf("[SIM] Central connected, conn_id=%u\n", gConnId);

    (void)server;
    BLEDevice::startAdvertising();
  }

  void onDisconnect(BLEServer *server) override {
    (void)server;
    gConnected = false;
    gConnId = 0;
    gDropAtMs = 0;
    Serial.println("[SIM] Central disconnected");

    BLEDevice::startAdvertising();
  }
};

static void configureOptionalMac() {
  if (!kUseCustomBaseMac) {
    return;
  }

  esp_err_t rc = esp_base_mac_addr_set(kCustomBaseMac);
  Serial.printf("[SIM] Set custom base MAC rc=%d\n", (int)rc);
}

static void setupBleServices() {
  BLEDevice::init(kDeviceName);

  gServer = BLEDevice::createServer();
  gServer->setCallbacks(new ServerCallbacks());

  BLEService *hrService = gServer->createService(BLEUUID((uint16_t)0x180D));
  gHrChar = hrService->createCharacteristic(
      BLEUUID((uint16_t)0x2A37),
      BLECharacteristic::PROPERTY_NOTIFY
  );
  gHrChar->addDescriptor(new BLE2902());

  BLEService *battService = gServer->createService(BLEUUID((uint16_t)0x180F));
  gBattChar = battService->createCharacteristic(
      BLEUUID((uint16_t)0x2A19),
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
  );
  gBattChar->addDescriptor(new BLE2902());
  gBattChar->setValue(&gBatteryLevel, 1);

  hrService->start();
  battService->start();

  BLEAdvertising *adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(BLEUUID((uint16_t)0x180D));
  adv->addServiceUUID(BLEUUID((uint16_t)0x180F));
  adv->setScanResponse(true);
  adv->setMinPreferred(0x06);
  adv->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
}

static void notifyHeartRate() {
  uint8_t payload[2];
  payload[0] = 0x00;
  payload[1] = (uint8_t)gCurrentHr;

  gHrChar->setValue(payload, sizeof(payload));
  gHrChar->notify();

  Serial.printf("[SIM] HR notify: %u bpm\n", (unsigned)gCurrentHr);

  if (gCurrentHr <= kHrMin) {
    gHrDeltaDirection = 1;
  } else if (gCurrentHr >= kHrMax) {
    gHrDeltaDirection = -1;
  }

  uint16_t step = (uint16_t)randInRange(1, 4);
  int32_t next = (int32_t)gCurrentHr + (int32_t)(step * gHrDeltaDirection);
  if (next < (int32_t)kHrMin) next = kHrMin;
  if (next > (int32_t)kHrMax) next = kHrMax;
  gCurrentHr = (uint16_t)next;
}

static void updateBatteryIfNeeded() {
  if (!kEnableBatteryDrain) {
    return;
  }

  uint32_t now = millis();
  if (now - gLastBatteryDrainMs < kBatteryDrainStepMs) {
    return;
  }
  gLastBatteryDrainMs = now;

  if (gBatteryLevel > 5) {
    gBatteryLevel--;
  }

  gBattChar->setValue(&gBatteryLevel, 1);
  if (gConnected) {
    gBattChar->notify();
  }

  Serial.printf("[SIM] Battery: %u%%\n", (unsigned)gBatteryLevel);
}

static void maybeSimulateDrop() {
  if (!kEnableRandomDrop || !gConnected || gDropAtMs == 0) {
    return;
  }

  uint32_t now = millis();
  if (now < gDropAtMs) {
    return;
  }

  Serial.println("[SIM] Simulate drop: restart device");
  delay(100);
  ESP.restart();
}

void setup() {
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart < 2500)) {
    delay(10);
  }
  delay(200);

  randomSeed(esp_random());

  Serial.println("[SIM] ===== BOOT LOG =====");
  Serial.println("[SIM] BLE HR simulator boot");
  Serial.printf("[SIM] DeviceName=%s\n", kDeviceName);
  Serial.printf("[SIM] HR range=%u..%u, notify=%lu ms\n",
                (unsigned)kHrMin,
                (unsigned)kHrMax,
                (unsigned long)kNotifyIntervalMs);

  configureOptionalMac();
  setupBleServices();
  printBleAddress();

  gLastHrNotifyMs = millis();
  gLastBatteryDrainMs = millis();
  gLastHeartbeatMs = millis();
}

void loop() {
  uint32_t now = millis();

  if (gConnected && (now - gLastHrNotifyMs >= kNotifyIntervalMs)) {
    gLastHrNotifyMs = now;
    notifyHeartRate();
  }

  updateBatteryIfNeeded();

  if (now - gLastHeartbeatMs >= 5000) {
    gLastHeartbeatMs = now;
    Serial.printf("[SIM] alive, uptime=%lu ms, connected=%d\n",
                  (unsigned long)now, gConnected ? 1 : 0);
    printBleAddress();
  }

  maybeSimulateDrop();

  delay(20);
}
