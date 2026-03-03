/*
 * File: main.cpp
 * Purpose: Application entry point and top-level task orchestration.
 */
#include "BluetoothManager.h"
#include "BrightnessManager.h"
#include "CellularManager.h"
#include "ConfigManager.h"
#include "DataFlowManager.h"
#include "FirmwareVersion.h"
#include "GNSSProcessor.h"
#include "IMUManager.h"
#include "MQTTManager.h"
#include "PowerManager.h"
#include "SDCardManager.h"
#include "SensorPCF85063.hpp"
#include "StrokeDataManager.h"
#include "TCA9554.h"
#include "TouchDrvFT6X36.hpp"
#include "TrainingMode.h"
#include "UIManager.h"
#include "WifiTransferManager.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <Arduino_GFX_Library.h>
#include <HardwareSerial.h>
#include <lvgl.h>
#include <queue>
#include <ui.h>

#define GFX_BL 6
#define SPI_MISO 2
#define SPI_MOSI 1
#define SPI_SCLK 5
#define LCD_CS -1
#define LCD_DC 3
#define LCD_RST -1
#define LCD_HOR_RES 320
#define LCD_VER_RES 480
#define I2C_SDA 8
#define I2C_SCL 7

TCA9554 TCA(0x20);
Arduino_DataBus *bus =
    new Arduino_ESP32SPI(LCD_DC, LCD_CS, SPI_SCLK, SPI_MOSI, SPI_MISO);
Arduino_GFX *gfx =
    new Arduino_ST7796(bus, LCD_RST, 3, true, LCD_HOR_RES, LCD_VER_RES);
TouchDrvFT6X36 touch;

uint32_t screenWidth;
uint32_t screenHeight;
uint32_t bufSize;
lv_disp_draw_buf_t draw_buf;
lv_color_t *disp_draw_buf1;
lv_color_t *disp_draw_buf2;

HardwareSerial SerialGNSS(2);
extern HardwareSerial Serial4G;

GNSSProcessor gnss(SerialGNSS, 48, 47);
CellularManager cellular(Serial4G, TXD1, RXD1);
IMUManager imu(I2C_SDA, I2C_SCL, &gnss);
TrainingMode training;
PowerManager powerMgr;
SensorPCF85063 rtc;
SDCardManager sdCardManager;
WifiTransferManager wifiTransfer;
BrightnessManager brightness(GFX_BL, 0);

SemaphoreHandle_t serial4GMutex = nullptr;
bool configLoadingInProgress = false;

SemaphoreHandle_t dataMutex = nullptr;

SemaphoreHandle_t uiStateMutex = nullptr;

struct MutexStats {
  uint32_t dataMutexTakes = 0;
  uint32_t dataMutexTimeouts = 0;
  uint32_t serial4GMutexTakes = 0;
  uint32_t serial4GMutexTimeouts = 0;
  uint32_t uiStateMutexTakes = 0;
  uint32_t uiStateMutexTimeouts = 0;
};

MutexStats mutexStats;

bool safeTakeMutex(SemaphoreHandle_t mutex, TickType_t timeout,
                   const char *mutexName) {
  if (!mutex) {
    return false;
  }

  bool success = (xSemaphoreTake(mutex, timeout) == pdTRUE);

  if (strcmp(mutexName, "dataMutex") == 0) {
    if (success)
      mutexStats.dataMutexTakes++;
    else
      mutexStats.dataMutexTimeouts++;
  } else if (strcmp(mutexName, "serial4GMutex") == 0) {
    if (success)
      mutexStats.serial4GMutexTakes++;
    else
      mutexStats.serial4GMutexTimeouts++;
  } else if (strcmp(mutexName, "uiStateMutex") == 0) {
    if (success)
      mutexStats.uiStateMutexTakes++;
    else
      mutexStats.uiStateMutexTimeouts++;
  }

  return success;
}

bool gnssActive = false;
bool isMQTTConnected = false;
String localTime = "--:--";
bool rtcInitialized = false;
bool rtcTimeSynced = false;
bool networkTaskCompleted = false;
bool ui_initialized = false;
float strokeRate = 0.0f;
float speedMps = 0.0f;
int strokeCount = 0;
float strokeLength = 0.0f;
float strokeDistance = 0.0f;
float totalDistance = 0.0f;

int panel1ContentIndex = 0;
int panel8ContentIndex = 3;
constexpr uint8_t DEBOUNCE_DELAY = 20;
constexpr uint8_t KEY_QUEUE_SIZE = 32;
constexpr unsigned long GNSS_LABEL_UPDATE_INTERVAL_MS = 5000;

struct KeyEvent {
  uint8_t pin;
  bool action;
  uint32_t timestampMs;
};

QueueHandle_t keyQueue = nullptr;
TaskHandle_t keyTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;

// IMU鏁版嵁璁板綍宸叉敼涓虹洿鎺ヨ皟鐢紙甯﹀唴閮ㄧ紦鍐诧級锛屾棤闇€闃熷垪鍜屽紓姝ヤ换鍔?

static lv_obj_t *dot;

volatile ScreenId current_screen = SCREEN1;
int screen3_selected_idx = 0;

ScreenId safeGetCurrentScreen() {
  if (safeTakeMutex(uiStateMutex, pdMS_TO_TICKS(50), "uiStateMutex")) {
    ScreenId screen = current_screen;
    xSemaphoreGive(uiStateMutex);
    return screen;
  }
  return SCREEN1;
}

bool safeSetCurrentScreen(ScreenId newScreen) {
  if (safeTakeMutex(uiStateMutex, pdMS_TO_TICKS(50), "uiStateMutex")) {
    current_screen = newScreen;
    xSemaphoreGive(uiStateMutex);
    return true;
  }
  return false;
}

int safeGetScreen3SelectedIdx() {
  if (safeTakeMutex(uiStateMutex, pdMS_TO_TICKS(50), "uiStateMutex")) {
    int idx = screen3_selected_idx;
    xSemaphoreGive(uiStateMutex);
    return idx;
  }
  return 0;
}

bool safeSetScreen3SelectedIdx(int newIdx) {
  if (safeTakeMutex(uiStateMutex, pdMS_TO_TICKS(50), "uiStateMutex")) {
    screen3_selected_idx = newIdx;
    xSemaphoreGive(uiStateMutex);
    return true;
  }
  return false;
}

const int K1_PIN = 21;
const int K2_PIN = 38;
const int K3_PIN = 39;
const int K4_PIN = 40;

bool k1_last = HIGH, k2_last = HIGH, k3_last = HIGH, k4_last = HIGH;
unsigned long last_key_time = 0;
const unsigned long debounce_delay = 30;

bool trainingActive = false;
unsigned long k4PressTime = 0;
bool k4ToggledThisPress = false;
const unsigned long trainingLongPress = 2000;
const unsigned long trainingDoubleClickWindow = 800;
const unsigned long trainingTapMaxPress = 600;
unsigned long k4LastTapTimeScreen1 = 0;
bool k4PressedInScreen1 = false;
bool k4IsPressed = false;

lv_group_t *group_screen2 = nullptr;
lv_group_t *group_screen3 = nullptr;
lv_obj_t *screen2_focus_objs[2];
int screen2_focus_idx = 1;
int screen2_remembered_focus_idx = 0;
uint32_t lastScreen2KeyPress = 0;

lv_obj_t *screen3_btns[9] = {nullptr};
lv_obj_t *screen3_status_labels[9] = {nullptr};
lv_obj_t *screen3_battery_labels[9] = {nullptr};
lv_obj_t *screen3_bluetooth_icons[9] = {nullptr};
lv_obj_t *screen3_name_labels[9] = {nullptr};
int screen3_device_index_map[9] = {-1, -1, -1, -1, -1, -1, -1, -1, -1};

volatile bool ui_update_needed = false;
uint32_t last_ui_update_time = 0;
int screen3_button_count = 0;
unsigned long screen3_enter_time = 0;
bool screen3_scan_triggered = false;
bool screen3_buttons_created = false;

bool boot_auto_once_done = false;
uint8_t boot_auto_once_stage = 0;
uint32_t boot_auto_once_ts = 0;
int boot_auto_target_idx = -1;

uint32_t uiUpdatesPerSec = 0;

