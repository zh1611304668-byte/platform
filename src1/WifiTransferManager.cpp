/*
 * File: WifiTransferManager.cpp
 * Purpose: Implements runtime logic for the Wifi Transfer Manager module.
 */
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
  Serial.println("[WiFi传输] WiFi传输模式已启动");
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
  Serial.println("[WiFi传输] WiFi传输模式已停止");
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

  Serial.printf("[WiFi传输] AP已启动 - SSID: %s\n", ssid.c_str());
  Serial.printf("[WiFi传输] IP: %s\n", ip.toString().c_str());

  return true;
}

void WifiTransferManager::teardownWiFi() {
  // 1. 先断开所有AP客户端连接
  WiFi.softAPdisconnect(true);
  delay(200); // 等待客户端断开连接

  // 2. 关闭WiFi模式
  WiFi.mode(WIFI_OFF);
  delay(500); // 增加延迟，确保WiFi完全反初始化

  // 3. 彻底断开WiFi（清理内部状态）
  WiFi.disconnect(true);
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
  // Use raw string literal for the new UI
  // NOTE: Hex escapes (\xNN) are NOT processed in raw strings, so we use JS
  // unicode escapes or direct UTF-8
  const char *html = R"raw(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <title>数据传输</title>
    <style>
        :root {
            --primary-color: #007AFF;
            --background-color: #F2F2F7;
            --card-background: #FFFFFF;
            --text-primary: #1C1C1E;
            --text-secondary: #8E8E93;
            --separator-color: #E5E5EA;
            --touch-highlight: rgba(0, 0, 0, 0.05);
            --shadow: 0 4px 12px rgba(0, 0, 0, 0.08);
            --radius-l: 20px;
            --font-stack: -apple-system, "SF Pro Text", "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
        }
        body {
            font-family: var(--font-stack);
            background-color: var(--background-color);
            color: var(--text-primary);
            margin: 0;
            padding: 0;
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container {
            width: 100%;
            max-width: 640px;
            padding: 20px;
            box-sizing: border-box;
        }
        header { margin: 40px 0 24px; text-align: center; }
        h1 { font-size: 28px; font-weight: 700; margin: 0; letter-spacing: -0.5px; }
        .subtitle { font-size: 15px; color: var(--text-secondary); margin-top: 8px; }
        .card {
            background: var(--card-background);
            border-radius: var(--radius-l);
            box-shadow: var(--shadow);
            overflow: hidden;
            margin-bottom: 20px;
        }
        .file-item {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 16px 20px;
            border-bottom: 1px solid var(--separator-color);
            cursor: pointer;
            -webkit-tap-highlight-color: transparent;
        }
        .file-item:last-child { border-bottom: none; }
        .file-item:active { background-color: var(--touch-highlight); }
        .file-info { display: flex; align-items: center; flex: 1; min-width: 0; }
        .file-icon { font-size: 24px; margin-right: 16px; width: 32px; text-align: center; }
        .file-details { display: flex; flex-direction: column; overflow: hidden; }
        .file-name { font-size: 17px; font-weight: 500; white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
        .file-meta { font-size: 13px; color: var(--text-secondary); margin-top: 2px; }
        .action-icon { color: var(--primary-color); margin-left: 12px; font-size: 20px; opacity: 0.8; }
        .empty-state { padding: 40px; text-align: center; color: var(--text-secondary); }
        
        .pagination {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px 20px;
            background-color: var(--card-background);
            border-radius: var(--radius-l);
            box-shadow: var(--shadow);
        }
        .page-btn {
            background: none;
            border: none;
            color: var(--primary-color);
            font-size: 16px;
            padding: 10px 15px;
            cursor: pointer;
            border-radius: 8px;
        }
        .page-btn:disabled { color: var(--text-secondary); cursor: default; opacity: 0.5; }
        .page-btn:active:not(:disabled) { background-color: var(--touch-highlight); }
        .page-info { font-size: 14px; color: var(--text-secondary); }

        .overlay {
            position: fixed; top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(0, 0, 0, 0.4);
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            z-index: 1000;
            display: none;
            align-items: center; justify-content: center;
            opacity: 0; transition: opacity 0.3s ease;
        }
        .overlay.visible { opacity: 1; }
        .progress-card {
            background: var(--card-background);
            padding: 30px;
            border-radius: var(--radius-l);
            width: 80%; max-width: 320px;
            text-align: center;
            box-shadow: 0 8px 30px rgba(0, 0, 0, 0.12);
        }
        .progress-title { font-size: 17px; font-weight: 600; margin-bottom: 20px; }
        .progress-bar-bg { background-color: var(--separator-color); height: 6px; border-radius: 3px; overflow: hidden; margin-bottom: 12px; }
        .progress-bar-fill { height: 100%; background-color: var(--primary-color); width: 0%; transition: width 0.2s ease; }
        .progress-text { font-size: 13px; color: var(--text-secondary); font-variant-numeric: tabular-nums; }
        footer { margin-top: 20px; color: var(--text-secondary); font-size: 12px; text-align: center; padding-bottom: 20px; }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>数据传输</h1>
            <div class="subtitle">WiFi File Transfer</div>
        </header>
        <div class="card">
            <div id="file-list-container" class="file-list">
                <div class="empty-state">加载中...</div>
            </div>
        </div>
        
        <div class="pagination" id="pagination-controls" style="display:none;">
            <button class="page-btn" id="prev-btn" onclick="prevPage()">&larr; 上一页</button>
            <span class="page-info" id="page-info">第 1 页</span>
            <button class="page-btn" id="next-btn" onclick="nextPage()">下一页 &rarr;</button>
        </div>

        <footer>Rowing Performance Monitor</footer>
    </div>
    <div id="overlay" class="overlay">
        <div class="progress-card">
            <div class="progress-title">下载中...</div>
            <div class="progress-bar-bg"><div id="progress-fill" class="progress-bar-fill"></div></div>
            <div id="progress-text" class="progress-text">0%</div>
        </div>
    </div>
    <script>
        let downloading = false;
        let currentPath = '/';
        let currentPage = 1;
        const PAGE_LIMIT = 50; // Use a larger default limit
        let hasMoreFiles = false;
        
        function formatSize(b){if(b===0)return'0 B';const k=1024,s=['B','KB','MB','GB'],i=Math.floor(Math.log(b)/Math.log(k));return parseFloat((b/Math.pow(k,i)).toFixed(1))+' '+s[i]}
        
        function renderList(data){
            const files = data.files;
            const c=document.getElementById('file-list-container');
            let h='';
            
            // Add back button if not root - ONLY on first page
            // Or always show it? Usually it's always shown, or taken out of the list logic.
            // Let's keep it in the list for now logic-wise, but maybe handle specially for pagination.
            // If we are on page > 1, maybe "Back to Parent" is confusing if scrolling back up? 
            // Standard "Explorer" behavior: Back is nav, pagination is view. 
            // So Back returns to parent directory.
            
            if(currentPath !== '/' && currentPath !== '') {
                // Only show "Back" item on first page to save space? Or always?
                // Let's show it always to be safe navigation-wise.
                let parent = currentPath.substring(0, currentPath.lastIndexOf('/'));
                if(parent === '') parent = '/';
                h += `<div class="file-item" onclick="load('${parent}')"><div class="file-info"><div class="file-icon">\u21A9</div><div class="file-details"><div class="file-name">.. (\u8FD4\u56DE\u4E0A\u4E00\u7EA7)</div></div></div></div>`;
            }

            if(!files||!files.length){
                if(h === '') c.innerHTML='<div class="empty-state">暂无文件</div>';
                else c.innerHTML=h;
            } else {
                files.forEach(f=>{
                    const icon=f.isDir?'\u{1F4C1}':'\u{1F4C4}';
                    const meta=f.isDir?'文件夹':formatSize(f.size);
                    const act=f.isDir?`load('${f.path}')`: `download('${f.path}','${f.name}')`;
                    h+=`<div class="file-item" onclick="${act}"><div class="file-info"><div class="file-icon">${icon}</div><div class="file-details"><div class="file-name">${f.name}</div><div class="file-meta">${meta}</div></div></div><div class="action-icon">${f.isDir?'\u203A':'\u2193'}</div></div>`
                });
                c.innerHTML=h;
            }
            
            // Update Pagination UI
            currentPage = data.page || 1;
            hasMoreFiles = data.hasMore || false;
            
            const pagControls = document.getElementById('pagination-controls');
            if (currentPage === 1 && !hasMoreFiles) {
                pagControls.style.display = 'none';
            } else {
                pagControls.style.display = 'flex';
                document.getElementById('page-info').textContent = `第 ${currentPage} 页`;
                document.getElementById('prev-btn').disabled = (currentPage <= 1);
                document.getElementById('next-btn').disabled = !hasMoreFiles;
            }
        }
        
        function load(p, page = 1){
            // Reset to page 1 if changing directory
            if (p !== currentPath) {
                currentPath = p;
                page = 1;
            }
            currentPage = page;
            
            document.getElementById('file-list-container').innerHTML='<div class="empty-state">加载中...</div>';
            
            fetch(`/api/list?path=${encodeURIComponent(p)}&page=${page}&limit=${PAGE_LIMIT}`)
                .then(r=>r.json())
                .then(d=>renderList(d))
                .catch(e=>{
                    console.error(e);
                    document.getElementById('file-list-container').innerHTML='<div class="empty-state">加载失败</div>'
                });
        }
        
        function prevPage() {
            if (currentPage > 1) load(currentPath, currentPage - 1);
        }
        
        function nextPage() {
            if (hasMoreFiles) load(currentPath, currentPage + 1);
        }

        function download(path,name){
            // Use native browser download to avoid memory buffering lag
            const a = document.createElement('a');
            a.href = '/download?file=' + encodeURIComponent(path);
            a.download = name;
            a.style.display = 'none';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);

            // Show simple "Started" toast
            const ov=document.getElementById('overlay'),txt=document.getElementById('progress-text');
            const bg=document.querySelector('.progress-bar-bg');
            if(bg) bg.style.display='none'; // Hide progress bar
            
            txt.textContent = '下载已开始...';
            ov.style.display='flex';
            setTimeout(() => { ov.classList.add('visible'); }, 10);
            
            // Hide toast after 2 seconds
            setTimeout(()=>{
                ov.classList.remove('visible');
                setTimeout(()=>{
                    ov.style.display='none';
                    if(bg) bg.style.display='block'; // Reset for potential reuse
                },300);
            }, 2000);
        }
        
        // Initial load
        load('/');
    </script>
</body>
</html>
)raw";

  server->send(200, "text/html", html);
}

