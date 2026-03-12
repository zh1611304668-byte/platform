#include "BluetoothManager.h"
#include "BrightnessManager.h"
#include "CellularManager.h"
#include "ConfigManager.h"
#include "DataFlowManager.h"
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
unsigned long lastSystemInteractionTime = 0;
volatile bool g_touchWakePending = false;

bool consumeTouchWakePending() {
  if (!g_touchWakePending) {
    return false;
  }
  g_touchWakePending = false;
  return true;
}

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
struct LoopObservationStats {
  unsigned long windowStartMs = 0;
  unsigned long loopCount = 0;
  unsigned long long totalLoopUs = 0;
  unsigned long maxLoopUs = 0;
  unsigned long trackedLogEvents = 0;
  unsigned long uiPollHits = 0;
  unsigned long batteryPollHits = 0;
  unsigned long networkPollHits = 0;
  unsigned long bluetoothPollHits = 0;
  unsigned long bluetoothPollSkipHits = 0;
  unsigned long slowLoopHits = 0;
  unsigned long slowLoopScreen1Hits = 0;
  unsigned long slowLoopScreen2Hits = 0;
  unsigned long slowLoopScreen3Hits = 0;
  unsigned long slowLoopScreen4Hits = 0;

  unsigned long screen1SensorUiHits = 0;
  unsigned long long screen1SensorUiTotalUs = 0;
  unsigned long screen1SensorUiMaxUs = 0;
  unsigned long screen1SensorUiSlowHits = 0;

  unsigned long screen1TrainBindHits = 0;
  unsigned long long screen1TrainBindTotalUs = 0;
  unsigned long screen1TrainBindMaxUs = 0;
  unsigned long screen1TrainBindSlowHits = 0;

  unsigned long lvglTimerHits = 0;
  unsigned long long lvglTimerTotalUs = 0;
  unsigned long lvglTimerMaxUs = 0;
  unsigned long lvglTimerSlowHits = 0;

  unsigned long batteryUiHits = 0;
  unsigned long long batteryUiTotalUs = 0;
  unsigned long batteryUiMaxUs = 0;
  unsigned long batteryUiSlowHits = 0;
};

LoopObservationStats loopObs;

void resetLoopObservationWindow(unsigned long now) {
  loopObs.windowStartMs = now;
  loopObs.loopCount = 0;
  loopObs.totalLoopUs = 0;
  loopObs.maxLoopUs = 0;
  loopObs.trackedLogEvents = 0;
  loopObs.uiPollHits = 0;
  loopObs.batteryPollHits = 0;
  loopObs.networkPollHits = 0;
  loopObs.bluetoothPollHits = 0;
  loopObs.bluetoothPollSkipHits = 0;
  loopObs.slowLoopHits = 0;
  loopObs.slowLoopScreen1Hits = 0;
  loopObs.slowLoopScreen2Hits = 0;
  loopObs.slowLoopScreen3Hits = 0;
  loopObs.slowLoopScreen4Hits = 0;

  loopObs.screen1SensorUiHits = 0;
  loopObs.screen1SensorUiTotalUs = 0;
  loopObs.screen1SensorUiMaxUs = 0;
  loopObs.screen1SensorUiSlowHits = 0;

  loopObs.screen1TrainBindHits = 0;
  loopObs.screen1TrainBindTotalUs = 0;
  loopObs.screen1TrainBindMaxUs = 0;
  loopObs.screen1TrainBindSlowHits = 0;

  loopObs.lvglTimerHits = 0;
  loopObs.lvglTimerTotalUs = 0;
  loopObs.lvglTimerMaxUs = 0;
  loopObs.lvglTimerSlowHits = 0;

  loopObs.batteryUiHits = 0;
  loopObs.batteryUiTotalUs = 0;
  loopObs.batteryUiMaxUs = 0;
  loopObs.batteryUiSlowHits = 0;
}