void optimizeTaskPriorities();
void syncRTCWithCellular();
String getRTCTimeHHMM();
String getRTCFullDateTime();
void keyDetectionTask(void *pvParameters);
void networkInitTask(void *pvParameters);
bool startMqttTaskIfReady(const char *sourceTag = nullptr);

static void anim_set_size(void *obj, int32_t v) {
  lv_obj_set_size((lv_obj_t *)obj, v, 4);
}

static void anim_fade_in(void *obj, int32_t v) {
  lv_obj_set_style_opa((lv_obj_t *)obj, v, 0);
}

void setup() {
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("[MAIN] Wakeup from deep sleep by K1 button");
  } else {
    Serial.println("[MAIN] Normal boot or power-on reset");
  }

  Serial.begin(115200);
  Serial.println();
  Serial.printf("[FW] Version: %s\n", FIRMWARE_VERSION);
  Serial.printf("[FW] Build: %s\n", FIRMWARE_BUILD_STAMP);

  // 鍦ㄧ郴缁熷垵濮嬪寲瀹屾垚鍚庡啀璇诲彇NVS涓殑骞冲彴鍦板潃
  configManager.loadPlatformAddressFromStorage();

  esp_task_wdt_init(30, true);

  strokeDataMgr.begin();
  Serial.println("[MAIN] Task Watchdog enabled (30s timeout)");

  Wire.begin(I2C_SDA, I2C_SCL);
  TCA.begin();
  TCA.pinMode1(1, OUTPUT);
  lcd_reset();
  Serial.println("LVGL SquareLine Studio UI Test - LVGL v8.3.11");

  if (!touch.begin(Wire, FT6X36_SLAVE_ADDRESS)) {
    Serial.println("Failed to find FT6X36 - check your wiring!");
    while (1) {
      delay(1000);
    }
  }

  lv_init();

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->setRotation(1);
  gfx->fillScreen(RGB565_BLACK);

#ifdef GFX_BL
  // 浣跨敤BrightnessManager鍒濆鍖朠WM鎺у埗锛屾浛浠ｇ畝鍗曠殑寮€鍏虫帶鍒?
  brightness.begin();
#endif

  screenWidth = gfx->width();
  screenHeight = gfx->height();
  bufSize = screenWidth * 120;

  disp_draw_buf1 = (lv_color_t *)heap_caps_malloc(
      bufSize * 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
  if (!disp_draw_buf1) {
    Serial.println("FATAL: Display buffer 1 allocation failed!");
    Serial.printf("Required memory: %d bytes\n", bufSize * 2);
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
    bufSize = screenWidth * 60;
    disp_draw_buf1 =
        (lv_color_t *)heap_caps_malloc(bufSize * 2, MALLOC_CAP_DEFAULT);
    if (!disp_draw_buf1) {
      Serial.println(
          "FATAL: Even reduced buffer allocation failed! Restarting...");
      delay(3000);
      esp_restart();
    }
    Serial.println("WARNING: Using reduced buffer size for stability");
    disp_draw_buf2 = nullptr;
  } else {
    disp_draw_buf2 = (lv_color_t *)heap_caps_malloc(
        bufSize * 2, MALLOC_CAP_DEFAULT | MALLOC_CAP_8BIT);
    if (!disp_draw_buf2) {
      Serial.println(
          "WARNING: Display buffer 2 allocation failed, using single buffer");
      disp_draw_buf2 = nullptr;
    }
  }

  Serial.printf(
      "[MEMORY] Display buffers allocated: buf1=%p, buf2=%p, size=%d\n",
      disp_draw_buf1, disp_draw_buf2, bufSize);

  lv_disp_draw_buf_init(&draw_buf, disp_draw_buf1, disp_draw_buf2, bufSize);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  disp_drv.rotated = LV_DISP_ROT_180;
  lv_disp_drv_register(&disp_drv);

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  lv_indev_drv_register(&indev_drv);

  Serial.println("[MAIN] 鍒濆鍖朠owerManager...");
  if (!powerMgr.begin()) {
    Serial.println("PowerManager initialization failed!");
  } else {
    powerMgr.printDetailedBatteryStatus();
  }

  if (rtc.begin(Wire)) {
    rtcInitialized = true;

    RTC_DateTime datetime = rtc.getDateTime();
    if (datetime.getYear() >= 2024) {
      rtcTimeSynced = true;
    } else {
      Serial.println("[RTC] 鏃堕棿鏃犳晥锛岀瓑寰呯綉缁滃悓姝?..");
    }
  } else {
    Serial.println("[RTC] PCF85063 init failed; check wiring");
  }

  gnss.begin();
  gnss.setSDCardManager(&sdCardManager); // 璁剧疆SD鍗＄鐞嗗櫒鐢ㄤ簬璁板綍NMEA鏁版嵁
  imu.begin();
  cellular.begin();

  dataMutex = xSemaphoreCreateMutex();
  if (!dataMutex) {
    Serial.println("FATAL: Data mutex creation failed!");
    esp_restart();
  }

  serial4GMutex = xSemaphoreCreateMutex();
  if (!serial4GMutex) {
    Serial.println("FATAL: 4G serial mutex creation failed!");
    esp_restart();
  }

  uiStateMutex = xSemaphoreCreateMutex();
  if (!uiStateMutex) {
    Serial.println("FATAL: UI state mutex creation failed!");
    esp_restart();
  }

  BaseType_t networkTaskResult = xTaskCreatePinnedToCore(
      networkInitTask, "NetworkInit", 8192, NULL, 1, NULL, 1);
  if (networkTaskResult != pdPASS) {
    Serial.println("[MAIN] ERROR: failed to create network init task");
  }

  show_boot_animation();

  delay(3000);

  lv_obj_clean(lv_scr_act());

  ui_init();
  ui_initialized = true;

  powerMgr.initBatteryUI();

  training.imageIndicator = ui_Image5;
  training.timeLabel = ui_Label7;
  lv_label_set_text(ui_Label7, "00:00");

  if (rtcInitialized && rtcTimeSynced) {
    String rtcTime = getRTCTimeHHMM();
    lv_label_set_text(ui_Label10, rtcTime.c_str());
  }

  switch_to_screen(SCREEN1);

  if (sdCardManager.begin()) {
    Serial.println("SD Card initialized successfully.");
  } else {
    Serial.println("SD Card initialization failed.");
  }

  // WiFi浼犺緭灏嗗湪缃戠粶浠诲姟瀹屾垚鍚庡惎鍔紙浠ヤ娇鐢ㄨ埞缁勭紪鍙凤級

  updatePanel1();
  updatePanel8();

  lv_timer_handler();
  delay(100);

  pinMode(K1_PIN, INPUT_PULLUP);
  pinMode(K2_PIN, INPUT_PULLUP);
  pinMode(K3_PIN, INPUT_PULLUP);
  pinMode(K4_PIN, INPUT_PULLUP);

  BT::begin();
  BT::startTask();

  keyQueue = xQueueCreate(KEY_QUEUE_SIZE, sizeof(KeyEvent));
  if (!keyQueue) {
    Serial.println("FATAL: Key queue creation failed!");
    esp_restart();
  }

  xTaskCreatePinnedToCore(keyDetectionTask, "KeyDetect", 2048, nullptr, 3,
                          &keyTaskHandle, 0);

  if (!keyTaskHandle) {
    Serial.println("FATAL: Key detection task creation failed!");
    esp_restart();
  }

  // IMU鏃ュ織宸叉敼涓虹洿鎺ュ啓鍏ワ紙鍐呴儴缂撳啿锛夛紝涓嶅啀闇€瑕侀槦鍒楀拰寮傛浠诲姟

  optimizeTaskPriorities();

  // WiFi灏嗗湪缃戠粶浠诲姟瀹屾垚鍚庤嚜鍔ㄥ惎鍔?

  boot_auto_once_done = false;
  boot_auto_once_stage = 0;
  boot_auto_once_ts = millis();
  boot_auto_target_idx = -1;
}

void optimizeTaskPriorities() { vTaskPrioritySet(NULL, 2); }

