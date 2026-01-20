#include "WifiTransferManager.h"
#include <ArduinoJson.h>

// WiFi配置常量
static const char *DEFAULT_SSID = "Rowing_Data_Transfer";
static const char *DEFAULT_PASSWORD = "12345789";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

WifiTransferManager::WifiTransferManager()
    : active(false), ssid(DEFAULT_SSID), password(DEFAULT_PASSWORD),
      server(nullptr) {}

WifiTransferManager::~WifiTransferManager() { stop(); }

bool WifiTransferManager::start() {
  if (active) {
    Serial.println("[WiFi传输] 已经处于激活状态");
    return true;
  }

  Serial.println("[WiFi传输] 启动WiFi传输模式...");

  // 设置WiFi AP模式
  if (!setupWiFiAP()) {
    Serial.println("[WiFi传输] WiFi AP设置失败");
    return false;
  }

  // 创建并设置Web服务器
  server = new AsyncWebServer(80);
  setupWebServer();
  server->begin();

  active = true;
  Serial.println("[WiFi传输] ✅ WiFi传输模式已启动");
  Serial.printf("[WiFi传输] SSID: %s\n", ssid.c_str());
  Serial.printf("[WiFi传输] IP地址: %s\n", WiFi.softAPIP().toString().c_str());
  Serial.printf("[WiFi传输] 访问地址: http://%s\n",
                WiFi.softAPIP().toString().c_str());

  return true;
}

void WifiTransferManager::stop() {
  if (!active) {
    return;
  }

  Serial.println("[WiFi传输] 停止WiFi传输模式...");

  // 停止Web服务器
  if (server) {
    server->end();
    delete server;
    server = nullptr;
  }

  // 关闭WiFi
  teardownWiFi();

  active = false;
  Serial.println("[WiFi传输] WiFi传输模式已停止");
}

String WifiTransferManager::getIPAddress() const {
  if (active) {
    return WiFi.softAPIP().toString();
  }
  return "";
}

bool WifiTransferManager::setupWiFiAP() {
  // 断开所有现有连接
  WiFi.disconnect(true);
  delay(100);

  // 配置AP模式
  if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
    Serial.println("[WiFi传输] AP配置失败");
    return false;
  }

  // 启动AP（无密码 - 开放网络）
  if (!WiFi.softAP(ssid.c_str(), NULL, 1, 0, 1)) {
    Serial.println("[WiFi传输] AP启动失败");
    return false;
  }

  delay(100);
  Serial.printf("[WiFi传输] AP已启动 - SSID: %s\n", ssid.c_str());
  Serial.printf("[WiFi传输] IP: %s\n", WiFi.softAPIP().toString().c_str());

  return true;
}

void WifiTransferManager::teardownWiFi() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void WifiTransferManager::setupWebServer() {
  // 主页 - 文件浏览器UI
  server->on("/", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleRoot(request);
  });

  // API: 获取文件列表
  server->on("/api/list", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleFileList(request);
  });

  // 文件下载
  server->on("/download", HTTP_GET, [this](AsyncWebServerRequest *request) {
    this->handleFileDownload(request);
  });

  // 404处理
  server->onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not Found");
  });
}

