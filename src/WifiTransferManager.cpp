#include "WifiTransferManager.h"
#include "ConfigManager.h"
#include "MQTTManager.h"
#include <Update.h>
#include <esp_task_wdt.h>

extern ConfigManager configManager;

static const char *DEFAULT_PASSWORD = "12345789";
static const IPAddress AP_IP(192, 168, 4, 1);
static const IPAddress AP_GATEWAY(192, 168, 4, 1);
static const IPAddress AP_SUBNET(255, 255, 255, 0);

static const int DEFAULT_FOLDER_PAGE_SIZE = 30;
static const int MAX_FOLDER_PAGE_SIZE = 60;
static const int MAX_FILES_PER_RESPONSE = 200;
static const unsigned long DOWNLOAD_STALL_TIMEOUT_MS = 30000;
static const size_t OTA_MAX_BIN_SIZE = 0x300000;
static uint32_t g_downloadSessionCounter = 0;
static uint32_t g_activeSessionId = 0;
static String g_activeSessionFile;
static size_t g_activeSessionSent = 0;
static size_t g_activeSessionTotal = 0;
static bool g_activeSessionRunning = false;
static String g_lastTransferError;

WifiTransferManager::WifiTransferManager()
    : active(false), ssid("Rowing_Data"), password(DEFAULT_PASSWORD),
      server(nullptr), otaInProgress(false), otaResultOk(false),
      otaResultMsg(""), otaWrittenBytes(0), otaExpectedBytes(0),
      otaNetworkSuspended(false) {}

WifiTransferManager::~WifiTransferManager() { stop(); }

bool WifiTransferManager::start() {
  if (active) {
    Serial.println("[WiFiTransfer] already active");
    return true;
  }

  const DeviceConfig &deviceConfig = configManager.getDeviceConfig();
  if (deviceConfig.isValid && !deviceConfig.boatCode.isEmpty()) {
    ssid = "Rowing_" + deviceConfig.boatCode;
  } else {
    ssid = "Rowing_Data";
  }

  Serial.printf("[WiFiTransfer] starting AP: %s\n", ssid.c_str());

  if (!setupWiFiAP()) {
    Serial.println("[WiFiTransfer] setup AP failed");
    return false;
  }

  server = new WebServer(80);
  setupWebServer();
  server->begin();

  active = true;
  Serial.printf("[WiFiTransfer] ready at http://%s\n",
                WiFi.softAPIP().toString().c_str());
  return true;
}

void WifiTransferManager::stop() {
  if (!active) {
    return;
  }

  Serial.println("[WiFiTransfer] stopping");

  if (server) {
    server->stop();
    delete server;
    server = nullptr;
  }

  if (otaNetworkSuspended) {
    requestNetworkResume();
    otaNetworkSuspended = false;
  }

  teardownWiFi();
  active = false;
}

void WifiTransferManager::update() {
  if (active && server) {
    server->handleClient();
  }

  static unsigned long lastCheck = 0;
  const unsigned long now = millis();

  if (active && (now - lastCheck > 30000)) {
    lastCheck = now;
    Serial.printf("[WiFiTransfer] clients: %d\n", WiFi.softAPgetStationNum());
  }
}

String WifiTransferManager::getIPAddress() const {
  return active ? WiFi.softAPIP().toString() : "";
}

bool WifiTransferManager::setupWiFiAP() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(500);

  WiFi.mode(WIFI_AP);
  delay(100);

  if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) {
    Serial.println("[WiFiTransfer] AP config failed");
    return false;
  }

  if (!WiFi.softAP(ssid.c_str(), nullptr, 1, 0, 4)) {
    Serial.println("[WiFiTransfer] AP start failed");
    return false;
  }

  delay(500);
  IPAddress ip = WiFi.softAPIP();
  if (ip == IPAddress(0, 0, 0, 0)) {
    Serial.println("[WiFiTransfer] AP IP invalid");
    return false;
  }

  return true;
}

void WifiTransferManager::teardownWiFi() {
  WiFi.softAPdisconnect(true);
  delay(200);

  WiFi.mode(WIFI_OFF);
  delay(500);

  WiFi.disconnect(true);
  delay(100);
}

void WifiTransferManager::setupWebServer() {
  const char *headerKeys[] = {"Range"};
  server->collectHeaders(headerKeys, 1);

  server->on("/", [this]() { this->handleRoot(); });
  server->on("/ota", [this]() { this->handleOtaPage(); });
  server->on("/update", HTTP_POST, [this]() { this->handleOtaUploadPost(); },
             [this]() { this->handleOtaUpload(); });
  server->on("/api/list", [this]() { this->handleFileList(); });
  server->on("/api/fs/status", [this]() { this->handleFsStatus(); });
  server->on("/api/fs/item", HTTP_PUT, [this]() { this->handleFileItemPut(); });
  server->on("/api/fs/item", HTTP_DELETE, [this]() { this->handleFileItemDelete(); });
  server->on("/api/fs/upload", HTTP_POST, [this]() { this->handleFileUploadPost(); },
             [this]() { this->handleFileUpload(); });
  server->on("/download", [this]() { this->handleFileDownload(); });
  server->onNotFound([this]() { server->send(404, "text/plain", "Not Found"); });
}