inline void noteTrackedLoopLog() { loopObs.trackedLogEvents++; }

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
constexpr uint8_t DEBOUNCE_DELAY = 50;
constexpr uint8_t KEY_QUEUE_SIZE = 16;
constexpr unsigned long GNSS_LABEL_UPDATE_INTERVAL_MS = 5000;

struct KeyEvent {
  uint8_t pin;
  bool action;
};

QueueHandle_t keyQueue = nullptr;
TaskHandle_t keyTaskHandle = nullptr;
TaskHandle_t mqttTaskHandle = nullptr;

// IMU数据记录已改为直接调用（带内部缓冲），无需队列和异步任务

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
const unsigned long trainingLongPress = 1000;
bool k4PressedInScreen1 = false;
bool k4IsPressed = false;
unsigned long k4LastClickTime = 0;
unsigned long k4TrainingToggleTime = 0;  // 训练切换冷却时间戳

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

  // 在系统初始化完成后再读取NVS中的平台地址
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
  // 使用BrightnessManager初始化PWM控制，替代简单的开关控制
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

  Serial.println("[MAIN] 初始化PowerManager...");
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
      Serial.println("[RTC] 时间无效，等待网络同步...");
    }
  } else {
    Serial.println("[RTC] PCF85063初始化失败！检查接线");
  }

  gnss.begin();
  gnss.setSDCardManager(&sdCardManager); // 设置SD卡管理器用于记录NMEA数据
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
    Serial.println("[MAIN] ERROR: 网络任务创建失败！");
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

  // WiFi传输将在网络任务完成后启动（以使用船组编号）

  updatePanel1();
  updatePanel8();

  lv_timer_handler();
  delay(100);

  pinMode(K1_PIN, INPUT_PULLUP);
  pinMode(K2_PIN, INPUT_PULLUP);
  pinMode(K3_PIN, INPUT_PULLUP);
  pinMode(K4_PIN, INPUT_PULLUP);

  BT::begin();

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

  // IMU日志已改为直接写入（内部缓冲），不再需要队列和异步任务

  optimizeTaskPriorities();

  // WiFi将在网络任务完成后自动启动

  boot_auto_once_done = false;
  boot_auto_once_stage = 0;
  boot_auto_once_ts = millis();
}

void optimizeTaskPriorities() { vTaskPrioritySet(NULL, 2); }