void WifiTransferManager::handleRoot(AsyncWebServerRequest *request) {
  // 嵌入式HTML UI
  String html = R"html(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>训练数据传输</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      min-height: 100vh;
      padding: 20px;
    }
    .container {
      max-width: 900px;
      margin: 0 auto;
      background: rgba(255, 255, 255, 0.95);
      border-radius: 20px;
      padding: 30px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    }
    h1 {
      color: #667eea;
      margin-bottom: 10px;
      font-size: 2em;
    }
    .subtitle {
      color: #666;
      margin-bottom: 30px;
      font-size: 0.9em;
    }
    .file-list {
      background: white;
      border-radius: 10px;
      padding: 20px;
      box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    }
    .file-item {
      display: flex;
      justify-content: space-between;
      align-items: center;
      padding: 15px;
      border-bottom: 1px solid #eee;
      transition: background 0.2s;
    }
    .file-item:hover {
      background: #f8f9ff;
    }
    .file-item:last-child {
      border-bottom: none;
    }
    .file-info {
      flex: 1;
    }
    .file-name {
      font-weight: 600;
      color: #333;
      margin-bottom: 5px;
    }
    .file-meta {
      font-size: 0.85em;
      color: #999;
    }
    .file-icon {
      width: 40px;
      height: 40px;
      background: linear-gradient(135deg, #667eea, #764ba2);
      border-radius: 8px;
      display: flex;
      align-items: center;
      justify-content: center;
      color: white;
      font-weight: bold;
      margin-right: 15px;
    }
    .folder-icon {
      background: linear-gradient(135deg, #f093fb, #f5576c);
    }
    .download-btn {
      background: linear-gradient(135deg, #667eea, #764ba2);
      color: white;
      border: none;
      padding: 10px 20px;
      border-radius: 8px;
      cursor: pointer;
      font-weight: 600;
      transition: transform 0.2s, box-shadow 0.2s;
    }
    .download-btn:hover {
      transform: translateY(-2px);
      box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
    }
    .loading {
      text-align: center;
      padding: 40px;
      color: #999;
    }
    .breadcrumb {
      display: flex;
      gap: 5px;
      margin-bottom: 20px;
      flex-wrap: wrap;
    }
    .breadcrumb-item {
      color: #667eea;
      cursor: pointer;
      padding: 5px 10px;
      border-radius: 5px;
      transition: background 0.2s;
    }
    .breadcrumb-item:hover {
      background: #f0f0f0;
    }
    .breadcrumb-separator {
      color: #ccc;
      padding: 5px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>🚣 训练数据传输</h1>
    <div class="subtitle">浏览和下载SD卡中的训练数据</div>
    <div class="breadcrumb" id="breadcrumb"></div>
    <div class="file-list" id="fileList">
      <div class="loading">正在加载...</div>
    </div>
  </div>
  <script>
    let currentPath = '/';
    
    function loadFiles(path) {
      currentPath = path;
      updateBreadcrumb();
      
      fetch('/api/list?path=' + encodeURIComponent(path))
        .then(r => r.json())
        .then(data => {
          const list = document.getElementById('fileList');
          if (data.files.length === 0) {
            list.innerHTML = '<div class="loading">此目录为空</div>';
            return;
          }
          
          list.innerHTML = data.files.map(f => `
            <div class="file-item">
              <div class="file-icon ${f.isDir ? 'folder-icon' : ''}">${f.isDir ? '📁' : '📄'}</div>
              <div class="file-info">
                <div class="file-name">${f.name}</div>
                <div class="file-meta">${f.isDir ? '文件夹' : formatSize(f.size)}</div>
              </div>
              ${f.isDir ? 
                `<button class="download-btn" onclick="loadFiles('${f.path}')">打开</button>` :
                `<button class="download-btn" onclick="downloadFile('${f.path}')">下载</button>`
              }
            </div>
          `).join('');
        })
        .catch(err => {
          document.getElementById('fileList').innerHTML = '<div class="loading">加载失败</div>';
        });
    }
    
    function updateBreadcrumb() {
      const parts = currentPath.split('/').filter(p => p);
      const bc = document.getElementById('breadcrumb');
      
      let html = '<span class="breadcrumb-item" onclick="loadFiles(\'/\')">根目录</span>';
      let accPath = '';
      
      parts.forEach((part, i) => {
        accPath += '/' + part;
        const pathCopy = accPath;
        html += '<span class="breadcrumb-separator">/</span>';
        html += `<span class="breadcrumb-item" onclick="loadFiles('${pathCopy}')">${part}</span>`;
      });
      
      bc.innerHTML = html;
    }
    
    function downloadFile(path) {
      window.location.href = '/download?file=' + encodeURIComponent(path);
    }
    
    function formatSize(bytes) {
      if (bytes < 1024) return bytes + ' B';
      if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + ' KB';
      if (bytes < 1024 * 1024 * 1024) return (bytes / 1024 / 1024).toFixed(1) + ' MB';
      return (bytes / 1024 / 1024 / 1024).toFixed(1) + ' GB';
    }
    
    loadFiles('/');
  </script>
</body>
</html>
  )html";

  request->send(200, "text/html", html);
}

void WifiTransferManager::handleFileList(AsyncWebServerRequest *request) {
  String path = "/";
  if (request->hasParam("path")) {
    path = request->getParam("path")->value();
  }

  // 验证路径安全性
  if (!isValidPath(path)) {
    request->send(400, "application/json", "{\"error\":\"Invalid path\"}");
    return;
  }

  String jsonList = listDirectory(path);
  request->send(200, "application/json", jsonList);
}

void WifiTransferManager::handleFileDownload(AsyncWebServerRequest *request) {
  if (!request->hasParam("file")) {
    request->send(400, "text/plain", "Missing file parameter");
    return;
  }

  String filePath = request->getParam("file")->value();

  // 验证路径安全性
  if (!isValidPath(filePath)) {
    request->send(400, "text/plain", "Invalid file path");
    return;
  }

  // 检查文件是否存在
  if (!SD_MMC.exists(filePath.c_str())) {
    request->send(404, "text/plain", "File not found");
    return;
  }

  // 流式传输文件
  File file = SD_MMC.open(filePath.c_str(), FILE_READ);
  if (!file || file.isDirectory()) {
    request->send(500, "text/plain", "Cannot open file");
    if (file)
      file.close();
    return;
  }

  String contentType = getContentType(filePath);
  String fileName = filePath.substring(filePath.lastIndexOf('/') + 1);

  AsyncWebServerResponse *response = request->beginResponse(
      contentType, file.size(),
      [file](uint8_t *buffer, size_t maxLen, size_t index) mutable -> size_t {
        if (!file)
          return 0;
        size_t bytesRead = file.read(buffer, maxLen);
        if (bytesRead == 0 || !file.available()) {
          file.close();
        }
        return bytesRead;
      });

  response->addHeader("Content-Disposition",
                      "attachment; filename=\"" + fileName + "\"");
  request->send(response);
}

String WifiTransferManager::listDirectory(const String &path) {
  StaticJsonDocument<4096> doc;
  JsonArray filesArray = doc.createNestedArray("files");

  File root = SD_MMC.open(path.c_str());
  if (!root || !root.isDirectory()) {
    doc["error"] = "Cannot open directory";
    String output;
    serializeJson(doc, output);
    return output;
  }

  File file = root.openNextFile();
  while (file) {
    JsonObject fileObj = filesArray.createNestedObject();
    String fileName = String(file.name());

    // 获取相对路径
    String fullPath = file.path();

    fileObj["name"] = fileName;
    fileObj["path"] = fullPath;
    fileObj["isDir"] = file.isDirectory();
    fileObj["size"] = file.isDirectory() ? 0 : file.size();

    file = root.openNextFile();
  }
  root.close();

  String output;
  serializeJson(doc, output);
  return output;
}

bool WifiTransferManager::isValidPath(const String &path) {
  // 基本安全检查：防止目录遍历攻击
  if (path.indexOf("..") != -1) {
    return false;
  }

  // 路径必须以/开头
  if (!path.startsWith("/")) {
    return false;
  }

  return true;
}

String WifiTransferManager::getContentType(const String &filename) {
  if (filename.endsWith(".csv"))
    return "text/csv";
  if (filename.endsWith(".json"))
    return "application/json";
  if (filename.endsWith(".jsonl"))
    return "application/x-jsonlines";
  if (filename.endsWith(".txt"))
    return "text/plain";
  if (filename.endsWith(".log"))
    return "text/plain";
  return "application/octet-stream";
}
