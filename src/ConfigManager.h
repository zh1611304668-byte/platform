#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <ArduinoJson.h>
#include <vector>

enum ConfigStatus {
  CONFIG_IDLE,
  CONFIG_LOADING,
  CONFIG_SUCCESS,
  CONFIG_FAILED
};

struct DeviceConfig {
  String tenantCode;
  String boatCode;
  String boatName;
  String stdId;
  bool isValid = false;
};

struct MQTTConfig {
  String host;
  int port = 1883;
  String username;
  String password;
  bool cleanSession = true;
  int keepAliveInterval = 10;
  bool isValid = false;
};

struct RTKConfig {
  String host;
  int port = 1883;
  String username;
  String password;
  bool isValid = false;
};

struct RowerInfo {
  String rowerId;
  String rowerName;
  String deviceCode;
  String btAddr;
};

class ConfigManager {
private:
  String platformHost;
  uint16_t platformPort;
  static const String PLATFORM_API_PATH;

  DeviceConfig deviceConfig;
  MQTTConfig mqttConfig;
  RTKConfig rtkConfig;
  std::vector<RowerInfo> rowerList;

  ConfigStatus deviceConfigStatus = CONFIG_IDLE;
  ConfigStatus mqttConfigStatus = CONFIG_IDLE;
  ConfigStatus rtkConfigStatus = CONFIG_IDLE;
  ConfigStatus rowerListStatus = CONFIG_IDLE;

  String globalDate;
  String globalTime;
  String globalDateTime;
  bool nitzTimeValid = false;
  unsigned long lastNITZUpdate = 0;

  String deviceIMEI;

  String baselineDateTime = "";
  unsigned long baselineMillis = 0;

  bool httpConnectionOpen = false;
  bool serverConfigured = false;

  static TaskHandle_t configTaskHandle;
  static bool taskRunning;

  static void configTask(void *pvParameters);
  bool makeHttpRequest(const String &endpoint, String &response);
  bool make4GHttpRequest(const String &endpoint, String &response);
  bool make4GHttpRequestHTTP(const String &endpoint, String &response);
  bool make4GHttpRequestWithPort(const String &endpoint, String &response,
                                 int port);
  bool send4GATCommand(String command, String expectedResponse,
                       unsigned long timeout = 5000);
  void stripHttpUrcs(String &response);
  void closeHttpConnection();

  bool parseDeviceConfigResponse(const String &json);
  bool parseMQTTConfigResponse(const String &json);
  bool parseRowerListResponse(const String &json);
  void printHeartRateDevices();

  bool loadAllConfigsInOneConnection();
  bool makeHttpRequestKeepAlive(const String &endpoint, String &response);

public:
  ConfigManager();
  ~ConfigManager();

  void startConfigLoading();
  void stopConfigLoading();

  bool isDeviceConfigReady() const { return deviceConfig.isValid; }
  bool isMQTTConfigReady() const { return mqttConfig.isValid; }
  bool isRTKConfigReady() const { return rtkConfig.isValid; }
  bool isRowerListReady() const { return !rowerList.empty(); }
  bool isConfigReady() const {
    return deviceConfig.isValid && mqttConfig.isValid;
  }

  ConfigStatus getDeviceConfigStatus() const { return deviceConfigStatus; }
  ConfigStatus getMQTTConfigStatus() const { return mqttConfigStatus; }
  ConfigStatus getRTKConfigStatus() const { return rtkConfigStatus; }
  ConfigStatus getRowerListStatus() const { return rowerListStatus; }

  const DeviceConfig &getDeviceConfig() const { return deviceConfig; }
  const MQTTConfig &getMQTTConfig() const { return mqttConfig; }
  const RTKConfig &getRTKConfig() const { return rtkConfig; }
  const std::vector<RowerInfo> &getRowerList() const { return rowerList; }

  void updateScreen4Display();
  void updateDeviceConfigUI();

  void refreshConfig();

  bool isAddressInWhitelist(const String &address) const;
  bool getDeviceInfoByAddress(const String &address, String &deviceCode,
                              String &rowerName) const;

  String getFormattedDateTime() const;
  String getCurrentFormattedDateTime();
  String getTimeHHMM() const;
  bool isTimeValid() const;
  void processNITZTime(const String &nitzData);
  void enableNITZTime();
  void setDateTime(const String &date, const String &time,
                   const String &dateTime);
  void updateTimeBaseline(const String &dateTime, unsigned long currentMillis);
  bool setTimeFromNITZ(const String &nitzTimeStr);

  void setDeviceIMEI(const String &imei);
  String getDeviceIMEI() const;

  bool setPlatformAddress(const String &address);
  String getPlatformAddress() const;
  String getPlatformHost() const { return platformHost; }
  uint16_t getPlatformPort() const { return platformPort; }
  String buildFullUrl(const String &endpoint) const;
  void loadPlatformAddressFromStorage();
  void savePlatformAddressToStorage();
  void saveConfigToNVS();   // 保存 API 配置到 NVS
  void loadConfigFromNVS(); // 从 NVS 加载缓存的 API 配置

  enum ReloadPhase {
    RELOAD_IDLE,
    RELOAD_STOPPING_TASKS,
    RELOAD_WAITING_RELEASE,
    RELOAD_UPDATING_CONFIG,
    RELOAD_RESTARTING
  };
  bool safeReloadPlatformAddress(const String &newAddress);
  ReloadPhase getReloadPhase() const { return currentReloadPhase; }
  bool isReloadInProgress() const { return currentReloadPhase != RELOAD_IDLE; }
  void requestReloadCancel();

private:
  ReloadPhase currentReloadPhase = RELOAD_IDLE;
  String pendingPlatformAddress = "";
  volatile bool reloadCancelRequested = false;

public:
  void safeUpdateSensorData(float newStrokeRate, float newSpeedMps,
                            int newStrokeCount, float newTotalDistance);
  void safeGetSensorData(float &outStrokeRate, float &outSpeedMps,
                         int &outStrokeCount, float &outTotalDistance);
  void safeUpdateTimeData(const String &newLocalTime);
  void safeGetTimeData(String &outLocalTime);
};

extern ConfigManager configManager;

#endif
