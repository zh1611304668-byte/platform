#include "WifiTransferManager.h"
#include "ConfigManager.h"
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

extern ConfigManager configManager;

// WiFi配置常量
static const char *DEFAULT_PASSWORD = "12345789";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

WifiTransferManager::WifiTransferManager()
    : active(false), password(DEFAULT_PASSWORD), server(nullptr) {
  // SSID将在start()时根据boatCode动态设置
  ssid = "Rowing_Data"; // 默认值，实际会被覆盖
}

WifiTransferManager::~WifiTransferManager() { stop(); }

bool WifiTransferManager::start() {
  if (active) {
    Serial.println("[WiFi传输] 已经处于激活状态");
    return true;
  }

  // 从ConfigManager获取船组编号，构建SSID
  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  if (deviceConfig.isValid && !deviceConfig.boatCode.isEmpty()) {
    ssid = "Rowing_" + deviceConfig.boatCode;
    Serial.printf("[WiFi传输] 使用船组编号: %s\n",
                  deviceConfig.boatCode.c_str());
  } else {
    ssid = "Rowing_Data"; // 如果配置未加载，使用默认名称
    Serial.println("[WiFi传输] 配置未就绪，使用默认SSID");
  }

  Serial.printf("[WiFi传输] 启动WiFi传输模式: %s\n", ssid.c_str());

  // 设置WiFi AP模式
  if (!setupWiFiAP()) {
    Serial.println("[WiFi传输] WiFi AP设置失败");
    return false;
  }

  // 创建并设置Web服务器
  server = new WebServer(80);
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
    server->stop();
    delete server;
    server = nullptr;
  }

  // 关闭WiFi
  teardownWiFi();

  active = false;
  Serial.println("[WiFi传输] ✅ WiFi传输模式已停止");
}

void WifiTransferManager::update() {
  // 处理Web服务器请求
  if (active && server) {
    server->handleClient();
  }

  // 定期检查WiFi状态（每30秒）
  static unsigned long lastCheck = 0;
  unsigned long now = millis();

  if (active && (now - lastCheck > 30000)) {
    lastCheck = now;
    int clientCount = WiFi.softAPgetStationNum();
    Serial.printf("[WiFi传输] 状态检查 - 连接客户端数: %d\n", clientCount);
  }
}

String WifiTransferManager::getIPAddress() const {
  if (active) {
    return WiFi.softAPIP().toString();
  }
  return "";
}

bool WifiTransferManager::setupWiFiAP() {
  // 断开所有现有连接并关闭WiFi
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500); // 增加延迟确保完全关闭

  // 设置为AP模式
  WiFi.mode(WIFI_AP);
  delay(100);

  // 配置AP模式
  if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
    Serial.println("[WiFi传输] ❌ AP配置失败");
    return false;
  }

  // 启动AP（无密码 - 开放网络）
  // 参数：SSID, 密码, 信道, 隐藏SSID, 最大连接数
  if (!WiFi.softAP(ssid.c_str(), NULL, 1, 0, 4)) {
    Serial.println("[WiFi传输] ❌ AP启动失败");
    return false;
  }

  // 等待AP完全启动
  delay(500);

  // 验证IP地址
  IPAddress ip = WiFi.softAPIP();
  if (ip == IPAddress(0, 0, 0, 0)) {
    Serial.println("[WiFi传输] ❌ 获取IP地址失败");
    return false;
  }

  Serial.printf("[WiFi传输] ✅ AP已启动 - SSID: %s\n", ssid.c_str());
  Serial.printf("[WiFi传输] ✅ IP: %s\n", ip.toString().c_str());

  return true;
}

