#ifndef WIFI_TRANSFER_MANAGER_H
#define WIFI_TRANSFER_MANAGER_H

#include "SD_MMC.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>

class WifiTransferManager {
public:
  WifiTransferManager();
  ~WifiTransferManager();

  bool start();
  void stop();
  void update();
  bool isActive() const { return active; }

  void setSSID(const String &ssid) { this->ssid = ssid; }
  void setPassword(const String &password) { this->password = password; }
  String getSSID() const { return ssid; }
  String getIPAddress() const;

private:
  bool active;
  String ssid;
  String password;

  WebServer *server;

  bool setupWiFiAP();
  void teardownWiFi();

  void setupWebServer();
  void handleRoot();
  void handleOtaPage();
  void handleOtaUpload();
  void handleOtaUploadPost();
  void handleFileList();
  void handleFilePreview();
  void handleFileDownload();
  void handleFsStatus();
  void handleFileItemPut();
  void handleFileItemDelete();
  void handleFileUpload();
  void handleFileUploadPost();

  String listDirectory(const String &path, int page = 1, int folderLimit = 30);
  bool isValidPath(const String &path);
  String getContentType(const String &filename);
  String escapeJson(const String &value);
  bool parseRangeHeader(const String &rangeHeader, size_t fileSize, size_t &start,
                        size_t &end);
  bool createDirectories(const String &dirPath);
  bool deleteRecursively(const String &path);

  File uploadFile;
  String uploadError;
  String uploadSavedPath;
  size_t uploadWrittenBytes;

  bool otaInProgress;
  bool otaResultOk;
  String otaResultMsg;
  size_t otaWrittenBytes;
  size_t otaExpectedBytes;
  bool otaNetworkSuspended;
};

#endif