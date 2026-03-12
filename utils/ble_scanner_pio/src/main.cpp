/**
 * BLE Scanner 工具
 * ================
 * 扫描周围所有 BLE 设备并通过串口打印 MAC 地址、设备名称、RSSI 和服务 UUID。
 * 专用于查找心率带等 BLE 设备的 MAC 地址。
 *
 * 使用方法：
 *   1. 用 PlatformIO 烧录到 ESP32-S3
 *   2. 打开串口监控（115200 baud）
 *   3. 让心率带保持活跃（贴近皮肤或轻拍激活）
 *   4. 观察串口输出，找到你的设备
 *
 * 心率带识别技巧：
 *   - Polar H10：名称包含 "Polar" 或 "H10"，有 0x180D (Heart Rate) 服务
 *   - Wahoo TICKR：名称包含 "TICKR"
 *   - Garmin：名称包含 "HRM"
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// 扫描参数
static const int    SCAN_DURATION_SEC = 5;   // 每轮扫描 5 秒
static const int    SCAN_INTERVAL_SEC = 10;  // 每隔 10 秒扫描一次（包含扫描时间）

// 心率服务 UUID（0x180D），用于标记心率带
static BLEUUID hrServiceUUID((uint16_t)0x180D);

// ─────────────────────────────────────────────
// 扫描回调：每发现一个设备就调用一次
// ─────────────────────────────────────────────
class ScanCallback : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice device) override {
    String addr = device.getAddress().toString().c_str();
    String name = device.haveName() ? device.getName().c_str() : "(无名称)";
    int    rssi = device.getRSSI();

    // 检查是否有心率服务
    bool hasHR = device.haveServiceUUID() && device.isAdvertisingService(hrServiceUUID);

    Serial.printf("  %-18s  RSSI:%-4d  %-30s  %s\n",
                  addr.c_str(),
                  rssi,
                  name.c_str(),
                  hasHR ? "<<< 心率服务 HR 0x180D" : "");
  }
};

// ─────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n====================================");
  Serial.println("  BLE 设备扫描工具 v1.0");
  Serial.println("  用于查找心率带 MAC 地址");
  Serial.println("====================================");
  Serial.println("列: [MAC地址]  [RSSI]  [设备名称]  [服务标记]");
  Serial.println();

  BLEDevice::init("BLE_Scanner");
}

static int scanRound = 0;

void loop() {
  scanRound++;
  Serial.printf("--- 第 %d 轮扫描（持续 %d 秒）---\n", scanRound, SCAN_DURATION_SEC);

  BLEScan* pScan = BLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new ScanCallback(), true);
  pScan->setActiveScan(true);  // 主动扫描获取设备名称
  pScan->setInterval(100);
  pScan->setWindow(99);

  BLEScanResults results = pScan->start(SCAN_DURATION_SEC, false);

  Serial.printf("--- 本轮共发现 %d 个设备 ---\n\n", results.getCount());
  pScan->clearResults();

  // 等待下一轮
  int waitSec = SCAN_INTERVAL_SEC - SCAN_DURATION_SEC;
  if (waitSec > 0) {
    Serial.printf("（%d 秒后开始下一轮...）\n\n", waitSec);
    delay(waitSec * 1000);
  }
}