void WifiTransferManager::teardownWiFi() {
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

void WifiTransferManager::setupWebServer() {
  // 主页 - 文件浏览器UI
  server->on("/", [this]() { this->handleRoot(); });

  // API: 获取文件列表
  server->on("/api/list", [this]() { this->handleFileList(); });

  // 文件下载
  server->on("/download", [this]() { this->handleFileDownload(); });

  // 404处理
  server->onNotFound(
      [this]() { server->send(404, "text/plain", "Not Found"); });
}

void WifiTransferManager::handleRoot() {
  // 简化HTML避免内存问题，添加下载进度显示
  const char *html =
      "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' "
      "content='width=device-width,initial-scale=1'><title>"
      "\xE6\x95\xB0\xE6\x8D\xAE\xE4\xBC\xA0\xE8\xBE\x93</"
      "title><style>body{font-family:Arial;margin:20px;background:#f5f5f5}.c{"
      "max-width:900px;margin:0 "
      "auto;background:#fff;padding:20px;border-radius:8px}h1{color:#333;"
      "margin:0 0 "
      "20px}.i{display:flex;justify-content:space-between;padding:12px;border-"
      "bottom:1px solid "
      "#eee}.n{font-weight:bold}.m{color:#999;font-size:.9em}.b{background:#"
      "007bff;color:#fff;border:none;padding:8px "
      "16px;border-radius:4px;cursor:pointer;text-decoration:none;transition:"
      "background "
      ".2s}.b:hover{background:#0056b3}.b:disabled{background:#ccc;cursor:not-"
      "allowed}.l{text-align:center;padding:20px;color:#999}.prog{position:"
      "fixed;bottom:20px;right:20px;background:rgba(0,0,0,.8);color:#fff;"
      "padding:15px "
      "20px;border-radius:8px;display:none;min-width:250px}.prog-bar{"
      "background:#444;height:6px;border-radius:3px;margin:10px "
      "0;overflow:hidden}.prog-fill{background:#4CAF50;height:100%;transition:"
      "width .3s}</style></head><body><div class='c'><h1>\xF0\x9F\x9A\xA3 "
      "\xE6\x95\xB0\xE6\x8D\xAE\xE4\xBC\xA0\xE8\xBE\x93</h1><div id='d'><div "
      "class='l'>\xE5\x8A\xA0\xE8\xBD\xBD...</div></div></div><div "
      "class='prog' id='prog'><div "
      "id='prog-text'>\xE4\xB8\x8B\xE8\xBD\xBD\xE4\xB8\xAD...</div><div "
      "class='prog-bar'><div class='prog-fill' id='prog-fill'></div></div><div "
      "id='prog-pct'>0%</div></div><script>let downloading=false;function "
      "load(p){fetch('/api/"
      "list?path='+encodeURIComponent(p)).then(r=>r.json()).then(d=>{let "
      "h='';if(!d.files||!d.files.length)h='<div "
      "class=\"l\">\xE7\xA9\xBA</div>';else d.files.forEach(f=>{h+='<div "
      "class=\"i\"><div><div class=\"n\">'+(f.isDir?'\xF0\x9F\x93\x81 "
      "':'\xF0\x9F\x93\x84 ')+f.name+'</div><div "
      "class=\"m\">'+(f.isDir?'\xE6\x96\x87\xE4\xBB\xB6\xE5\xA4\xB9':sz(f.size)"
      ")+'</div></div>';h+=f.isDir?'<button class=\"b\" "
      "onclick=\"load(\\''+f.path+'\\')\">\xE6\x89\x93\xE5\xBC\x80</"
      "button>':'<button class=\"b\" "
      "onclick=\"dl(\\''+f.path+'\\',\\''+f.name+'\\','+f.size+')\">"
      "\xE4\xB8\x8B\xE8\xBD\xBD</button>';h+='</"
      "div>'});document.getElementById('d').innerHTML=h}).catch(()=>document."
      "getElementById('d').innerHTML='<div "
      "class=\"l\">\xE5\xA4\xB1\xE8\xB4\xA5</div>')}function sz(b){return "
      "b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' "
      "KB':b<1073741824?(b/1048576).toFixed(1)+' "
      "MB':(b/1073741824).toFixed(1)+' GB'}function "
      "dl(path,name,size){if(downloading)return;downloading=true;const "
      "prog=document.getElementById('prog');const "
      "fill=document.getElementById('prog-fill');const "
      "pct=document.getElementById('prog-pct');const "
      "txt=document.getElementById('prog-text');prog.style.display='block';txt."
      "textContent='\xE4\xB8\x8B\xE8\xBD\xBD: '+name;let loaded=0;const "
      "xhr=new "
      "XMLHttpRequest();xhr.open('GET','/"
      "download?file='+encodeURIComponent(path),true);xhr.responseType='blob';"
      "xhr.onprogress=e=>{if(e.lengthComputable){loaded=e.loaded;const "
      "p=Math.round(e.loaded/"
      "e.total*100);fill.style.width=p+'%';pct.textContent=p+'% "
      "('+sz(e.loaded)+' / "
      "'+sz(e.total)+')'}};xhr.onload=()=>{if(xhr.status===200){const "
      "blob=xhr.response;const url=window.URL.createObjectURL(blob);const "
      "a=document.createElement('a');a.href=url;a.download=name;a.click();"
      "window.URL.revokeObjectURL(url);txt.textContent='"
      "\xE5\xAE\x8C\xE6\x88\x90!';setTimeout(()=>{prog.style.display='none';"
      "downloading=false;fill.style.width='0';pct.textContent='0%'},2000)}else{"
      "txt.textContent='\xE5\xA4\xB1\xE8\xB4\xA5: "
      "'+xhr.status;setTimeout(()=>{prog.style.display='none';downloading="
      "false},3000)}};xhr.onerror=()=>{txt.textContent='"
      "\xE7\xBD\x91\xE7\xBB\x9C\xE9\x94\x99\xE8\xAF\xAF';setTimeout(()=>{prog."
      "style.display='none';downloading=false},3000)};xhr.send()}load('/')</"
      "script></body></html>";

  server->send(200, "text/html", html);
}

void WifiTransferManager::handleFileList() {
  String path = "/";
  if (server->hasArg("path")) {
    path = server->arg("path");
  }

  // 验证路径安全性
  if (!isValidPath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid path\"}");
    return;
  }

  String jsonList = listDirectory(path);
  server->send(200, "application/json", jsonList);
}