void WifiTransferManager::handleFileList() {
  String path = "/";
  if (server->hasArg("path")) {
    path = server->arg("path");
  }

  int page = 1;
  if (server->hasArg("page")) {
    page = server->arg("page").toInt();
    if (page < 1)
      page = 1;
  }

  int limit = 20;
  if (server->hasArg("limit")) {
    limit = server->arg("limit").toInt();
    if (limit < 1)
      limit = 20;
    if (limit > 100)
      limit = 100; // 限制最大每页数量
  }

  // 验证路径安全性
  if (!isValidPath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid path\"}");
    return;
  }

  String jsonList = listDirectory(path, page, limit);
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

  Serial.printf("[WiFi传输] 开始传输: %s (%.2f KB)\n", fileName.c_str(),
                file.size() / 1024.0);

  // 发送响应头
  server->setContentLength(file.size());
  server->sendHeader("Content-Disposition",
                     "attachment; filename=\"" + fileName + "\"");
  server->sendHeader("Cache-Control", "no-cache");
  server->send(200, contentType, "");

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
  Serial.printf("[WiFi传输] 传输完成: %d bytes\n", totalSent);
}

String WifiTransferManager::listDirectory(const String &path, int page,
                                          int limit) {
  // Use JsonDocument instead of deprecated StaticJsonDocument
  JsonDocument doc;
  JsonArray filesArray = doc["files"].to<JsonArray>();

  File root = SD_MMC.open(path.c_str());
  if (!root || !root.isDirectory()) {
    doc["error"] = "Cannot open directory";
    String output;
    serializeJson(doc, output);
    if (root)
      root.close();
    return output;
  }

  int skip = (page - 1) * limit;
  int count = 0;
  int added = 0;
  bool hasMore = false;

  File file = root.openNextFile();
  while (file) {
    // 跳过前面的文件
    if (count < skip) {
      count++;
      file.close();
      file = root.openNextFile();
      continue;
    }

    // 添加当前页的文件
    if (added < limit) {
      JsonObject fileObj = filesArray.add<JsonObject>();
      String fileName = String(file.name());
      String fullPath = file.path();

      fileObj["name"] = fileName;
      fileObj["path"] = fullPath;
      fileObj["isDir"] = file.isDirectory();
      fileObj["size"] = file.isDirectory() ? 0 : file.size();
      added++;
    } else {
      // 还有更多文件
      hasMore = true;
      file.close(); // 关闭当前文件
      break;        // 不需要继续遍历
    }

    file.close();
    file = root.openNextFile();
    count++;

    // 允许其他任务运行 (防止看门狗触发)
    if (count % 10 == 0) {
      yield();
    }
  }

  root.close();

  doc["page"] = page;
  doc["limit"] = limit;
  doc["hasMore"] = hasMore;

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