void loop() {
  static bool loopWdtRegistered = false;
  if (!loopWdtRegistered) {
    esp_task_wdt_add(NULL);
    Serial.println("[MAIN] Loop registered to Task Watchdog");
    loopWdtRegistered = true;
  }

  static unsigned long lastSystemInteractionTime =
      millis(); // 璁板綍鏈€鍚庝竴娆＄郴缁熶氦浜?鎸夐敭/璁粌)鏃堕棿
  unsigned long now = millis();

  // High frequency sensor update
  imu.update();
  if (wifiTransfer.isActive()) {
    wifiTransfer.update();
  }
  esp_task_wdt_reset();

  // 灞忓箷浜害绠＄悊鏇存柊 (姣?00ms妫€鏌ヤ竴娆?
  static unsigned long lastBrightnessUpdate = 0;
  if (now - lastBrightnessUpdate > 100) {
    brightness.update(now, training.isActive(), lastSystemInteractionTime);
    lastBrightnessUpdate = now;
  }

  static unsigned long lastLogTime = 0;
  static unsigned long lastSdStatusPrint = 0;

  if (now - lastLogTime > 16) { // 16ms interval = 62.5Hz锛屼笌IMU閲囨牱鐜囦竴鑷?
    lastLogTime = now;

    // 鍗充娇RTC鏈悓姝ワ紝涔熻褰旾MU鏁版嵁锛堜娇鐢╩illis锛?
    if (training.isActive()) {
      float ax, ay, az;
      imu.getAcceleration(ax, ay, az);

      // 浣跨敤now浣滀负鏃堕棿鎴筹紝纭繚璁板綍鐨勬槸閲囨牱鏃堕棿鑰岄潪鍐欏叆鏃堕棿
      sdCardManager.logImuData(now, ax, ay, az);
    }

    if (sdCardManager.isDisabled() && (now - lastSdStatusPrint > 30000)) {
      lastSdStatusPrint = now;
      Serial.println("[SD] 鈿狅笍  SD card is disabled, data logging suspended");
    }
  }

  static unsigned long lastLoopTime = 0;
  if (now - lastLoopTime > 1000) {
    powerMgr.printMemoryStatus();

    static unsigned long lastDegradeCheck = 0;
    if (now - lastDegradeCheck > 60000) {
      bool anyDegraded = false;

      if (sdCardManager.isDisabled()) {
        Serial.println("[绯荤粺]  SD鍗″凡闄嶇骇绂佺敤锛屾暟鎹褰曞凡鏆傚仠");
        anyDegraded = true;
      }

      if (networkInitGracefullyDegraded) {
        unsigned long remaining =
            (NETWORK_RETRY_RESET_INTERVAL - (now - networkRetryStartTime)) /
            1000;
        if (remaining > 0) {
          Serial.printf("[绯荤粺] 馃敾 缃戠粶闄嶇骇涓紝杩樺墿%lu绉抃n", remaining);
          anyDegraded = true;
        } else {
          networkInitGracefullyDegraded = false;
          networkRetryCount = 0;
          Serial.println("[SYSTEM] Network degrade window ended; retry allowed");
        }
      }

      if (!anyDegraded) {
        static unsigned long lastHealthReport = 0;
        if (now - lastHealthReport > 300000) {
          Serial.println("[SYSTEM] Health check OK; all modules stable");
          lastHealthReport = now;
        }
      }

      lastDegradeCheck = now;
    }
  }
  lastLoopTime = now;

  static uint32_t lastApiRetryTime = 0;

  if (networkTaskCompleted && !configManager.isConfigReady() &&
      (now - lastApiRetryTime > 10000) && !configLoadingInProgress) {
    configLoadingInProgress = true;
    configManager.startConfigLoading();
    configLoadingInProgress = false;
    lastApiRetryTime = now;
  }

  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("SETAPI=")) {
      String address = cmd.substring(7);
      address.trim();

      Serial.println("\n[CMD] 鏀跺埌鍦板潃鏇存敼璇锋眰: " + address);

      if (configManager.getReloadPhase() != ConfigManager::RELOAD_IDLE) {
        Serial.println("\n鉂?閲嶈浇姝ｅ湪杩涜涓紝璇风◢鍚庡啀璇曪紒\n");
        return;
      }

      // 妫€鏌ュ綋鍓嶆槸鍚﹀浜?绛夊緟閰嶇疆"鐘舵€侊紙鍗矵ost涓虹┖锛?
      bool isWaitingForConfig = configManager.getPlatformHost().isEmpty();

      if (isWaitingForConfig) {
        // 鍒濆璁剧疆锛岀洿鎺ヨ皟鐢?setPlatformAddress
        if (configManager.setPlatformAddress(address)) {
          Serial.println("\n鉁?鍒濆骞冲彴鍦板潃宸茶缃紝绯荤粺灏嗚嚜鍔ㄥ紑濮嬪姞杞介厤缃甛n");
        } else {
          Serial.println("\n鉂?鍦板潃璁剧疆澶辫触锛岃妫€鏌ユ牸寮?(IP:PORT)\n");
        }
        return;
      }

      // 杩愯鏃堕噸杞?
      Serial.println("[CMD] 姝ｅ湪鍋滄鏃т换鍔″苟閲嶈浇鍦板潃...");

      if (configManager.safeReloadPlatformAddress(address)) {
        Serial.println("\n鉁?骞冲彴鍦板潃瀹夊叏閲嶈浇鎴愬姛!");
        Serial.println("绯荤粺宸蹭娇鐢ㄦ柊鍦板潃閲嶆柊鑾峰彇閰嶇疆\n");
      } else {
        Serial.println("\n鉂?骞冲彴鍦板潃閲嶈浇澶辫触!");
        Serial.println("鍘熷洜锛氬湴鍧€鏍煎紡閿欒銆佽祫婧愰噴鏀捐秴鏃舵垨PPP鎷ㄥ彿澶辫触");
        Serial.println("璇锋鏌ュ悗閲嶈瘯\n");
      }
    } else if (cmd == "GETAPI") {
      Serial.println("褰撳墠骞冲彴鍦板潃: " + configManager.getPlatformAddress());
    } else if (cmd == "HELP" || cmd == "help") {
      Serial.println("\n===== 涓插彛鍛戒护甯姪 =====");
      Serial.println(
          "SETAPI=<IP:PORT>  - 璁剧疆骞冲彴鍦板潃 (渚? SETAPI=117.83.111.19:10033)");
      Serial.println("                    "
                     "Note: stops MQTT/DataFlow, releases resources, then redials");
      Serial.println("GETAPI            - 鏌ヨ褰撳墠骞冲彴鍦板潃");
      Serial.println("HELP              - 鏄剧ず甯姪淇℃伅");
      Serial.println("========================\n");
    }
  }

  if (!boot_auto_once_done) {
    switch (boot_auto_once_stage) {
    case 0: {
      if (configManager.isRowerListReady()) {
        BT::triggerScan();
        boot_auto_once_stage = 1;
        boot_auto_once_ts = now;
      } else if (now - boot_auto_once_ts > 30000) {
        BT::triggerScan();
        boot_auto_once_stage = 1;
        boot_auto_once_ts = now;
      }
      break;
    }
    case 1: {
      if (BT::getFoundDeviceCount() > 0) {
        for (int i = 0; i < BT::NUM_PRESETS; ++i) {
          auto &d = BT::devices()[i];
          if (d.found && d.address[0] != '\0') {
            boot_auto_target_idx = i;
            break;
          }
        }
        if (boot_auto_target_idx >= 0) {
          BT::requestConnect(boot_auto_target_idx);
          boot_auto_once_stage = 2;
          boot_auto_once_ts = now;
        }
      }
      if (now - boot_auto_once_ts > 10000) {
        boot_auto_once_done = true;
      }
      break;
    }
    case 2: {
      if (boot_auto_target_idx >= 0) {
        auto &d = BT::devices()[boot_auto_target_idx];
        if (d.connected) {
          BT::setUploadSource(&d);
          boot_auto_once_stage = 3;
          boot_auto_once_done = true;
        }
      }
      if (now - boot_auto_once_ts > 10000) {
        boot_auto_once_done = true;
      }
      break;
    }
    default:
      boot_auto_once_done = true;
      break;
    }
  }

  static bool wasInScreen2 = false;
  static bool wasInScreen3ForFocus = false;

  bool isInScreen2 = (lv_scr_act() == ui_Screen2);
  bool isInScreen3ForFocus = (lv_scr_act() == ui_Screen3);

  if (isInScreen2 && !wasInScreen2) {
    safeSetCurrentScreen(SCREEN2);

    if (!group_screen2) {
      group_screen2 = lv_group_create();
      lv_group_add_obj(group_screen2, ui_Button3);
      lv_group_add_obj(group_screen2, ui_Button5);
    } else {
      lv_group_remove_all_objs(group_screen2);
      lv_group_add_obj(group_screen2, ui_Button3);
      lv_group_add_obj(group_screen2, ui_Button5);
    }

    lv_indev_set_group(lv_indev_get_next(NULL), group_screen2);
    screen2_focus_objs[0] = ui_Button3;
    screen2_focus_objs[1] = ui_Button5;

    screen2_focus_idx = screen2_remembered_focus_idx;

    lv_obj_clear_flag(ui_Button3, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_Button5, LV_OBJ_FLAG_HIDDEN);

    lv_group_focus_obj(screen2_focus_objs[screen2_remembered_focus_idx]);

    if (screen2_remembered_focus_idx == 0) {
      lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
    } else {
      lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
      lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
    }
  }

  if (isInScreen3ForFocus && !wasInScreen3ForFocus) {
    safeSetCurrentScreen(SCREEN3);
    safeSetScreen3SelectedIdx(0);

    if (!group_screen3) {
      group_screen3 = lv_group_create();
    }
    lv_indev_set_group(lv_indev_get_next(NULL), group_screen3);

    if (screen3_buttons_created && screen3_button_count > 0 &&
        screen3_btns[0]) {
      lv_group_focus_obj(screen3_btns[0]);
      safeSetScreen3SelectedIdx(0);
    }
  }

  if (!isInScreen3ForFocus && wasInScreen3ForFocus) {
    screen3_enter_time = 0;

    BT::setAutoScanEnabled(true);
    BT::setContinuousScan(false);
  }

  if (!isInScreen2 && !isInScreen3ForFocus) {
    bool isInScreen4 = (lv_scr_act() == ui_Screen4);
    if (isInScreen4) {
      safeSetCurrentScreen(SCREEN4);
    } else {
      safeSetCurrentScreen(SCREEN1);
    }
  }

  wasInScreen2 = isInScreen2;
  wasInScreen3ForFocus = isInScreen3ForFocus;

  KeyEvent event;
  while (xQueueReceive(keyQueue, &event, 0) == pdTRUE) {
    const unsigned long eventTime = event.timestampMs;
    lastSystemInteractionTime = now; // 鎸夐敭閲嶇疆璁℃椂鍣?
    brightness.resetInteraction();   // 绔嬪嵆鎭㈠鏈€楂樹寒搴?
    switch (event.pin) {
    case K1_PIN:
      if (event.action) {
        ScreenId currentScreen = safeGetCurrentScreen();
        switch (currentScreen) {
        case SCREEN1:
          switch_to_screen(SCREEN2);
          break;
        case SCREEN2:
          switch_to_screen(SCREEN1);
          break;
        case SCREEN3:
          switch_to_screen(SCREEN2);
          break;
        case SCREEN4:
          switch_to_screen(SCREEN2);
          break;
        }
      }
      break;

    case K2_PIN:
      if (event.action) {
        ScreenId currentScreen = safeGetCurrentScreen();
        switch (currentScreen) {
        case SCREEN1:
          panel1ContentIndex = (panel1ContentIndex + 1) % 7;
          updatePanel1();
          break;
        case SCREEN2:
          screen2_focus_idx = (screen2_focus_idx + 1) % 2;
          screen2_remembered_focus_idx = screen2_focus_idx;

          lv_group_focus_obj(screen2_focus_objs[screen2_focus_idx]);

          if (screen2_focus_idx == 0) {
            lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
            lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
          } else {
            lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
            lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
          }

          lv_timer_handler();

          lastScreen2KeyPress = now;
          break;
        case SCREEN3:
          if (screen3_button_count > 0) {
            syncScreen3FocusState();

            int currentIdx = safeGetScreen3SelectedIdx();
            int newIdx =
                (currentIdx - 1 + screen3_button_count) % screen3_button_count;
            safeSetScreen3SelectedIdx(newIdx);

            if (newIdx < screen3_button_count && screen3_btns[newIdx]) {
              lv_group_focus_obj(screen3_btns[newIdx]);
            } else {
              int fallbackIdx = screen3_button_count - 1;
              safeSetScreen3SelectedIdx(fallbackIdx);
              if (fallbackIdx >= 0 && screen3_btns[fallbackIdx]) {
                lv_group_focus_obj(screen3_btns[fallbackIdx]);
              }
            }
          }
          break;
        }
      }
      break;

    case K3_PIN:
      if (event.action) {
        ScreenId currentScreen = safeGetCurrentScreen();
        switch (currentScreen) {
        case SCREEN1:
          panel8ContentIndex = (panel8ContentIndex + 1) % 7;
          updatePanel8();
          break;
        case SCREEN2:
          screen2_focus_idx = (screen2_focus_idx - 1 + 2) % 2;
          screen2_remembered_focus_idx = screen2_focus_idx;

          lv_group_focus_obj(screen2_focus_objs[screen2_focus_idx]);

          if (screen2_focus_idx == 0) {
            lv_obj_add_state(ui_Label37, LV_STATE_FOCUSED);
            lv_obj_clear_state(ui_Label38, LV_STATE_FOCUSED);
          } else {
            lv_obj_clear_state(ui_Label37, LV_STATE_FOCUSED);
            lv_obj_add_state(ui_Label38, LV_STATE_FOCUSED);
          }

          lv_timer_handler();

          lastScreen2KeyPress = now;
          break;
        case SCREEN3:
          if (screen3_button_count > 0) {
            syncScreen3FocusState();

            int currentIdx = safeGetScreen3SelectedIdx();
            int newIdx = (currentIdx + 1) % screen3_button_count;
            safeSetScreen3SelectedIdx(newIdx);

            if (newIdx < screen3_button_count && screen3_btns[newIdx]) {
              lv_group_focus_obj(screen3_btns[newIdx]);
            } else {
              safeSetScreen3SelectedIdx(0);
              if (screen3_btns[0]) {
                lv_group_focus_obj(screen3_btns[0]);
              }
            }
          }
          break;
        }
      }
      break;

    case K4_PIN:
      if (event.action) {
        k4PressTime = eventTime;
        k4PressedInScreen1 = (safeGetCurrentScreen() == SCREEN1);
        k4IsPressed = true;
      } else {
        k4IsPressed = false;
        unsigned long pressDuration = eventTime - k4PressTime;

        if (k4PressTime == 0) {
          pressDuration = 0;
        } else if (pressDuration > 60000) {
          pressDuration = 0;
        }

        if (!k4PressedInScreen1 && !k4ToggledThisPress &&
            pressDuration < trainingLongPress) {
          ScreenId currentScreen = safeGetCurrentScreen();
          switch (currentScreen) {
          case SCREEN2:
            screen2_remembered_focus_idx = screen2_focus_idx;
            switch_to_screen(screen2_focus_idx == 0 ? SCREEN3 : SCREEN4);
            break;
          case SCREEN3: {
            int selectedIdx = safeGetScreen3SelectedIdx();
            if (selectedIdx < screen3_button_count) {
              int devIdx = screen3_device_index_map[selectedIdx];

              if (devIdx >= 1000) {
                int apiIdx = devIdx - 1000;
                const auto &rowerList = configManager.getRowerList();
                if (apiIdx < rowerList.size()) {
                  const auto &rower = rowerList[apiIdx];

                  int scannedDevIdx = -1;
                  for (int j = 0; j < BT::getFoundDeviceCount(); j++) {
                    String scannedAddr = String(BT::devices()[j].address);
                    if (scannedAddr.equalsIgnoreCase(rower.btAddr)) {
                      scannedDevIdx = j;
                      break;
                    }
                  }

                  if (scannedDevIdx != -1) {
                    auto &dev = BT::devices()[scannedDevIdx];

                    if (dev.connected) {
                      BT::setUploadSource(&dev);
                    } else if (dev.found) {
                      BT::requestConnect(scannedDevIdx);
                    }
                  }
                }
              } else if (devIdx >= 0) {
                auto &dev = BT::devices()[devIdx];

                if (dev.connected) {
                  BT::setUploadSource(&dev);
                } else if (dev.found) {
                  BT::requestConnect(devIdx);
                }
              }
              BT::refreshScreen3UI(screen3_btns, screen3_battery_labels,
                                   selectedIdx, screen3_button_count,
                                   screen3_device_index_map);
            }
          } break;
          case SCREEN1:
            break;
          }
        } else if (k4PressedInScreen1 && pressDuration <= trainingTapMaxPress &&
                   safeGetCurrentScreen() == SCREEN1) {
          if (k4LastTapTimeScreen1 > 0 &&
              (eventTime - k4LastTapTimeScreen1) <= trainingDoubleClickWindow) {
            if (!training.isActive()) {
              training.start();
              Serial.println("========================================");
              Serial.println("✅ [训练模式] 双击触发 - 开始训练");
              Serial.println("========================================");
            } else {
              training.stop();
              Serial.println("========================================");
              Serial.println("✅ [训练模式] 双击触发 - 停止训练");
              Serial.println("========================================");
            }
            trainingActive = training.isActive();
            k4LastTapTimeScreen1 = 0;
          } else {
            k4LastTapTimeScreen1 = eventTime;
          }
        }

        k4ToggledThisPress = false;
        k4PressTime = 0;
        k4PressedInScreen1 = false;
      }
      break;
    }

    lv_timer_handler();
  }
  k4ToggledThisPress = false;

  if (!k4IsPressed && k4PressTime > 0) {
  }

  static ScreenId lastScreen = SCREEN1;
  ScreenId currentScreen = safeGetCurrentScreen();

  if (currentScreen != SCREEN1 && lastScreen == SCREEN1) {
    k4PressedInScreen1 = false;
    k4ToggledThisPress = false;
    k4IsPressed = false;
    k4LastTapTimeScreen1 = 0;
  }

  lastScreen = currentScreen;

  static int lastStrokeCount = 0;
  static uint32_t lastGNSSUpdate = 0;
  static uint32_t lastSensorUIUpdate = 0;

  if (now - lastGNSSUpdate > 300) {
    gnss.process();

    float newSpeedMps = gnss.getSpeed();

    float tempStrokeRate, tempTotalDistance;
    int tempStrokeCount;
    configManager.safeGetSensorData(tempStrokeRate, speedMps, tempStrokeCount,
                                    tempTotalDistance);
    configManager.safeUpdateSensorData(tempStrokeRate, newSpeedMps,
                                       tempStrokeCount, tempTotalDistance);

    String newLocalTime;
    if (rtcInitialized && rtcTimeSynced) {
      newLocalTime = getRTCTimeHHMM();
    } else {
      newLocalTime = configManager.getTimeHHMM();
    }
    configManager.safeUpdateTimeData(newLocalTime);

    lastGNSSUpdate = now;
    esp_task_wdt_reset();
  }

  static uint32_t lastCellularProcess = 0;
  if (now - lastCellularProcess > 100) {
    cellular.process();
    lastCellularProcess = now;
  }

  // 妫€鏌ユ槸鍚︽湁鏂板垝妗ㄦ暟鎹紙浜嬩欢椹卞姩 - 绔嬪嵆鏇存柊锛屼笉鍙?100/200ms 闄愬埗锛?
  if (imu.hasNewStroke()) {
    const StrokeMetrics &metrics = imu.getLastStrokeMetrics();

    // 鏇存柊妗ㄦ暟鍜屾〃棰?
    strokeRate = imu.getStrokeRate();
    strokeCount = metrics.strokeNumber;
    int currentStrokeCount = strokeCount;
    float currentSpeedMps = gnss.getSpeed();

    // 瑙﹀彂璁粌閫昏緫鍜屾暟鎹崟鑾凤紙浼氳绠楄窛绂诲苟鏇存柊鍏ㄥ眬鍙橀噺锛?
    training.onStrokeDetected();
    strokeDataMgr.captureStroke(currentStrokeCount);

    // 鐜板湪strokeLength鍜宼otalDistance宸茶StrokeDataManager鏇存柊锛屽彲浠ョ敤浜嶶I鏄剧ず

    // 鏇存柊 ConfigManager
    configManager.safeUpdateSensorData(strokeRate, currentSpeedMps,
                                       currentStrokeCount, totalDistance);

    // 鏇存柊 UI 鏄剧ず
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%.1f", strokeRate);
    lv_label_set_text(ui_Label9, tmp);

    snprintf(tmp, sizeof(tmp), "%d", currentStrokeCount);
    lv_label_set_text(ui_Label44, tmp);
    lv_label_set_text(ui_Label64, tmp);

    snprintf(tmp, sizeof(tmp), "%.1f", strokeLength);
    lv_label_set_text(ui_Label12, tmp);
    lv_label_set_text(ui_Label56, tmp);

    snprintf(tmp, sizeof(tmp), "%.3f", totalDistance / 1000.0f);
    lv_label_set_text(ui_Label23, tmp);
    lv_label_set_text(ui_Label60, tmp);

    // 绔嬪嵆鍒锋柊UI鏄剧ず锛屽噺灏戝欢杩燂紙浜嬩欢椹卞姩鍒锋柊锛?
    lv_timer_handler();

    // 娓呴櫎鏍囧織
    imu.clearNewStrokeFlag();
    lastStrokeCount = currentStrokeCount;

    // 绔嬪嵆鍚屾鏁版嵁缁戝畾
    updateBoundData();
  }

  uint32_t sensorUpdateInterval =
      (safeGetCurrentScreen() == SCREEN3) ? 100 : 200;
  if (now - lastSensorUIUpdate > sensorUpdateInterval) {
    char tmp[16];

    float displaySpeedMps = 0.0f;
    String displayLocalTime = "--:--";
    float tempStrokeRate, tempTotalDistance;
    int tempStrokeCount;
    configManager.safeGetSensorData(tempStrokeRate, displaySpeedMps,
                                    tempStrokeCount, tempTotalDistance);
    configManager.safeGetTimeData(displayLocalTime);

    // 閫熷害/鍔熺巼/閰嶉€熸瘡绉掑埛鏂颁竴娆?
    static uint32_t lastSpeedPowerPaceUpdate = 0;
    if (now - lastSpeedPowerPaceUpdate > 1000) {
      snprintf(tmp, sizeof(tmp), "%.1f", displaySpeedMps);
      lv_label_set_text(ui_Label46, tmp);
      lv_label_set_text(ui_Label68, tmp);

      snprintf(tmp, sizeof(tmp), "%.0f", gnss.getPower());
      lv_label_set_text(ui_Label48, tmp);
      lv_label_set_text(ui_Label72, tmp);

      lv_label_set_text(ui_Label8, gnss.getPaceString().c_str());

      lastSpeedPowerPaceUpdate = now;
    }

    String displayTimeStr;
    if (rtcInitialized && rtcTimeSynced) {
      displayTimeStr = getRTCTimeHHMM();
    } else {
      displayTimeStr = displayLocalTime;
    }
    lv_label_set_text(ui_Label10, displayTimeStr.c_str());

    int solvingSats = gnss.getSolvingSatellites();
    bool hasGnssData = gnss.hasDataReceived();
    static unsigned long lastGnssLabelUpdate = 0;

    if (now - lastGnssLabelUpdate >= GNSS_LABEL_UPDATE_INTERVAL_MS) {
      lastGnssLabelUpdate = now;

      if (hasGnssData) {
        snprintf(tmp, sizeof(tmp), "%02d", solvingSats);
        lv_label_set_text(ui_Label15, tmp);
      } else {
        lv_label_set_text(ui_Label15, "00");
      }

      if (hasGnssData) {
        String fixStatus = gnss.getFixStatus();
        if (fixStatus.isEmpty()) {
          fixStatus = "--";
        }
        lv_label_set_text(ui_Label58, fixStatus.c_str());
      } else {
        lv_label_set_text(ui_Label58, "");
      }

      if (hasGnssData) {
        String diffAge = gnss.getDiffAge();
        if (diffAge.isEmpty()) {
          diffAge = "--";
        }
        lv_label_set_text(ui_Label65, diffAge.c_str());
      } else {
        lv_label_set_text(ui_Label65, "");
      }

      if (hasGnssData) {
        String hdop = gnss.getHDOP();
        if (hdop.isEmpty()) {
          hdop = "--";
        } else if (hdop == "--") {
        } else {
          float hdopValue = hdop.toFloat();
          if (hdopValue > 10.0f) {
            hdop = "--";
          }
        }
        lv_label_set_text(ui_Label43, hdop.c_str());
      } else {
        lv_label_set_text(ui_Label43, "");
      }
    }

    // 瀹氭湡鏇存柊妯″紡锛氫粎鏇存柊琛板噺鐨勬〃棰戯紙濡傛灉鏈夋柊鍒掓〃鍒氭墠宸茬粡鏇存柊杩囦簡锛?
    float currentRate = imu.getStrokeRate();
    snprintf(tmp, sizeof(tmp), "%.1f", currentRate);
    lv_label_set_text(ui_Label9, tmp);

    training.update();
    updateBoundData();

    lastSensorUIUpdate = now;
  }
  static uint32_t lastHRUpdate = 0;
  auto activeDev = BT::activeDevice();

  BT::pollUIEvents();

  if (activeDev && activeDev->connected && (now - lastHRUpdate) > 2000) {
    static char hrDisplayBuffer[16];
    memset(hrDisplayBuffer, 0, sizeof(hrDisplayBuffer));
    snprintf(hrDisplayBuffer, sizeof(hrDisplayBuffer), "%d",
             activeDev->lastHeartRate);

    try {
      safeLabelUpdate(ui_Label50, hrDisplayBuffer, "Label50(P1HR)");
      safeLabelUpdate(ui_Label4, hrDisplayBuffer, "Label4(P8HR)");
    } catch (...) {
      Serial.println("[MAIN] Error updating HR labels");
    }

    lastHRUpdate = now;
  } else if (!activeDev && (now - lastHRUpdate) > 2000) {
    try {
      safeLabelUpdate(ui_Label50, "0", "Label50(P1HR)");
      safeLabelUpdate(ui_Label4, "0", "Label4(P8HR)");
      safeLabelUpdate(ui_Label14, "鏈€夋嫨", "Label14(NoDevice)");
      safeLabelUpdate(ui_Label53, "鏈€夋嫨", "Label53(NoDevice)");
    } catch (...) {
      Serial.println("[MAIN] Error clearing HR labels");
    }
    lastHRUpdate = now;
  }

  static uint32_t lastBluetoothCleanup = 0;
  static uint32_t lastMemoryCheck = 0;

  if (now - lastMemoryCheck > 30000) {
    size_t freeHeap = ESP.getFreeHeap();
    size_t minFreeHeap = ESP.getMinFreeHeap();
    if (freeHeap < 50000) {
      lastBluetoothCleanup = now;
    }
    lastMemoryCheck = now;
  }

  if (now - lastBluetoothCleanup > 60000) {
    size_t freeHeap = ESP.getFreeHeap();
    if (freeHeap > 80000) {
      BT::cleanupDisconnectedDevices();
    }
    lastBluetoothCleanup = now;
  }

  static uint32_t lastScreen2Sync = 0;
  static uint32_t lastScreen2KeyPress = 0;

  if (now - lastScreen2KeyPress < 500) {
    lastScreen2Sync = now;
  }

  if (safeGetCurrentScreen() == SCREEN2 && (now - lastScreen2Sync) > 500) {
    syncScreen2LabelStates();
    lastScreen2Sync = now;
  }

  static uint32_t lastScreen3FocusSync = 0;
  if (safeGetCurrentScreen() == SCREEN3 && (now - lastScreen3FocusSync) > 100) {
    syncScreen3FocusState();
    lastScreen3FocusSync = now;
  }

  static int globalLastConnectedCount = -1;
  static uint32_t lastConnectionCountUpdate = 0;
  if (now - lastConnectionCountUpdate > 500) {
    int currentConnectedCount = BT::getConnectedCount();
    if (currentConnectedCount != globalLastConnectedCount) {
      char countText[16];
      snprintf(countText, sizeof(countText), "%d", currentConnectedCount);
      lv_label_set_text(ui_Label61, countText);
      globalLastConnectedCount = currentConnectedCount;
    }
    lastConnectionCountUpdate = now;
  }

  static uint32_t lastUIRef = 0;

  if (safeGetCurrentScreen() == SCREEN3 && (now - lastUIRef) > 100) {

    if (screen3_enter_time == 0) {
      screen3_enter_time = now;
      screen3_scan_triggered = false;
      if (screen3_button_count == 0) {
        safeSetScreen3SelectedIdx(0);
        for (int i = 0; i < 9; ++i)
          screen3_device_index_map[i] = -1;
      }

      BT::setAutoScanEnabled(false);
      BT::setContinuousScan(true);
    }

    if (!screen3_scan_triggered) {
      if (configManager.isRowerListReady()) {
        BT::triggerScan();
        screen3_scan_triggered = true;
      } else {
        static uint32_t lastApiCheck = 0;
        if (now - lastApiCheck > 3000) {
          lastApiCheck = now;
        }
      }
    }

    if (!screen3_buttons_created) {
      if (!group_screen3) {
        group_screen3 = lv_group_create();
      }

      if (configManager.isRowerListReady()) {
        const auto &rowerList = configManager.getRowerList();

        for (int i = 0; i < screen3_button_count; i++) {
          if (screen3_btns[i]) {
            if (group_screen3) {
              lv_group_remove_obj(screen3_btns[i]);
            }
            lv_obj_del(screen3_btns[i]);
          }
          screen3_btns[i] = nullptr;
          screen3_battery_labels[i] = nullptr;
          screen3_bluetooth_icons[i] = nullptr;
          screen3_device_index_map[i] = -1;
        }
        screen3_button_count = 0;

        for (size_t apiIdx = 0; apiIdx < rowerList.size() && apiIdx < 9;
             apiIdx++) {
          const auto &rower = rowerList[apiIdx];

          int idx = screen3_button_count;
          lv_obj_t *btn = lv_btn_create(ui_uiBTListContainer);
          lv_obj_set_size(btn, LV_PCT(100), 50);
          lv_obj_add_flag(btn,
                          LV_OBJ_FLAG_CHECKABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
          lv_group_add_obj(group_screen3, btn);

          lv_obj_add_event_cb(btn, screen3_button_event_cb, LV_EVENT_CLICKED,
                              NULL);
          lv_obj_set_style_bg_color(btn, lv_color_black(),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
          lv_obj_set_style_bg_color(btn, lv_color_black(),
                                    LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_bg_color(btn, lv_color_black(),
                                    LV_PART_MAIN | LV_STATE_CHECKED);
          lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
          lv_obj_set_style_border_width(btn, 3,
                                        LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_border_color(btn, lv_color_white(),
                                        LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_bg_color(btn, lv_color_black(),
                                    LV_PART_MAIN | LV_STATE_PRESSED);
          lv_obj_set_style_shadow_color(btn, lv_color_black(),
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
          lv_obj_set_style_shadow_color(btn, lv_color_black(),
                                        LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_shadow_opa(btn, LV_OPA_50,
                                      LV_PART_MAIN | LV_STATE_DEFAULT);
          lv_obj_set_style_shadow_opa(btn, LV_OPA_50,
                                      LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_shadow_width(btn, 10,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
          lv_obj_set_style_shadow_width(btn, 10,
                                        LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_shadow_spread(btn, 0,
                                         LV_PART_MAIN | LV_STATE_DEFAULT);
          lv_obj_set_style_shadow_spread(btn, 0,
                                         LV_PART_MAIN | LV_STATE_FOCUSED);
          lv_obj_set_style_shadow_ofs_x(btn, 0,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);
          lv_obj_set_style_shadow_ofs_y(btn, 0,
                                        LV_PART_MAIN | LV_STATE_DEFAULT);

          screen3_name_labels[idx] = lv_label_create(btn);
          String displayDeviceCode = rower.deviceCode;
          String displayRowerName = rower.rowerName;
          if (displayDeviceCode.isEmpty() ||
              displayDeviceCode.equalsIgnoreCase("null")) {
            displayDeviceCode = "-";
          }
          if (displayRowerName.isEmpty() ||
              displayRowerName.equalsIgnoreCase("null")) {
            displayRowerName = "-";
          }
          static char nameBuffer[64];
          snprintf(nameBuffer, sizeof(nameBuffer), "%s(%s)",
                   displayDeviceCode.c_str(), displayRowerName.c_str());
          lv_label_set_text(screen3_name_labels[idx], nameBuffer);
          lv_obj_set_style_text_color(screen3_name_labels[idx],
                                      lv_color_white(), 0);
          lv_obj_set_style_text_font(screen3_name_labels[idx], &ui_font_Font23,
                                     0);
          lv_obj_align(screen3_name_labels[idx], LV_ALIGN_LEFT_MID, 10, 0);

          lv_obj_t *statusLabel = lv_label_create(btn);
          lv_label_set_text(statusLabel, "");
          lv_obj_set_style_text_color(statusLabel, lv_color_white(), 0);
          lv_obj_set_style_text_font(statusLabel, &ui_font_Font23, 0);
          lv_obj_align(statusLabel, LV_ALIGN_CENTER, 100, 0);
          screen3_bluetooth_icons[idx] = statusLabel;

          screen3_battery_labels[idx] = lv_label_create(btn);
          lv_label_set_text(screen3_battery_labels[idx], "");
          lv_obj_set_style_text_color(screen3_battery_labels[idx],
                                      lv_color_white(), 0);
          lv_obj_set_style_text_font(screen3_battery_labels[idx],
                                     &ui_font_Font23, 0);
          lv_obj_align(screen3_battery_labels[idx], LV_ALIGN_RIGHT_MID, -10, 0);

          screen3_btns[idx] = btn;
          screen3_device_index_map[idx] = (int)apiIdx + 1000;
          screen3_button_count++;
        }

        screen3_buttons_created = true;
        if (safeGetScreen3SelectedIdx() >= screen3_button_count) {
          safeSetScreen3SelectedIdx(0);
        }

        if (screen3_button_count > 0 && screen3_btns[0]) {
          lv_indev_set_group(lv_indev_get_next(NULL), group_screen3);
          lv_group_focus_obj(screen3_btns[0]);
        }
      } else {
        static uint32_t lastRetryTime = 0;
        if (now - lastRetryTime > 3000) {
          lastRetryTime = now;
        }
      }
    }

    if (screen3_buttons_created) {
      uint32_t now = millis();
      if (now - last_ui_update_time > 500) {
        ui_update_needed = true;
        last_ui_update_time = now;
      }

      if (ui_update_needed) {
        updateScreen3ButtonStatesSafe();
        ui_update_needed = false;
      }
    }

    for (int i = 0; i < screen3_button_count;) {
      int devIdx = screen3_device_index_map[i];
      bool removeThis = false;

      if (devIdx >= 1000) {
        i++;
        continue;
      }

      if (devIdx < 0 || devIdx >= BT::NUM_PRESETS)
        removeThis = true;
      else if (BT::devices()[devIdx].address[0] == '\0')
        removeThis = true;
      if (!removeThis) {
        i++;
        continue;
      }
      if (screen3_btns[i]) {
        if (group_screen3) {
          lv_group_remove_obj(screen3_btns[i]);
        }
        lv_obj_del(screen3_btns[i]);
      }
      for (int k = i + 1; k < screen3_button_count; k++) {
        screen3_btns[k - 1] = screen3_btns[k];
        screen3_battery_labels[k - 1] = screen3_battery_labels[k];
        screen3_bluetooth_icons[k - 1] = screen3_bluetooth_icons[k];
        screen3_device_index_map[k - 1] = screen3_device_index_map[k];
      }
      int tail = screen3_button_count - 1;
      screen3_btns[tail] = nullptr;
      screen3_battery_labels[tail] = nullptr;
      screen3_bluetooth_icons[tail] = nullptr;
      screen3_device_index_map[tail] = -1;
      screen3_button_count--;
      if (safeGetScreen3SelectedIdx() >= screen3_button_count) {
        safeSetScreen3SelectedIdx(max(0, screen3_button_count - 1));
      }
    }

    static int lastConnectedCount = -1;
    int currentConnectedCount = BT::getConnectedCount();

    bool needsRefresh = (currentConnectedCount != lastConnectedCount);

    if (needsRefresh) {
      BT::refreshScreen3UI(screen3_btns, screen3_battery_labels,
                           safeGetScreen3SelectedIdx(), screen3_button_count,
                           screen3_device_index_map);
      lastConnectedCount = currentConnectedCount;
    }

    lastUIRef = now;
  }

  static uint32_t lastLVGLUpdate = 0;

  uint32_t refreshInterval;
  ScreenId currentScreenForRefresh = safeGetCurrentScreen();
  if (currentScreenForRefresh == SCREEN3) {
    refreshInterval = 10;
  } else if (currentScreenForRefresh == SCREEN2 ||
             currentScreenForRefresh == SCREEN1 ||
             currentScreenForRefresh == SCREEN4) {
    refreshInterval = 60;
  } else {
    refreshInterval = 40;
  }

  if (now - lastLVGLUpdate > refreshInterval) {
    lv_timer_handler();
    lastLVGLUpdate = now;
  }

  vTaskDelay(pdMS_TO_TICKS(1));

  static uint32_t lastWatchdogFeed = 0;
  if (now - lastWatchdogFeed > 3000) {
    powerMgr.feedWatchdog();
    lastWatchdogFeed = now;
  }

  static uint32_t lastBatteryUpdate = 0;
  if (now - lastBatteryUpdate > 300000) {
    lastBatteryUpdate = now;
  }

  static uint32_t lastRTCSync = 0;
  if (rtcInitialized && (now - lastRTCSync > 1800000)) {
    Serial.println("[RTC] 鎵ц瀹氭湡鏃堕棿鍚屾...");
    syncRTCWithCellular();
    lastRTCSync = now;
  }

  static unsigned long lastBatteryUIUpdate = 0;
  if (millis() - lastBatteryUIUpdate >= 2000) {
    powerMgr.update();
    powerMgr.updateBatteryUI();
    lastBatteryUIUpdate = millis();
  }

  static unsigned long lastDetailedBatteryPrint = 0;
  if (millis() - lastDetailedBatteryPrint >= 60000) {
    lastDetailedBatteryPrint = millis();
  }

  static uint32_t lastMinuteBatteryUIUpdate = 0;
  if (now - lastMinuteBatteryUIUpdate > 60000) {
    if (safeGetCurrentScreen() == SCREEN1) {
      powerMgr.updateBatteryUI();
    }
    lastMinuteBatteryUIUpdate = now;
  }

  // ==========================================================================================
  // 鑷姩鍏虫満閫昏緫 (5鍒嗛挓鏃犳搷浣滀笖涓嶅湪璁粌妯″紡)
  // ==========================================================================================

  // 1. 濡傛灉鍦ㄨ缁冩ā寮忥紝涓嶆柇閲嶇疆璁℃椂鍣?
  if (training.isActive()) {
    lastSystemInteractionTime = now;
  }

  // 2. 濡傛灉K4姝ｅ湪琚暱鎸?(k4IsPressed)
  if (k4IsPressed) {
    lastSystemInteractionTime = now;
  }

  // 3. 瑙︽懜灞忔椿璺冩椂闂?(LVGL鑷姩缁存姢)
  uint32_t touchInactiveTime = lv_disp_get_inactive_time(NULL);

  const unsigned long AUTO_SHUTDOWN_TIMEOUT = 300000;

  if ((now - lastSystemInteractionTime > AUTO_SHUTDOWN_TIMEOUT) &&
      (touchInactiveTime > AUTO_SHUTDOWN_TIMEOUT) && !training.isActive()) {

    Serial.println("[System] 馃挙 鑷姩鍏虫満瑙﹀彂 (5鍒嗛挓鏃犳搷浣?");
    powerMgr.shutdown();
  }

  delay(5);
}

int networkRetryCount = 0;
bool networkInitGracefullyDegraded = false;
const int MAX_NETWORK_RETRIES = 2;
const unsigned long NETWORK_RETRY_RESET_INTERVAL = 600000;
unsigned long networkRetryStartTime = 0;

bool startMqttTaskIfReady(const char *sourceTag) {
  const char *tag = sourceTag ? sourceTag : "[NETWORK]";

  if (mqttTaskHandle != nullptr) {
    Serial.printf("%s MQTT浠诲姟宸插湪杩愯锛岃烦杩囧垱寤篭n", tag);
    return true;
  }

  if (!configManager.isMQTTConfigReady()) {
    Serial.printf("%s MQTT閰嶇疆鏈氨缁紝鏃犳硶鍚姩MQTT浠诲姟\n", tag);
    return false;
  }

  const auto &mqttConfig = configManager.getMQTTConfig();
  Serial.printf("%s 浣跨敤MQTT閰嶇疆: %s:%d\n", tag, mqttConfig.host.c_str(),
                mqttConfig.port);

  BaseType_t result = xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192, NULL,
                                              1, &mqttTaskHandle, 1);

  if (result != pdPASS || !mqttTaskHandle) {
    Serial.printf("%s ERROR: MQTT浠诲姟鍒涘缓澶辫触\n", tag);
    mqttTaskHandle = nullptr;
    return false;
  }

  Serial.printf("%s MQTT浠诲姟鍒涘缓鎴愬姛\n", tag);
  return true;
}

void networkInitTask(void *pvParameters) {
  if (networkInitGracefullyDegraded) {
    Serial.println("[NETWORK] Degraded mode active, skip initialization");
    vTaskDelete(NULL);
    return;
  }

  esp_task_wdt_add(NULL);

  vTaskDelay(pdMS_TO_TICKS(50));

  TickType_t taskStartTime = xTaskGetTickCount();
  const TickType_t maxTaskRunTime = pdMS_TO_TICKS(300000);

  UBaseType_t taskStackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);

  try {
    esp_task_wdt_reset();

    setup_network();
    esp_task_wdt_reset();

    taskStackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    if (taskStackHighWaterMark < 1024) {
      Serial.printf("[缃戠粶浠诲姟] WARNING: 鏍堢┖闂翠笉瓒? %u words\n",
                    taskStackHighWaterMark);
    }

    Serial.println("[NETWORK] Initialization complete; config loaded");
    esp_task_wdt_reset();

    // 閰嶇疆鍔犺浇缁撴灉闈欓粯澶勭悊
    esp_task_wdt_reset();

    if (configManager.isMQTTConfigReady()) {
      const auto &mqttConfig = configManager.getMQTTConfig();

      BaseType_t result = xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192,
                                                  NULL, 1, &mqttTaskHandle, 1);

      if (result != pdPASS || !mqttTaskHandle) {
        Serial.println("[NETWORK] ERROR: failed to create MQTT task");
      }
    } else {
      Serial.println("[缃戠粶浠诲姟] MQTT閰嶇疆鏈氨缁紝璺宠繃MQTT浠诲姟鍒涘缓");
    }

    // 鍚姩WiFi浼犺緭锛堟鏃堕厤缃凡鍔犺浇锛屽彲浣跨敤鑸圭粍缂栧彿锛?
    if (!training.isActive() && !wifiTransfer.isActive()) {
      if (wifiTransfer.start()) {
        Serial.println("[WiFi] Auto-started transfer mode (crew-id profile)");
      } else {
        Serial.println("[WiFi浼犺緭] 鉂?鍚姩澶辫触");
      }
    }

    networkTaskCompleted = true;
    esp_task_wdt_reset();

  } catch (...) {
    Serial.println("[NETWORK] EXCEPTION: network task crashed");
    esp_task_wdt_reset();
    networkTaskCompleted = true;

    networkRetryCount++;
    if (networkRetryCount >= MAX_NETWORK_RETRIES) {
      networkInitGracefullyDegraded = true;
      networkRetryStartTime = millis();
      Serial.printf("[缃戠粶浠诲姟] 馃敾 缃戠粶鍒濆鍖栧け璐ワ紝浼橀泤闄嶇骇%d鍒嗛挓\n",
                    NETWORK_RETRY_RESET_INTERVAL / 60000);
    }
  }

  if (!configManager.isConfigReady()) {
    Serial.println("[NETWORK] API config not ready");
    networkRetryCount++;

    if (networkRetryCount >= MAX_NETWORK_RETRIES) {
      networkInitGracefullyDegraded = true;
      networkRetryStartTime = millis();
      Serial.printf("[缃戠粶浠诲姟] 馃敾 缃戠粶閰嶇疆澶辫触锛屼紭闆呴檷绾?d鍒嗛挓\n",
                    NETWORK_RETRY_RESET_INTERVAL / 60000);
      Serial.println("[NETWORK] Entering offline mode; training data stays local");
    } else {
      Serial.printf("[缃戠粶浠诲姟] 灏嗗湪%d鍒嗛挓鍚庨噸璇曠綉缁滃垵濮嬪寲\n",
                    (NETWORK_RETRY_RESET_INTERVAL / 60000) / 2);
    }
  } else {
    networkRetryCount = 0;
  }

  taskStackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);

  esp_task_wdt_reset();

  esp_task_wdt_delete(NULL);

  vTaskDelete(NULL);
}

void syncRTCWithCellular() {
  if (!rtcInitialized) {
    Serial.println("[RTC] RTC not initialized; skip sync");
    return;
  }

  String cellularTime = configManager.getCurrentFormattedDateTime();
  if (cellularTime.isEmpty() || cellularTime == "null" ||
      cellularTime.length() < 19) {
    Serial.println("[RTC] 4G time invalid; skip sync");
    return;
  }

  int year = cellularTime.substring(0, 4).toInt();
  int month = cellularTime.substring(5, 7).toInt();
  int day = cellularTime.substring(8, 10).toInt();
  int hour = cellularTime.substring(11, 13).toInt();
  int minute = cellularTime.substring(14, 16).toInt();
  int second = cellularTime.substring(17, 19).toInt();

  if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
      second > 59) {
    Serial.println("[RTC] Invalid date-time format; skip sync");
    return;
  }

  rtc.setDateTime(year, month, day, hour, minute, second);

  delay(100);
  RTC_DateTime verifyDateTime = rtc.getDateTime();

  if (verifyDateTime.getYear() < 2024) {
    Serial.println("[RTC] RTC verify failed after sync");
    return;
  }

  Serial.printf("[RTC] 鍚屾瀹屾垚: %04d-%02d-%02d %02d:%02d:%02d\n",
                verifyDateTime.getYear(), verifyDateTime.getMonth(),
                verifyDateTime.getDay(), verifyDateTime.getHour(),
                verifyDateTime.getMinute(), verifyDateTime.getSecond());

  rtcTimeSynced = true;

  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", verifyDateTime.getHour(),
           verifyDateTime.getMinute());
  lv_label_set_text(ui_Label10, timeStr);
}

String getRTCTimeHHMM() {
  if (!rtcInitialized || !rtcTimeSynced) {
    return "--:--";
  }

  RTC_DateTime datetime = rtc.getDateTime();
  char timeStr[6];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", datetime.getHour(),
           datetime.getMinute());
  return String(timeStr);
}

String getRTCFullDateTime() {
  if (!rtcInitialized || !rtcTimeSynced) {
    return "";
  }

  RTC_DateTime datetime = rtc.getDateTime();
  char dateTimeStr[25];
  snprintf(dateTimeStr, sizeof(dateTimeStr),
           "%04d-%02d-%02d %02d:%02d:%02d.%03lu", datetime.getYear(),
           datetime.getMonth(), datetime.getDay(), datetime.getHour(),
           datetime.getMinute(), datetime.getSecond(), millis() % 1000);
  return String(dateTimeStr);
}

// Add the missing keyDetectionTask function
void keyDetectionTask(void *pvParameters) {
  bool lastStates[4] = {HIGH, HIGH, HIGH, HIGH};
  uint8_t pins[4] = {K1_PIN, K2_PIN, K3_PIN, K4_PIN};
  uint32_t lastChange[4] = {0, 0, 0, 0};

  while (true) {
    uint32_t now = millis();

    for (int i = 0; i < 4; i++) {
      bool current = digitalRead(pins[i]);

      if (current != lastStates[i]) {
        if (now - lastChange[i] > DEBOUNCE_DELAY) {
          lastStates[i] = current;
          lastChange[i] = now;

          KeyEvent event = {pins[i], current == LOW, now};
          xQueueSend(keyQueue, &event, 0);
        }
      }
    }

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}