void loop() {
  static bool loopWdtRegistered = false;
  if (!loopWdtRegistered) {
    esp_task_wdt_add(NULL);
    Serial.println("[MAIN] Loop registered to Task Watchdog");
    loopWdtRegistered = true;
  }

  if (lastSystemInteractionTime == 0) {
    lastSystemInteractionTime = millis();
  }
  unsigned long now = millis();
  unsigned long loopStartUs = micros();
  if (loopObs.windowStartMs == 0) {
    resetLoopObservationWindow(now);
  }

  // High frequency sensor update
  imu.update();
  if (wifiTransfer.isActive()) {
    wifiTransfer.update();
  }
  esp_task_wdt_reset();

  // 屏幕亮度管理更新 (每100ms检查一次)
  static unsigned long lastBrightnessUpdate = 0;
  if (now - lastBrightnessUpdate > 100) {
    brightness.update(now, training.isActive(), lastSystemInteractionTime);
    lastBrightnessUpdate = now;
  }

  static unsigned long lastLogTime = 0;
  static unsigned long lastSdStatusPrint = 0;

  if (now - lastLogTime > 16) { // 16ms interval = 62.5Hz，与IMU采样率一致
    lastLogTime = now;

    // 即使RTC未同步，也记录IMU数据（使用millis）
    if (training.isActive()) {
      float ax, ay, az;
      imu.getAcceleration(ax, ay, az);

      // 使用now作为时间戳，确保记录的是采样时间而非写入时间
      sdCardManager.logImuData(now, ax, ay, az);
    }

    if (sdCardManager.isDisabled() && (now - lastSdStatusPrint > 30000)) {
      lastSdStatusPrint = now;
      noteTrackedLoopLog();
      Serial.println("[SD] ⚠️  SD card is disabled, data logging suspended");
    }
  }

  static unsigned long lastLoopTime = 0;
  if (now - lastLoopTime > 1000) {
    powerMgr.printMemoryStatus();

    static unsigned long lastDegradeCheck = 0;
    if (now - lastDegradeCheck > 60000) {
      bool anyDegraded = false;

      if (sdCardManager.isDisabled()) {
        Serial.println("[系统]  SD卡已降级禁用，数据记录已暂停");
        anyDegraded = true;
      }

      if (networkInitGracefullyDegraded) {
        unsigned long remaining =
            (NETWORK_RETRY_RESET_INTERVAL - (now - networkRetryStartTime)) /
            1000;
        if (remaining > 0) {
          noteTrackedLoopLog();
          Serial.printf("[系统] 🔻 网络降级中，还剩%lu秒\n", remaining);
          anyDegraded = true;
        } else {
          networkInitGracefullyDegraded = false;
          networkRetryCount = 0;
          noteTrackedLoopLog();
          Serial.println("[系统] 🔄 网络降级期结束，可重新尝试");
        }
      }

      if (!anyDegraded) {
        static unsigned long lastHealthReport = 0;
        if (now - lastHealthReport > 300000) {
          noteTrackedLoopLog();
          Serial.println("[系统] ✅ 系统运行正常，所有模块稳定");
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
    loopObs.networkPollHits++;
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

      Serial.println("\n[CMD] 收到地址更改请求: " + address);

      if (configManager.getReloadPhase() != ConfigManager::RELOAD_IDLE) {
        Serial.println("\n❌ 重载正在进行中，请稍后再试！\n");
        return;
      }

      // 检查当前是否处于"等待配置"状态（即Host为空）
      bool isWaitingForConfig = configManager.getPlatformHost().isEmpty();

      if (isWaitingForConfig) {
        // 初始设置，直接调用 setPlatformAddress
        if (configManager.setPlatformAddress(address)) {
          Serial.println("\n✅ 初始平台地址已设置，系统将自动开始加载配置\n");
        } else {
          Serial.println("\n❌ 地址设置失败，请检查格式 (IP:PORT)\n");
        }
        return;
      }

      // 运行时重载
      Serial.println("[CMD] 正在停止旧任务并重载地址...");

      if (configManager.safeReloadPlatformAddress(address)) {
        Serial.println("\n✅ 平台地址安全重载成功!");
        Serial.println("系统已使用新地址重新获取配置\n");
      } else {
        Serial.println("\n❌ 平台地址重载失败!");
        Serial.println("原因：地址格式错误、资源释放超时或PPP拨号失败");
        Serial.println("请检查后重试\n");
      }
    } else if (cmd == "GETAPI") {
      Serial.println("当前平台地址: " + configManager.getPlatformAddress());
    } else if (cmd == "HELP" || cmd == "help") {
      Serial.println("\n===== 串口命令帮助 =====");
      Serial.println(
          "SETAPI=<IP:PORT>  - 设置平台地址 (例: SETAPI=117.83.111.19:10033)");
      Serial.println("                    "
                     "注：会自动停止MQTT/DataFlow，释放资源，重新拨号后恢复");
      Serial.println("GETAPI            - 查询当前平台地址");
      Serial.println("HELP              - 显示帮助信息");
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
      for (int i = 0; i < BT::NUM_PRESETS; ++i) {
        auto &d = BT::devices()[i];
        if (d.connected) {
          BT::setUploadSource(&d);
          boot_auto_once_done = true;
          break;
        }
      }
      if (now - boot_auto_once_ts > 15000) {
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

  if (consumeTouchWakePending()) {
    lastSystemInteractionTime = now;
    brightness.resetInteraction();
  }

  KeyEvent event;
  while (xQueueReceive(keyQueue, &event, 0) == pdTRUE) {
    lastSystemInteractionTime = now; // 按键重置计时器
    brightness.resetInteraction();   // 立即恢复最高亮度
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
        Serial.println("[DEBUG] K4 Pressed");
        k4PressTime = now;
        k4PressedInScreen1 = (safeGetCurrentScreen() == SCREEN1);
        k4IsPressed = true;
      } else {
        Serial.println("[DEBUG] K4 Released");
        k4IsPressed = false;
        unsigned long pressDuration = now - k4PressTime;

        if (k4PressTime == 0) {
          pressDuration = 0;
        } else if (pressDuration > 60000) {
          pressDuration = 0;
        }

        // 释放时判定双击（仅在SCREEN1，短按30~400ms，窗口600ms，冷却1200ms）
        if (k4PressedInScreen1 && pressDuration >= 30 && pressDuration <= 400) {
          if (k4TrainingToggleTime == 0 || (now - k4TrainingToggleTime > 1200)) {
            if (k4LastClickTime > 0 && (now - k4LastClickTime) < 600) {
              Serial.printf("[DEBUG] Double-click interval: %lu ms\n", now - k4LastClickTime);
              if (!training.isActive()) {
                training.start();
                Serial.println("========================================");
                Serial.println("✓ [训练模式] 双击触发 - 开始训练");
                Serial.println("========================================");
              } else {
                training.stop();
                Serial.println("========================================");
                Serial.println("✓ [训练模式] 双击触发 - 停止训练");
                Serial.println("========================================");
              }
              trainingActive = training.isActive();
              k4ToggledThisPress = true;
              k4LastClickTime = 0;
              k4TrainingToggleTime = now;
            } else {
              k4LastClickTime = now;
            }
          }
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
        }

        k4ToggledThisPress = false;
        k4PressTime = 0;
        k4PressedInScreen1 = false;
      }
      break;
    }

    lv_timer_handler();
  }

  // 长按触发训练模式逻辑已替换为双击触发

  static ScreenId lastScreen = SCREEN1;
  ScreenId currentScreen = safeGetCurrentScreen();

  if (currentScreen != SCREEN1 && lastScreen == SCREEN1) {
    k4PressedInScreen1 = false;
    k4ToggledThisPress = false;
    k4IsPressed = false;
    k4LastClickTime = 0;  // 离开SCREEN1清空首击，避免跨页残留
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

  // 检查是否有新划桨数据（事件驱动 - 立即更新，不受 100/200ms 限制）
  if (imu.hasNewStroke()) {
    const StrokeMetrics &metrics = imu.getLastStrokeMetrics();

    // 更新桨数和桨频
    strokeRate = imu.getStrokeRate();
    strokeCount = metrics.strokeNumber;
    int currentStrokeCount = strokeCount;
    float currentSpeedMps = gnss.getSpeed();

    // 触发训练逻辑和数据捕获（会计算距离并更新全局变量）
    training.onStrokeDetected();
    strokeDataMgr.captureStroke(currentStrokeCount);

    // 现在strokeLength和totalDistance已被StrokeDataManager更新，可以用于UI显示

    // 更新 ConfigManager
    configManager.safeUpdateSensorData(strokeRate, currentSpeedMps,
                                       currentStrokeCount, totalDistance);

    // 更新 UI 显示
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

    // 立即刷新UI显示，减少延迟（事件驱动刷新）
    lv_timer_handler();

    // 清除标志
    imu.clearNewStrokeFlag();
    lastStrokeCount = currentStrokeCount;

    // 立即同步数据绑定
    updateBoundData();
  }

  ScreenId sensorScreen = safeGetCurrentScreen();
  uint32_t sensorUpdateInterval = (sensorScreen == SCREEN3) ? 100 : 200;
  if (now - lastSensorUIUpdate > sensorUpdateInterval) {
    loopObs.uiPollHits++;
    unsigned long sensorUiStartUs = micros();
    char tmp[16];

    float displaySpeedMps = 0.0f;
    String displayLocalTime = "--:--";
    float tempStrokeRate, tempTotalDistance;
    int tempStrokeCount;
    configManager.safeGetSensorData(tempStrokeRate, displaySpeedMps,
                                    tempStrokeCount, tempTotalDistance);
    configManager.safeGetTimeData(displayLocalTime);

    // 速度/功率/配速每秒刷新一次
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

    // 定期更新模式：仅更新衰减的桨频（如果有新划桨刚才已经更新过了）
    float currentRate = imu.getStrokeRate();
    snprintf(tmp, sizeof(tmp), "%.1f", currentRate);
    lv_label_set_text(ui_Label9, tmp);

    unsigned long trainBindStartUs = micros();
    training.update();
    updateBoundData();
    unsigned long trainBindElapsedUs = micros() - trainBindStartUs;
    if (sensorScreen == SCREEN1) {
      loopObs.screen1TrainBindHits++;
      loopObs.screen1TrainBindTotalUs += trainBindElapsedUs;
      if (trainBindElapsedUs > loopObs.screen1TrainBindMaxUs) {
        loopObs.screen1TrainBindMaxUs = trainBindElapsedUs;
      }
      if (trainBindElapsedUs >= 10000) {
        loopObs.screen1TrainBindSlowHits++;
      }
    }

    lastSensorUIUpdate = now;

    unsigned long sensorUiElapsedUs = micros() - sensorUiStartUs;
    if (sensorScreen == SCREEN1) {
      loopObs.screen1SensorUiHits++;
      loopObs.screen1SensorUiTotalUs += sensorUiElapsedUs;
      if (sensorUiElapsedUs > loopObs.screen1SensorUiMaxUs) {
        loopObs.screen1SensorUiMaxUs = sensorUiElapsedUs;
      }
      if (sensorUiElapsedUs >= 10000) {
        loopObs.screen1SensorUiSlowHits++;
      }
    }
  }
  static uint32_t lastHRUpdate = 0;
  auto activeDev = BT::activeDevice();

  static uint32_t lastBluetoothUiPollMs = 0;
  if (now - lastBluetoothUiPollMs >= 20) {
    BT::pollUIEvents();
    loopObs.bluetoothPollHits++;
    lastBluetoothUiPollMs = now;
  } else {
    loopObs.bluetoothPollSkipHits++;
  }

  if (activeDev && activeDev->connected && (now - lastHRUpdate) > 2000) {
    static char hrDisplayBuffer[16];
    memset(hrDisplayBuffer, 0, sizeof(hrDisplayBuffer));
    snprintf(hrDisplayBuffer, sizeof(hrDisplayBuffer), "%d",
             activeDev->lastHeartRate);

    try {
      safeLabelUpdate(ui_Label50, hrDisplayBuffer, "Label50(P1HR)");
      safeLabelUpdate(ui_Label4, hrDisplayBuffer, "Label4(P8HR)");
    } catch (...) {
      noteTrackedLoopLog();
      Serial.println("[MAIN] Error updating HR labels");
    }

    lastHRUpdate = now;
  } else if (!activeDev && (now - lastHRUpdate) > 2000) {
    try {
      safeLabelUpdate(ui_Label50, "0", "Label50(P1HR)");
      safeLabelUpdate(ui_Label4, "0", "Label4(P8HR)");
      safeLabelUpdate(ui_Label14, "未选择", "Label14(NoDevice)");
      safeLabelUpdate(ui_Label53, "未选择", "Label53(NoDevice)");
    } catch (...) {
      noteTrackedLoopLog();
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
    loopObs.uiPollHits++;

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
    unsigned long lvglStartUs = micros();
    lv_timer_handler();
    unsigned long lvglElapsedUs = micros() - lvglStartUs;
    loopObs.lvglTimerHits++;
    loopObs.lvglTimerTotalUs += lvglElapsedUs;
    if (lvglElapsedUs > loopObs.lvglTimerMaxUs) {
      loopObs.lvglTimerMaxUs = lvglElapsedUs;
    }
    if (lvglElapsedUs >= 10000) {
      loopObs.lvglTimerSlowHits++;
    }
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
    noteTrackedLoopLog();
    Serial.println("[RTC] 执行定期时间同步...");
    syncRTCWithCellular();
    lastRTCSync = now;
  }

  static unsigned long lastBatteryUIUpdate = 0;
  if (millis() - lastBatteryUIUpdate >= 2000) {
    loopObs.batteryPollHits++;
    unsigned long batteryStartUs = micros();
    powerMgr.update();
    powerMgr.updateBatteryUI();
    unsigned long batteryElapsedUs = micros() - batteryStartUs;
    loopObs.batteryUiHits++;
    loopObs.batteryUiTotalUs += batteryElapsedUs;
    if (batteryElapsedUs > loopObs.batteryUiMaxUs) {
      loopObs.batteryUiMaxUs = batteryElapsedUs;
    }
    if (batteryElapsedUs >= 10000) {
      loopObs.batteryUiSlowHits++;
    }
    lastBatteryUIUpdate = millis();
  }

  static unsigned long lastDetailedBatteryPrint = 0;
  if (millis() - lastDetailedBatteryPrint >= 60000) {
    lastDetailedBatteryPrint = millis();
  }

  static uint32_t lastMinuteBatteryUIUpdate = 0;
  if (now - lastMinuteBatteryUIUpdate > 60000) {
    loopObs.batteryPollHits++;
    if (safeGetCurrentScreen() == SCREEN1) {
      unsigned long batteryStartUs = micros();
      powerMgr.updateBatteryUI();
      unsigned long batteryElapsedUs = micros() - batteryStartUs;
      loopObs.batteryUiHits++;
      loopObs.batteryUiTotalUs += batteryElapsedUs;
      if (batteryElapsedUs > loopObs.batteryUiMaxUs) {
        loopObs.batteryUiMaxUs = batteryElapsedUs;
      }
      if (batteryElapsedUs >= 10000) {
        loopObs.batteryUiSlowHits++;
      }
    }
    lastMinuteBatteryUIUpdate = now;
  }

  // removed rogue xQueueReceive from here

  // ==========================================================================================
  // 自动关机逻辑 (10分钟无操作且不在训练模式)
  // ==========================================================================================

  // 1. 如果在训练模式，不断重置计时器
  if (training.isActive()) {
    lastSystemInteractionTime = now;
  }

  // 2. 如果K4正在被长按 (k4IsPressed)
  if (k4IsPressed) {
    lastSystemInteractionTime = now;
  }

  // 3. 触摸屏活跃时间 (LVGL自动维护)
  uint32_t touchInactiveTime = lv_disp_get_inactive_time(NULL);

  const unsigned long AUTO_SHUTDOWN_TIMEOUT = 600000;

  if ((now - lastSystemInteractionTime > AUTO_SHUTDOWN_TIMEOUT) &&
      (touchInactiveTime > AUTO_SHUTDOWN_TIMEOUT) && !training.isActive()) {

    noteTrackedLoopLog();
    Serial.println("[System] 💤 自动关机触发 (10分钟无操作)");
    powerMgr.shutdown();
  }

  unsigned long loopElapsedUs = micros() - loopStartUs;
  loopObs.loopCount++;
  loopObs.totalLoopUs += loopElapsedUs;
  if (loopElapsedUs > loopObs.maxLoopUs) {
    loopObs.maxLoopUs = loopElapsedUs;
  }

  if (loopElapsedUs >= 20000) {
    loopObs.slowLoopHits++;
    switch (safeGetCurrentScreen()) {
    case SCREEN1:
      loopObs.slowLoopScreen1Hits++;
      break;
    case SCREEN2:
      loopObs.slowLoopScreen2Hits++;
      break;
    case SCREEN3:
      loopObs.slowLoopScreen3Hits++;
      break;
    case SCREEN4:
      loopObs.slowLoopScreen4Hits++;
      break;
    }
  }

  if (now - loopObs.windowStartMs >= 30000) {
    float avgLoopUs = (loopObs.loopCount > 0)
                          ? (float)loopObs.totalLoopUs / (float)loopObs.loopCount
                          : 0.0f;
    float s1SensorAvgUs = (loopObs.screen1SensorUiHits > 0)
                              ? (float)loopObs.screen1SensorUiTotalUs /
                                    (float)loopObs.screen1SensorUiHits
                              : 0.0f;
    float s1TrainBindAvgUs = (loopObs.screen1TrainBindHits > 0)
                                 ? (float)loopObs.screen1TrainBindTotalUs /
                                       (float)loopObs.screen1TrainBindHits
                                 : 0.0f;
    float lvglAvgUs = (loopObs.lvglTimerHits > 0)
                          ? (float)loopObs.lvglTimerTotalUs /
                                (float)loopObs.lvglTimerHits
                          : 0.0f;
    float batteryAvgUs = (loopObs.batteryUiHits > 0)
                             ? (float)loopObs.batteryUiTotalUs /
                                   (float)loopObs.batteryUiHits
                             : 0.0f;
    Serial.printf(
        "[LOOP-STATS] window_ms=%lu loops=%lu avg_us=%.1f max_us=%lu logs=%lu ui_hits=%lu battery_hits=%lu network_hits=%lu bt_hits=%lu bt_skips=%lu slow_hits=%lu slow_s1=%lu slow_s2=%lu slow_s3=%lu slow_s4=%lu s1_ui_avg=%.1f s1_ui_max=%lu s1_ui_slow=%lu s1_tb_avg=%.1f s1_tb_max=%lu s1_tb_slow=%lu lvgl_avg=%.1f lvgl_max=%lu lvgl_slow=%lu bat_avg=%.1f bat_max=%lu bat_slow=%lu\n",
        now - loopObs.windowStartMs, loopObs.loopCount, avgLoopUs,
        loopObs.maxLoopUs, loopObs.trackedLogEvents, loopObs.uiPollHits,
        loopObs.batteryPollHits, loopObs.networkPollHits,
        loopObs.bluetoothPollHits, loopObs.bluetoothPollSkipHits,
        loopObs.slowLoopHits, loopObs.slowLoopScreen1Hits,
        loopObs.slowLoopScreen2Hits, loopObs.slowLoopScreen3Hits,
        loopObs.slowLoopScreen4Hits, s1SensorAvgUs,
        loopObs.screen1SensorUiMaxUs, loopObs.screen1SensorUiSlowHits,
        s1TrainBindAvgUs, loopObs.screen1TrainBindMaxUs,
        loopObs.screen1TrainBindSlowHits, lvglAvgUs,
        loopObs.lvglTimerMaxUs, loopObs.lvglTimerSlowHits, batteryAvgUs,
        loopObs.batteryUiMaxUs, loopObs.batteryUiSlowHits);
    resetLoopObservationWindow(now);
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
    Serial.printf("%s MQTT任务已在运行，跳过创建\n", tag);
    return true;
  }

  if (!configManager.isMQTTConfigReady()) {
    Serial.printf("%s MQTT配置未就绪，无法启动MQTT任务\n", tag);
    return false;
  }

  const auto &mqttConfig = configManager.getMQTTConfig();
  Serial.printf("%s 使用MQTT配置: %s:%d\n", tag, mqttConfig.host.c_str(),
                mqttConfig.port);

  BaseType_t result = xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192, NULL,
                                              1, &mqttTaskHandle, 1);

  if (result != pdPASS || !mqttTaskHandle) {
    Serial.printf("%s ERROR: MQTT任务创建失败\n", tag);
    mqttTaskHandle = nullptr;
    return false;
  }

  Serial.printf("%s MQTT任务创建成功\n", tag);
  return true;
}

void networkInitTask(void *pvParameters) {
  if (networkInitGracefullyDegraded) {
    Serial.println("[网络任务] 网络已优雅降级，跳过初始化");
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
      Serial.printf("[网络任务] WARNING: 栈空间不足: %u words\n",
                    taskStackHighWaterMark);
    }

    Serial.println("[网络任务] 网络初始化完成，配置已获取");
    esp_task_wdt_reset();

    // 配置加载结果静默处理
    esp_task_wdt_reset();

    // 启动WiFi传输（此时配置已加载，可使用船组编号）
    // 先启动WiFi传输，避免其阻塞(delay)影响MQTT连接的串口响应
    if (!training.isActive() && !wifiTransfer.isActive()) {
      if (wifiTransfer.start()) {
        Serial.println("[WiFi传输] ✅ 已自动启动（使用船组编号）");
      } else {
        Serial.println("[WiFi传输] ❌ 启动失败");
      }
    }

    if (configManager.isMQTTConfigReady()) {
      const auto &mqttConfig = configManager.getMQTTConfig();

      BaseType_t result = xTaskCreatePinnedToCore(mqttTask, "MQTTTask", 8192,
                                                  NULL, 1, &mqttTaskHandle, 1);

      if (result != pdPASS || !mqttTaskHandle) {
        Serial.println("[网络任务] ERROR: MQTT任务创建失败！");
      }
    } else {
      Serial.println("[网络任务] MQTT配置未就绪，跳过MQTT任务创建");
    }

    networkTaskCompleted = true;
    esp_task_wdt_reset();

  } catch (...) {
    Serial.println("[网络任务] EXCEPTION: 任务执行异常！");
    esp_task_wdt_reset();
    networkTaskCompleted = true;

    networkRetryCount++;
    if (networkRetryCount >= MAX_NETWORK_RETRIES) {
      networkInitGracefullyDegraded = true;
      networkRetryStartTime = millis();
      Serial.printf("[网络任务] 🔻 网络初始化失败，优雅降级%d分钟\n",
                    NETWORK_RETRY_RESET_INTERVAL / 60000);
      BT::startTask();
    }
  }

  if (!configManager.isConfigReady()) {
    Serial.println("[网络任务] API配置未完成");
    networkRetryCount++;

    if (networkRetryCount >= MAX_NETWORK_RETRIES) {
      networkInitGracefullyDegraded = true;
      networkRetryStartTime = millis();
      Serial.printf("[网络任务] 🔻 网络配置失败，优雅降级%d分钟\n",
                    NETWORK_RETRY_RESET_INTERVAL / 60000);
      Serial.println("[网络任务] 系统将以离线模式运行，训练数据本地保存");
      BT::startTask();
    } else {
      Serial.printf("[网络任务] 将在%d分钟后重试网络初始化\n",
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
    Serial.println("[RTC] RTC未初始化，跳过同步");
    return;
  }

  String cellularTime = configManager.getCurrentFormattedDateTime();
  if (cellularTime.isEmpty() || cellularTime == "null" ||
      cellularTime.length() < 19) {
    Serial.println("[RTC] 4G时间无效，跳过同步");
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
    Serial.println("[RTC] 时间格式无效，跳过同步");
    return;
  }

  rtc.setDateTime(year, month, day, hour, minute, second);

  delay(100);
  RTC_DateTime verifyDateTime = rtc.getDateTime();

  if (verifyDateTime.getYear() < 2024) {
    Serial.println("[RTC] ⚠️  RTC 时间设置失败，年份异常");
    return;
  }

  Serial.printf("[RTC] 同步完成: %04d-%02d-%02d %02d:%02d:%02d\n",
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

          KeyEvent event = {pins[i], current == LOW};
          xQueueSend(keyQueue, &event, 0);
        }
      }
    }

    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