void WifiTransferManager::handleFileDownload() {
  if (!server->hasArg("file")) {
    server->send(400, "text/plain", "Missing file parameter");
    return;
  }

  String filePath = server->arg("file");

  Serial.printf("[WiFi传输] 下载请求: %s\n", filePath.c_str());

  // 验证路径安全性
  if (!isValidPath(filePath)) {
    Serial.println("[WiFi传输] ❌ 无效的文件路径");
    server->send(400, "text/plain", "Invalid file path");
    return;
  }

  // 检查文件是否存在
  if (!SD_MMC.exists(filePath.c_str())) {
    Serial.println("[WiFi传输] ❌ 文件不存在");
    server->send(404, "text/plain", "File not found");
    return;
  }

  // 打开文件
  File file = SD_MMC.open(filePath.c_str(), FILE_READ);
  if (!file) {
    Serial.println("[WiFi传输] ❌ 无法打开文件");
    server->send(500, "text/plain", "Cannot open file");
    return;
  }

  if (file.isDirectory()) {
    Serial.println("[WiFi传输] ❌ 不能下载目录");
    file.close();
    server->send(500, "text/plain", "Cannot download directory");
    return;
  }

  String contentType = getContentType(filePath);
  String fileName = filePath.substring(filePath.lastIndexOf('/') + 1);

  Serial.printf("[WiFi传输] ✅ 开始传输: %s (%.2f KB)\n", fileName.c_str(),
                file.size() / 1024.0);

  // 发送响应头
  server->setContentLength(file.size());
  server->sendHeader("Content-Type", contentType);
  server->sendHeader("Content-Disposition",
                     "attachment; filename=\"" + fileName + "\"");
  server->sendHeader("Cache-Control", "no-cache");
  server->send(200);

  // 手动分块传输，每次重置看门狗
  const size_t CHUNK_SIZE = 1024; // 1KB chunks
  uint8_t buffer[CHUNK_SIZE];
  size_t totalSent = 0;

  while (file.available()) {
    size_t bytesRead = file.read(buffer, CHUNK_SIZE);
    if (bytesRead > 0) {
      server->client().write(buffer, bytesRead);
      totalSent += bytesRead;

      // 每4KB重置一次看门狗
      if (totalSent % 4096 == 0) {
        esp_task_wdt_reset();
        yield();
      }
    }
  }

  file.close();
  Serial.printf("[WiFi传输] ✅ 传输完成: %d bytes\n", totalSent);
}

String WifiTransferManager::listDirectory(const String &path) {
  // 使用更大的缓冲区以支持更多文件
  StaticJsonDocument<8192> doc;
  JsonArray filesArray = doc.createNestedArray("files");

  File root = SD_MMC.open(path.c_str());
  if (!root || !root.isDirectory()) {
    doc["error"] = "Cannot open directory";
    String output;
    serializeJson(doc, output);
    root.close();
    return output;
  }

  // 限制文件数量以防止内存溢出
  const int MAX_FILES = 100;
  int fileCount = 0;

  File file = root.openNextFile();
  while (file && fileCount < MAX_FILES) {
    JsonObject fileObj = filesArray.createNestedObject();
    String fileName = String(file.name());

    // 获取完整路径
    String fullPath = file.path();

    fileObj["name"] = fileName;
    fileObj["path"] = fullPath;
    fileObj["isDir"] = file.isDirectory();
    fileObj["size"] = file.isDirectory() ? 0 : file.size();

    file.close();
    file = root.openNextFile();
    fileCount++;

    // 允许其他任务运行
    yield();
  }

  if (file) {
    file.close();
  }
  root.close();

  // 如果达到文件数量限制，添加警告
  if (fileCount >= MAX_FILES) {
    doc["warning"] = "Directory contains more files than can be displayed";
  }

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