void WifiTransferManager::handleRoot() {
  const char *html = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>WiFi File Transfer</title>
<style>
:root{
  --bg:#f4f6f8; --panel:#ffffff; --line:#dde3ea; --text:#1f2937;
  --muted:#6b7280; --brand:#0b6bcb; --brand-soft:#e9f3ff;
}
*{box-sizing:border-box}
html,body{margin:0;height:100%;font-family:"Segoe UI",Arial,sans-serif;background:var(--bg);color:var(--text)}
.app{display:flex;height:100vh;width:100vw;overflow:hidden}
.left,.right{display:flex;flex-direction:column;background:var(--panel)}
.left{width:19%;min-width:145px;border-right:1px solid var(--line)}
.right{flex:1;min-width:340px}
.head{padding:14px 16px;border-bottom:1px solid var(--line)}
.title{font-size:16px;font-weight:700}
.sub{margin-top:4px;color:var(--muted);font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.list{flex:1;overflow:auto;padding:8px}
.row{display:flex;align-items:center;justify-content:space-between;gap:10px;padding:10px 12px;margin:6px 0;border:1px solid var(--line);border-radius:8px;background:#fff;cursor:pointer}
.row:hover{background:#f9fbfd}
.name{white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.meta{font-size:12px;color:var(--muted)}
.btn{border:1px solid var(--line);background:#fff;color:var(--text);border-radius:8px;padding:6px 10px;cursor:pointer;display:inline-flex;align-items:center;justify-content:center;line-height:1;text-decoration:none;user-select:none}
.btn:hover{background:#f9fbfd}
.btn.primary{border-color:var(--brand);color:var(--brand)}
a.btn,a.btn:visited,a.btn:hover,a.btn:active{text-decoration:none}
.download-btn.disabled,.download-btn[aria-disabled="true"]{opacity:.45;pointer-events:none;border-color:#cbd5e1;color:#94a3b8;background:#f8fafc}
.footer{padding:10px 12px;border-top:1px solid var(--line);display:flex;align-items:center;justify-content:space-between;gap:8px}
.note{padding:8px 12px;color:#8a5500;background:#fff7e8;border-top:1px solid #f2dcaf;font-size:12px}
.empty{padding:16px;text-align:center;color:var(--muted)}
@media (max-width:900px){.app{flex-direction:column}.left{width:100%;min-width:0;height:46%}.right{min-width:0;height:54%}}
</style>
</head>
<body>
<div class="app">
  <section class="left">
    <div class="head">
      <div class="title" id="titleFolder"></div>
      <div class="sub" id="pathLabel">/</div>
    </div>
    <div class="list" id="folderList"><div class="empty" id="loadingText"></div></div>
    <div class="footer">
      <button class="btn" id="prevBtn" onclick="prevPage()"></button>
      <span class="meta" id="pageInfo"></span>
      <button class="btn" id="nextBtn" onclick="nextPage()"></button>
    </div>
  </section>
  <section class="right">
    <div class="head">
      <div class="title" id="titleFile"></div>
      <div class="sub" id="subtitleFile"></div>
    </div>
    <div class="list" id="fileList"><div class="empty" id="selectFolderText"></div></div>
    <div class="note" id="fileNote" style="display:none"></div>
  </section>
</div>
<script>
const TEXT = {
  folder: '\u76ee\u5f55',
  file: '\u6587\u4ef6',
  subtitle: '\u70b9\u51fb\u4e0b\u8f7d\uff0c\u652f\u6301\u65ad\u70b9\u7eed\u4f20',
  loading: '\u52a0\u8f7d\u4e2d...',
  selectFolder: '\u8bf7\u9009\u62e9\u76ee\u5f55',
  prev: '\u4e0a\u4e00\u9875',
  next: '\u4e0b\u4e00\u9875',
  page: '\u7b2c {n} \u9875',
  back: '.. \u8fd4\u56de\u4e0a\u4e00\u7ea7',
  emptyFolders: '\u5f53\u524d\u76ee\u5f55\u6ca1\u6709\u5b50\u6587\u4ef6\u5939',
  emptyFiles: '\u5f53\u524d\u76ee\u5f55\u6ca1\u6709\u6587\u4ef6',
  download: '\u4e0b\u8f7d',
  requestFailed: '\u8bf7\u6c42\u5931\u8d25',
  loadFailed: '\u52a0\u8f7d\u5931\u8d25',
  folderType: '\u76ee\u5f55',
  tooManyFiles: '\u5f53\u524d\u76ee\u5f55\u6587\u4ef6\u8f83\u591a\uff0c\u5df2\u9650\u5236\u8fd4\u56de\u524d 200 \u4e2a\u6587\u4ef6\u3002'
};

const PAGE_SIZE = 30;
let currentPath = '/';
let currentPage = 1;
let hasMoreFolders = false;
let downloadLocked = false;
let downloadPollTimer = 0;
let downloadSessionWatchStart = 0;
let downloadOwnerHref = '';

function escapeHtml(v){
  return String(v).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/\"/g,'&quot;').replace(/'/g,'&#39;');
}

function formatSize(bytes){
  if(bytes===0) return '0 B';
  const units=['B','KB','MB','GB']; let n=bytes, idx=0;
  while(n>=1024 && idx<units.length-1){n/=1024;idx++;}
  return n.toFixed(idx===0?0:1)+' '+units[idx];
}

function tPage(n){ return TEXT.page.replace('{n}', n); }

function setDownloadButtonsLocked(locked, ownerHref){
  const buttons = document.querySelectorAll('#fileList .download-btn');
  for(const btn of buttons){
    const isOwner = ownerHref && btn.getAttribute('href') === ownerHref;
    if(locked && !isOwner){
      btn.classList.add('disabled');
      btn.setAttribute('aria-disabled','true');
      btn.tabIndex = -1;
    } else {
      btn.classList.remove('disabled');
      btn.removeAttribute('aria-disabled');
      btn.tabIndex = 0;
    }
  }
}

function releaseDownloadLock(){
  downloadLocked = false;
  downloadOwnerHref = '';
  if(downloadPollTimer){
    clearInterval(downloadPollTimer);
    downloadPollTimer = 0;
  }
  setDownloadButtonsLocked(false, '');
}

function pollDownloadState(){
  fetch('/api/fs/status')
    .then(r => r.json())
    .then(s => {
      if(!s.activeSessionRunning){
        releaseDownloadLock();
      }
    })
    .catch(() => {
      if(Date.now() - downloadSessionWatchStart > 15000){
        releaseDownloadLock();
      }
    });
}

function startDownload(anchor){
  if(downloadLocked){
    return false;
  }
  downloadLocked = true;
  downloadOwnerHref = anchor.getAttribute('href') || '';
  downloadSessionWatchStart = Date.now();
  setDownloadButtonsLocked(true, downloadOwnerHref);

  if(downloadPollTimer){
    clearInterval(downloadPollTimer);
  }
  downloadPollTimer = setInterval(pollDownloadState, 800);
  setTimeout(pollDownloadState, 300);
  return true;
}

function render(data){
  currentPath = data.path || '/';
  currentPage = data.page || 1;
  hasMoreFolders = !!data.hasMoreFolders;

  document.getElementById('pathLabel').textContent = currentPath;
  document.getElementById('pageInfo').textContent = tPage(currentPage);
  document.getElementById('prevBtn').disabled = currentPage <= 1;
  document.getElementById('nextBtn').disabled = !hasMoreFolders;

  const folderList = document.getElementById('folderList');
  const folders = data.folders || [];
  let folderHtml = '';
  if(currentPath !== '/'){
    folderHtml += `<div class="row" onclick="loadPath('${encodeURIComponent(data.parent || '/')}')"><span class="name">${TEXT.back}</span><span class="meta">${TEXT.folderType}</span></div>`;
  }
  if(!folders.length){
    folderHtml += `<div class="empty">${TEXT.emptyFolders}</div>`;
  } else {
    for(const f of folders){
      const encoded = encodeURIComponent(f.path);
      folderHtml += `<div class="row" onclick="loadPath('${encoded}')"><span class="name">${escapeHtml(f.name)}</span><span class="meta">${TEXT.folderType}</span></div>`;
    }
  }
  folderList.innerHTML = folderHtml;

  const fileList = document.getElementById('fileList');
  const files = data.files || [];
  if(!files.length){
    fileList.innerHTML = `<div class="empty">${TEXT.emptyFiles}</div>`;
  } else {
    let fileHtml = '';
    for(const f of files){
      const dl = '/download?file=' + encodeURIComponent(f.path);
      fileHtml += `<div class="row"><div style="min-width:0;flex:1"><div class="name">${escapeHtml(f.name)}</div><div class="meta">${formatSize(f.size||0)}</div></div><a class="btn primary download-btn" href="${dl}" download="${escapeHtml(f.name)}" onclick="return startDownload(this)">${TEXT.download}</a></div>`;
    }
    fileList.innerHTML = fileHtml;
    if(downloadLocked){
      setDownloadButtonsLocked(true, downloadOwnerHref);
    }
  }

  const fileNote = document.getElementById('fileNote');
  if(data.hasMoreFiles){
    fileNote.style.display = 'block';
    fileNote.textContent = TEXT.tooManyFiles;
  } else {
    fileNote.style.display = 'none';
  }
}

function load(path, page){
  const reqPath = path || '/';
  const reqPage = page || 1;
  fetch(`/api/list?path=${encodeURIComponent(reqPath)}&page=${reqPage}&limit=${PAGE_SIZE}`)
    .then(r => r.json())
    .then(d => {
      if(d.error){
        document.getElementById('folderList').innerHTML = `<div class="empty">${escapeHtml(d.error)}</div>`;
        document.getElementById('fileList').innerHTML = `<div class="empty">${TEXT.requestFailed}</div>`;
        return;
      }
      render(d);
    })
    .catch(() => {
      document.getElementById('folderList').innerHTML = `<div class="empty">${TEXT.loadFailed}</div>`;
      document.getElementById('fileList').innerHTML = `<div class="empty">${TEXT.loadFailed}</div>`;
    });
}

function loadPath(encodedPath){ currentPage = 1; load(decodeURIComponent(encodedPath), 1); }
function prevPage(){ if(currentPage > 1){ load(currentPath, currentPage - 1); } }
function nextPage(){ if(hasMoreFolders){ load(currentPath, currentPage + 1); } }

document.getElementById('titleFolder').textContent = TEXT.folder;
document.getElementById('titleFile').textContent = TEXT.file;
document.getElementById('subtitleFile').textContent = TEXT.subtitle;
document.getElementById('loadingText').textContent = TEXT.loading;
document.getElementById('selectFolderText').textContent = TEXT.selectFolder;
document.getElementById('prevBtn').textContent = TEXT.prev;
document.getElementById('nextBtn').textContent = TEXT.next;
document.getElementById('pageInfo').textContent = tPage(1);

load('/', 1);
</script>
</body>
</html>
)rawliteral";

  server->send(200, "text/html; charset=utf-8", html);
}
void WifiTransferManager::handleOtaPage() {
  const char *html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1.0" />
<title>OTA Update</title>
<style>
body{font-family:Arial,sans-serif;background:#f4f6f8;margin:0;padding:24px;color:#1f2937}
.card{max-width:560px;margin:0 auto;background:#fff;border:1px solid #dde3ea;border-radius:12px;padding:18px}
h2{margin:0 0 12px}
.row{margin:10px 0}
button{border:1px solid #0b6bcb;color:#0b6bcb;background:#fff;padding:8px 14px;border-radius:8px;cursor:pointer}
button:disabled{opacity:.5;cursor:not-allowed}
.progress{height:10px;background:#e5e7eb;border-radius:999px;overflow:hidden}
.bar{height:100%;width:0;background:#0b6bcb}
.log{margin-top:10px;font-size:13px;white-space:pre-wrap}
.note{font-size:12px;color:#6b7280}
</style>
</head>
<body>
<div class="card">
  <h2>Firmware OTA Update</h2>
  <div class="row"><input id="bin" type="file" accept=".bin" /></div>
  <div class="row"><button id="btn" onclick="startOta()">Start Update</button></div>
  <div class="progress"><div id="bar" class="bar"></div></div>
  <div id="log" class="log">Select firmware.bin</div>
  <div class="note">Device will restart automatically after a successful update.</div>
</div>
<script>
function setLog(t){document.getElementById('log').textContent=t;}
function setProgress(v){document.getElementById('bar').style.width=v+'%';}
function startOta(){
  const f = document.getElementById('bin').files[0];
  if(!f){ setLog('Please select a .bin file'); return; }
  const btn = document.getElementById('btn');
  btn.disabled = true;
  setLog('Uploading...');
  const fd = new FormData();
  fd.append('update', f);
  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/update?size=' + f.size);
  xhr.upload.onprogress = (e)=>{
    if(e.lengthComputable){ setProgress(Math.round((e.loaded/e.total)*100)); }
  };
  xhr.onload = ()=>{
    btn.disabled = false;
    setProgress(100);
    setLog(xhr.responseText || ('HTTP ' + xhr.status));
  };
  xhr.onerror = ()=>{
    btn.disabled = false;
    setLog('Upload failed');
  };
  xhr.send(fd);
}
</script>
</body>
</html>
)rawliteral";

  server->send(200, "text/html; charset=utf-8", html);
}

void WifiTransferManager::handleOtaUploadPost() {
  if (otaInProgress) {
    server->send(409, "text/plain", "OTA still in progress");
    return;
  }

  if (!otaResultOk) {
    String msg = "OTA failed";
    if (otaResultMsg.length() > 0) {
      msg += ": " + otaResultMsg;
    }
    server->send(500, "text/plain", msg);
    return;
  }

  String okMsg = "OTA success, rebooting... bytes=" + String((unsigned long)otaWrittenBytes);
  server->send(200, "text/plain", okMsg);
  delay(500);
  ESP.restart();
}

void WifiTransferManager::handleOtaUpload() {
  HTTPUpload &upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    otaInProgress = true;
    otaResultOk = false;
    otaResultMsg = "";
    otaWrittenBytes = 0;
    otaExpectedBytes = server->hasArg("size") ? (size_t)server->arg("size").toInt() : 0;

    if (otaExpectedBytes == 0 || otaExpectedBytes > OTA_MAX_BIN_SIZE) {
      otaResultMsg = "invalid size";
      otaInProgress = false;
      return;
    }

    otaNetworkSuspended = requestNetworkSuspend();
    if (!otaNetworkSuspended) {
      Serial.println("[WiFiTransfer][OTA] warning: network suspend failed");
    }

    if (!Update.begin(otaExpectedBytes, U_FLASH)) {
      otaResultMsg = String("Update.begin failed: ") + Update.errorString();
      otaInProgress = false;
      if (otaNetworkSuspended) {
        requestNetworkResume();
        otaNetworkSuspended = false;
      }
      return;
    }

    Serial.printf("[WiFiTransfer][OTA] start, size=%u\n", (unsigned)otaExpectedBytes);
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (!otaInProgress || otaResultMsg.length() > 0) {
      return;
    }

    size_t written = Update.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      otaResultMsg = String("Update.write failed: ") + Update.errorString();
      Update.abort();
      otaInProgress = false;
      if (otaNetworkSuspended) {
        requestNetworkResume();
        otaNetworkSuspended = false;
      }
      return;
    }

    otaWrittenBytes += written;
    esp_task_wdt_reset();
    yield();
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (otaInProgress && otaResultMsg.length() == 0) {
      if (Update.end(true)) {
        otaResultOk = true;
        otaResultMsg = "ok";
        Serial.printf("[WiFiTransfer][OTA] done bytes=%u\n", (unsigned)otaWrittenBytes);
      } else {
        otaResultMsg = String("Update.end failed: ") + Update.errorString();
      }
    }

    otaInProgress = false;
    if (otaNetworkSuspended) {
      requestNetworkResume();
      otaNetworkSuspended = false;
    }
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    otaInProgress = false;
    otaResultOk = false;
    otaResultMsg = "OTA aborted";
    if (otaNetworkSuspended) {
      requestNetworkResume();
      otaNetworkSuspended = false;
    }
    Serial.println("[WiFiTransfer][OTA] aborted");
  }
}
void WifiTransferManager::handleFileList() {
  String path = server->hasArg("path") ? server->arg("path") : "/";

  int page = 1;
  if (server->hasArg("page")) {
    page = server->arg("page").toInt();
    if (page < 1) {
      page = 1;
    }
  }

  int limit = DEFAULT_FOLDER_PAGE_SIZE;
  if (server->hasArg("limit")) {
    limit = server->arg("limit").toInt();
  }
  if (limit < 1) {
    limit = DEFAULT_FOLDER_PAGE_SIZE;
  }
  if (limit > MAX_FOLDER_PAGE_SIZE) {
    limit = MAX_FOLDER_PAGE_SIZE;
  }

  if (!isValidPath(path)) {
    server->send(400, "application/json", "{\"error\":\"Invalid path\"}");
    return;
  }

  server->send(200, "application/json", listDirectory(path, page, limit));
}

void WifiTransferManager::handleFileDownload() {
  if (otaInProgress) {
    server->send(503, "text/plain", "OTA in progress");
    return;
  }
  if (!server->hasArg("file")) {
    server->send(400, "text/plain", "Missing file parameter");
    return;
  }

  String filePath = server->arg("file");
  if (!isValidPath(filePath)) {
    server->send(400, "text/plain", "Invalid file path");
    return;
  }

  uint32_t sessionId = ++g_downloadSessionCounter;
  unsigned long sessionStartMs = millis();
  g_activeSessionId = sessionId;
  g_activeSessionFile = filePath;
  g_activeSessionSent = 0;
  g_activeSessionTotal = 0;
  g_activeSessionRunning = false;
  g_lastTransferError = "";
  Serial.printf("[WiFiTransfer][S%lu] request file=%s\n", (unsigned long)sessionId,
                filePath.c_str());

  if (!SD_MMC.exists(filePath.c_str())) {
    g_lastTransferError = "file not found";
    Serial.printf("[WiFiTransfer][S%lu] file not found\n", (unsigned long)sessionId);
    server->send(404, "text/plain", "File not found");
    return;
  }

  File file = SD_MMC.open(filePath.c_str(), FILE_READ);
  if (!file) {
    g_lastTransferError = "open failed";
    Serial.printf("[WiFiTransfer][S%lu] open failed\n", (unsigned long)sessionId);
    server->send(500, "text/plain", "Cannot open file");
    return;
  }

  if (file.isDirectory()) {
    file.close();
    Serial.printf("[WiFiTransfer][S%lu] target is directory\n",
                  (unsigned long)sessionId);
    server->send(400, "text/plain", "Cannot download directory");
    return;
  }

  const size_t fileSize = file.size();
  size_t start = 0;
  size_t end = (fileSize > 0) ? (fileSize - 1) : 0;
  bool isPartial = false;

  if (fileSize > 0 && server->hasHeader("Range")) {
    String rangeHeader = server->header("Range");
    if (!parseRangeHeader(rangeHeader, fileSize, start, end)) {
      server->sendHeader("Content-Range", "bytes */" + String(fileSize));
      server->send(416, "text/plain", "Invalid Range");
      file.close();
      Serial.printf("[WiFiTransfer][S%lu] invalid range: %s\n",
                    (unsigned long)sessionId, rangeHeader.c_str());
      return;
    }
    isPartial = true;
  }

  const size_t contentLength = (fileSize > 0) ? (end - start + 1) : 0;
  if (start > 0 && !file.seek(start)) {
    file.close();
    server->send(500, "text/plain", "Seek failed");
    Serial.printf("[WiFiTransfer][S%lu] seek failed at %u\n",
                  (unsigned long)sessionId, (unsigned)start);
    return;
  }

  g_activeSessionTotal = contentLength;
  g_activeSessionRunning = true;

  String fileName = filePath.substring(filePath.lastIndexOf('/') + 1);
  String contentType = getContentType(filePath);

  Serial.printf(
      "[WiFiTransfer][S%lu] start tx name=%s size=%u range=%u-%u len=%u partial=%d\n",
      (unsigned long)sessionId, fileName.c_str(), (unsigned)fileSize,
      (unsigned)start, (unsigned)end, (unsigned)contentLength, isPartial ? 1 : 0);

  server->sendHeader("Content-Disposition",
                     "attachment; filename=\"" + fileName + "\"");
  server->sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server->sendHeader("Pragma", "no-cache");
  server->sendHeader("Connection", "close");
  server->sendHeader("Accept-Ranges", "bytes");
  bool networkSuspended = requestNetworkSuspend();
  if (!networkSuspended) {
    Serial.printf("[WiFiTransfer][S%lu] warning: network suspend failed\n",
                  (unsigned long)sessionId);
  }

  if (isPartial) {
    server->setContentLength(contentLength);
    server->sendHeader("Content-Range",
                       "bytes " + String(start) + "-" + String(end) + "/" +
                           String(fileSize));
    server->send(206, contentType, "");
  } else {
    server->setContentLength(contentLength);
    server->send(200, contentType, "");
  }

  WiFiClient client = server->client();
  client.setNoDelay(true);
  client.setTimeout(120);

  const size_t CHUNK_SIZE = 1024;
  const size_t PROGRESS_STEP_BYTES = 64 * 1024;
  uint8_t buffer[CHUNK_SIZE];
  size_t remaining = contentLength;
  size_t sentTotal = 0;
  size_t lastProgressBytes = 0;
  unsigned long lastFeed = millis();
  unsigned long noProgressSince = millis();
  unsigned long lastProgressMs = millis();
  bool aborted = false;

  while (remaining > 0 && client.connected()) {
    esp_task_wdt_reset();
    yield();

    size_t toRead = remaining > CHUNK_SIZE ? CHUNK_SIZE : remaining;
    size_t bytesRead = file.read(buffer, toRead);
    if (bytesRead == 0) {
      aborted = true;
      g_lastTransferError = "sd read returned 0";
      Serial.printf("[WiFiTransfer][S%lu] sd read returned 0, remaining=%u\n",
                    (unsigned long)sessionId, (unsigned)remaining);
      break;
    }

    size_t offset = 0;
    while (offset < bytesRead && client.connected()) {
      esp_task_wdt_reset();
      yield();

      size_t bytesWritten = client.write(buffer + offset, bytesRead - offset);
      if (bytesWritten > 0) {
        offset += bytesWritten;
        remaining -= bytesWritten;
        sentTotal += bytesWritten;
        noProgressSince = millis();
        g_activeSessionSent = sentTotal;

        bool stepReached = (sentTotal - lastProgressBytes) >= PROGRESS_STEP_BYTES;
        bool intervalReached = (millis() - lastProgressMs) >= 1000;
        if (stepReached || intervalReached || remaining == 0) {
          unsigned long percent =
              (contentLength == 0) ? 100 : (unsigned long)((sentTotal * 100UL) / contentLength);
          Serial.printf("[WiFiTransfer][S%lu] progress %u/%u (%lu%%)\n",
                        (unsigned long)sessionId, (unsigned)sentTotal,
                        (unsigned)contentLength, percent);
          lastProgressBytes = sentTotal;
          lastProgressMs = millis();
        }
      } else {
        if (millis() - noProgressSince > DOWNLOAD_STALL_TIMEOUT_MS) {
          aborted = true;
          g_lastTransferError = "stall timeout";
          Serial.printf("[WiFiTransfer][S%lu] stall timeout, remaining=%u\n",
                        (unsigned long)sessionId, (unsigned)remaining);
          break;
        }
        delay(2);
        esp_task_wdt_reset();
        yield();
      }

      if (millis() - lastFeed >= 20) {
        esp_task_wdt_reset();
        yield();
        lastFeed = millis();
      }
    }

    if (aborted) {
      break;
    }
  }

  file.close();
  client.stop();
  if (networkSuspended) {
    if (!requestNetworkResume()) {
      Serial.printf("[WiFiTransfer][S%lu] warning: network resume failed\n",
                    (unsigned long)sessionId);
    }
  }

  g_activeSessionRunning = false;
  g_activeSessionSent = sentTotal;
  unsigned long costMs = millis() - sessionStartMs;
  unsigned long speedBps = (costMs > 0) ? (unsigned long)((sentTotal * 1000UL) / costMs) : 0;
  if (remaining != 0 || aborted) {
    if (g_lastTransferError.length() == 0) {
      g_lastTransferError = "interrupted";
    }
    Serial.printf(
        "[WiFiTransfer][S%lu] interrupted sent=%u remaining=%u cost=%lums speed=%luB/s\n",
        (unsigned long)sessionId, (unsigned)sentTotal, (unsigned)remaining,
        costMs, speedBps);
  } else {
    g_lastTransferError = "";
    Serial.printf(
        "[WiFiTransfer][S%lu] done sent=%u cost=%lums speed=%luB/s\n",
        (unsigned long)sessionId, (unsigned)sentTotal, costMs, speedBps);
  }
}
void WifiTransferManager::handleFsStatus() {
  String json;
  json.reserve(320);
  json += "{\"ok\":true";
  json += ",\"apClients\":" + String(WiFi.softAPgetStationNum());
  json += ",\"ip\":\"" + escapeJson(WiFi.softAPIP().toString()) + "\"";
  json += ",\"fsTotal\":" + String((unsigned long)SD_MMC.totalBytes());
  json += ",\"fsUsed\":" + String((unsigned long)SD_MMC.usedBytes());
  json += ",\"activeSessionId\":" + String((unsigned long)g_activeSessionId);
  json += ",\"activeSessionFile\":\"" + escapeJson(g_activeSessionFile) + "\"";
  json += ",\"activeSessionRunning\":" + String(g_activeSessionRunning ? "true" : "false");
  json += ",\"activeSessionSent\":" + String((unsigned long)g_activeSessionSent);
  json += ",\"activeSessionTotal\":" + String((unsigned long)g_activeSessionTotal);
  json += ",\"lastTransferError\":\"" + escapeJson(g_lastTransferError) + "\"";
  json += ",\"otaInProgress\":" + String(otaInProgress ? "true" : "false");
  json += ",\"otaResultOk\":" + String(otaResultOk ? "true" : "false");
  json += ",\"otaWritten\":" + String((unsigned long)otaWrittenBytes);
  json += ",\"otaExpected\":" + String((unsigned long)otaExpectedBytes);
  json += ",\"otaResultMsg\":\"" + escapeJson(otaResultMsg) + "\"";
  json += "}";
  server->send(200, "application/json", json);
}

void WifiTransferManager::handleFileItemPut() {
  if (!server->hasArg("op") || !server->hasArg("path")) {
    server->send(400, "application/json", "{\"error\":\"Missing op/path\"}");
    return;
  }

  String op = server->arg("op");
  String path = server->arg("path");
  op.toLowerCase();

  if (!isValidPath(path) || path == "/") {
    server->send(400, "application/json", "{\"error\":\"Invalid path\"}");
    return;
  }

  bool ok = false;
  if (op == "mkdir") {
    ok = createDirectories(path);
  } else if (op == "touch") {
    File f = SD_MMC.open(path.c_str(), FILE_WRITE);
    ok = (bool)f;
    if (f) {
      f.close();
    }
  } else if (op == "rename" || op == "move") {
    if (!server->hasArg("dst")) {
      server->send(400, "application/json", "{\"error\":\"Missing dst\"}");
      return;
    }
    String dst = server->arg("dst");
    if (!isValidPath(dst) || dst == "/") {
      server->send(400, "application/json", "{\"error\":\"Invalid dst\"}");
      return;
    }
    ok = SD_MMC.rename(path.c_str(), dst.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Unsupported op\"}");
    return;
  }

  server->send(ok ? 200 : 500, "application/json",
               ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WifiTransferManager::handleFileItemDelete() {
  if (!server->hasArg("path")) {
    server->send(400, "application/json", "{\"error\":\"Missing path\"}");
    return;
  }

  String path = server->arg("path");
  if (!isValidPath(path) || path == "/") {
    server->send(400, "application/json", "{\"error\":\"Invalid path\"}");
    return;
  }

  if (!SD_MMC.exists(path.c_str())) {
    server->send(404, "application/json", "{\"error\":\"Not found\"}");
    return;
  }

  bool ok = deleteRecursively(path);
  server->send(ok ? 200 : 500, "application/json",
               ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

void WifiTransferManager::handleFileUploadPost() {
  String json;
  json.reserve(220);
  if (uploadError.length() > 0) {
    json = "{\"ok\":false,\"error\":\"" + escapeJson(uploadError) + "\"}";
    server->send(500, "application/json", json);
  } else {
    json = "{\"ok\":true,\"path\":\"" + escapeJson(uploadSavedPath) +
           "\",\"bytes\":" + String((unsigned long)uploadWrittenBytes) + "}";
    server->send(200, "application/json", json);
  }
}

void WifiTransferManager::handleFileUpload() {
  HTTPUpload &upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadError = "";
    uploadSavedPath = "";
    uploadWrittenBytes = 0;

    String dir = server->hasArg("dir") ? server->arg("dir") : "/";
    if (!isValidPath(dir)) {
      uploadError = "Invalid upload dir";
      return;
    }
    if (!createDirectories(dir)) {
      uploadError = "Cannot create dir";
      return;
    }

    String name = upload.filename;
    int p = name.lastIndexOf('/');
    if (p >= 0) {
      name = name.substring(p + 1);
    }
    p = name.lastIndexOf('\\');
    if (p >= 0) {
      name = name.substring(p + 1);
    }
    if (name.length() == 0) {
      uploadError = "Invalid filename";
      return;
    }

    String fullPath = (dir == "/") ? ("/" + name) : (dir + "/" + name);
    if (!isValidPath(fullPath)) {
      uploadError = "Invalid upload path";
      return;
    }

    uploadSavedPath = fullPath;
    uploadFile = SD_MMC.open(fullPath.c_str(), FILE_WRITE);
    if (!uploadFile) {
      uploadError = "Open upload file failed";
      return;
    }
  }

  if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadError.length() > 0) {
      return;
    }
    if (!uploadFile) {
      uploadError = "Upload stream invalid";
      return;
    }

    size_t written = uploadFile.write(upload.buf, upload.currentSize);
    if (written != upload.currentSize) {
      uploadError = "Write failed";
      return;
    }
    uploadWrittenBytes += written;
  }

  if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
  }

  if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadFile) {
      uploadFile.close();
    }
    if (uploadSavedPath.length() > 0) {
      SD_MMC.remove(uploadSavedPath.c_str());
    }
    uploadError = "Upload aborted";
  }
}

bool WifiTransferManager::createDirectories(const String &dirPath) {
  if (dirPath.length() == 0 || dirPath == "/") {
    return true;
  }

  if (!isValidPath(dirPath)) {
    return false;
  }

  String current;
  for (size_t i = 1; i < dirPath.length(); ++i) {
    if (dirPath[i] == '/') {
      current = dirPath.substring(0, i);
      if (current.length() > 0 && !SD_MMC.exists(current.c_str())) {
        if (!SD_MMC.mkdir(current.c_str())) {
          return false;
        }
      }
    }
  }

  if (!SD_MMC.exists(dirPath.c_str())) {
    if (!SD_MMC.mkdir(dirPath.c_str())) {
      return false;
    }
  }
  return true;
}

bool WifiTransferManager::deleteRecursively(const String &path) {
  File target = SD_MMC.open(path.c_str(), FILE_READ);
  if (!target) {
    return false;
  }

  if (!target.isDirectory()) {
    target.close();
    return SD_MMC.remove(path.c_str());
  }

  File entry = target.openNextFile();
  while (entry) {
    String childPath = String(entry.path());
    entry.close();

    if (!deleteRecursively(childPath)) {
      target.close();
      return false;
    }

    entry = target.openNextFile();
    yield();
  }
  target.close();
  return SD_MMC.rmdir(path.c_str());
}
String WifiTransferManager::listDirectory(const String &path, int page,
                                          int folderLimit) {
  File root = SD_MMC.open(path.c_str());
  if (!root || !root.isDirectory()) {
    if (root) {
      root.close();
    }
    return "{\"error\":\"Cannot open directory\"}";
  }

  if (page < 1) {
    page = 1;
  }
  if (folderLimit < 1) {
    folderLimit = DEFAULT_FOLDER_PAGE_SIZE;
  }

  const int skip = (page - 1) * folderLimit;
  int folderSeen = 0;
  int folderAdded = 0;
  int filesAdded = 0;
  bool hasMoreFolders = false;
  bool hasMoreFiles = false;

  String foldersJson;
  String filesJson;
  foldersJson.reserve(1024);
  filesJson.reserve(2048);

  File entry = root.openNextFile();
  int loopCount = 0;

  while (entry) {
    const bool isDir = entry.isDirectory();
    const String name = String(entry.name());
    const String entryPath = String(entry.path());

    if (isDir) {
      if (folderSeen >= skip && folderAdded < folderLimit) {
        if (folderAdded > 0) {
          foldersJson += ',';
        }
        foldersJson += "{\"name\":\"" + escapeJson(name) + "\",\"path\":\"" +
                       escapeJson(entryPath) + "\"}";
        folderAdded++;
      } else if (folderSeen >= skip + folderLimit) {
        hasMoreFolders = true;
      }
      folderSeen++;
    } else {
      if (filesAdded < MAX_FILES_PER_RESPONSE) {
        if (filesAdded > 0) {
          filesJson += ',';
        }
        filesJson += "{\"name\":\"" + escapeJson(name) + "\",\"path\":\"" +
                     escapeJson(entryPath) + "\",\"size\":" +
                     String((unsigned long)entry.size()) + "}";
        filesAdded++;
      } else {
        hasMoreFiles = true;
      }
    }

    entry.close();
    entry = root.openNextFile();

    loopCount++;
    if ((loopCount % 16) == 0) {
      yield();
    }
  }

  root.close();

  String parent = "/";
  if (path != "/") {
    int slashPos = path.lastIndexOf('/');
    if (slashPos > 0) {
      parent = path.substring(0, slashPos);
    }
  }

  String output;
  output.reserve(foldersJson.length() + filesJson.length() + 256);
  output += "{\"path\":\"" + escapeJson(path) + "\",";
  output += "\"parent\":\"" + escapeJson(parent) + "\",";
  output += "\"page\":" + String(page) + ",";
  output += "\"limit\":" + String(folderLimit) + ",";
  output += "\"hasMoreFolders\":" + String(hasMoreFolders ? "true" : "false") + ",";
  output += "\"hasMoreFiles\":" + String(hasMoreFiles ? "true" : "false") + ",";
  output += "\"folders\":[" + foldersJson + "],";
  output += "\"files\":[" + filesJson + "]}";

  return output;
}

bool WifiTransferManager::isValidPath(const String &path) {
  if (path.indexOf("..") != -1) {
    return false;
  }

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
  if (filename.endsWith(".html"))
    return "text/html";
  return "application/octet-stream";
}

String WifiTransferManager::escapeJson(const String &value) {
  String out;
  out.reserve(value.length() + 8);

  for (size_t i = 0; i < value.length(); ++i) {
    const char c = value[i];
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      if ((uint8_t)c < 0x20) {
        out += '?';
      } else {
        out += c;
      }
      break;
    }
  }

  return out;
}

bool WifiTransferManager::parseRangeHeader(const String &rangeHeader,
                                           size_t fileSize, size_t &start,
                                           size_t &end) {
  if (!rangeHeader.startsWith("bytes=")) {
    return false;
  }

  const String range = rangeHeader.substring(6);
  const int dashPos = range.indexOf('-');
  if (dashPos < 0) {
    return false;
  }

  const String startPart = range.substring(0, dashPos);
  const String endPart = range.substring(dashPos + 1);

  if (startPart.isEmpty() && endPart.isEmpty()) {
    return false;
  }

  if (startPart.isEmpty()) {
    unsigned long suffix = strtoul(endPart.c_str(), nullptr, 10);
    if (suffix == 0) {
      return false;
    }
    if (suffix >= fileSize) {
      start = 0;
    } else {
      start = fileSize - suffix;
    }
    end = fileSize - 1;
    return true;
  }

  unsigned long parsedStart = strtoul(startPart.c_str(), nullptr, 10);
  if (parsedStart >= fileSize) {
    return false;
  }
  start = parsedStart;

  if (endPart.isEmpty()) {
    end = fileSize - 1;
    return true;
  }

  unsigned long parsedEnd = strtoul(endPart.c_str(), nullptr, 10);
  if (parsedEnd < parsedStart) {
    return false;
  }

  end = (parsedEnd >= fileSize) ? (fileSize - 1) : parsedEnd;
  return true;
}








