#ifndef WIFI_TRANSFER_MANAGER_H
#define WIFI_TRANSFER_MANAGER_H

#include "SD_MMC.h"
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>


class WifiTransferManager {
public:
  WifiTransferManager();
  ~WifiTransferManager();

  // 生命周期管理
  bool start(); // 启动WiFi传输模式
  void stop();  // 停止WiFi传输模式
  bool isActive() const { return active; }

  // WiFi配置
  void setSSID(const String &ssid) { this->ssid = ssid; }
  void setPassword(const String &password) { this->password = password; }
  String getSSID() const { return ssid; }
  String getIPAddress() const;

private:
  bool active;
  String ssid;
  String password;

  AsyncWebServer *server;

  // WiFi设置
  bool setupWiFiAP();
  void teardownWiFi();

  // Web服务器路由
  void setupWebServer();
  void handleRoot(AsyncWebServerRequest *request);
  void handleFileList(AsyncWebServerRequest *request);
  void handleFileDownload(AsyncWebServerRequest *request);

  // 辅助函数
  String listDirectory(const String &path);
  bool isValidPath(const String &path);
  String getContentType(const String &filename);
};

#endif
