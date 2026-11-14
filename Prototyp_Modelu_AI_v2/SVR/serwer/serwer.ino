/*
  DZIUPLA VISION - SVR
  Board: Seeed XIAO ESP32-C3
  Libraries: built-in WiFi.h and WebServer.h only.

  AP: DZIUPLA-VISION / Dziupla2026
  SVR: 192.168.4.1
  IMG: 192.168.4.10
  AI:  192.168.4.11
  MAIL:192.168.4.12

  Node heartbeat:
  GET http://192.168.4.1/api/heartbeat?device=IMG
  GET http://192.168.4.1/api/heartbeat?device=AI
  GET http://192.168.4.1/api/heartbeat?device=MAIL

  Node log:
  GET http://192.168.4.1/api/log?device=IMG&message=Image%20received
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <esp_system.h>
#include "DziuplaVisionPages.h"

const char* AP_SSID = "DZIUPLA-VISION";
const char* AP_PASSWORD = "Dziupla2026";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress AP_GATEWAY(192, 168, 4, 1);
const IPAddress AP_SUBNET(255, 255, 255, 0);
const uint32_t NODE_TIMEOUT_MS = 15000;
const uint8_t MAX_LOGS = 20;

// Panel login
const char* PANEL_LOGIN = "admin";
const char* PANEL_PASSWORD = "admin";
const uint32_t SESSION_DURATION_MS = 30UL * 60UL * 1000UL;
String sessionToken;
uint32_t sessionExpiresAt = 0;

WebServer server(80);

// Obraz jest przekazywany strumieniowo do AI - nie zapisujemy 172800 B w RAM SVR.
static const char* AI_HOST = "192.168.4.11";
static const uint16_t AI_PORT = 80;
static const size_t EXPECTED_IMAGE_BYTES = 240UL * 240UL * 3UL;
WiFiClient aiClient;
bool imageRelayConnected = false;
bool imageUploadValid = false;
size_t imageBytesReceived = 0;
String imageRecipient;
String imageUploadError;

struct NodeInfo {
  const char* id;
  const char* name;
  const char* fixedIp;
  uint32_t lastSeen;
  uint32_t heartbeatCount;
  String lastMessage;
};

NodeInfo nodes[] = {
  {"IMG", "Image Processor", "192.168.4.10", 0, 0, "Waiting for first heartbeat"},
  {"AI", "AI Classifier", "192.168.4.11", 0, 0, "Waiting for first heartbeat"},
  {"MAIL", "Mail Sender", "192.168.4.12", 0, 0, "Waiting for first heartbeat"}
};
const size_t NODE_COUNT = sizeof(nodes) / sizeof(nodes[0]);

struct LogEntry { String timeText; String device; String message; };
LogEntry logs[MAX_LOGS];
uint8_t logWriteIndex = 0;
uint8_t logCount = 0;
uint32_t requestCount = 0;

String jsonEscape(const String& input) {
  String out;
  out.reserve(input.length() + 8);
  for (size_t i = 0; i < input.length(); i++) {
    char c = input[i];
    if (c == '"') out += "\"";
    else if (c == '\\') out += "\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else if (c == '\t') out += "\\t";
    else if ((uint8_t)c >= 32) out += c;
  }
  return out;
}

int findNodeIndex(const String& id) {
  for (size_t i = 0; i < NODE_COUNT; i++) if (id.equalsIgnoreCase(nodes[i].id)) return (int)i;
  return -1;
}

String uptimeText() {
  uint32_t s = millis() / 1000UL;
  uint32_t d = s / 86400UL; s %= 86400UL;
  uint32_t h = s / 3600UL; s %= 3600UL;
  uint32_t m = s / 60UL; s %= 60UL;
  char b[40];
  snprintf(b, sizeof(b), "%lu d %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
  return String(b);
}

String elapsedText(uint32_t timestamp) {
  if (timestamp == 0) return "Never";
  return String((millis() - timestamp) / 1000UL) + " s ago";
}

void addLog(const String& device, const String& message) {
  logs[logWriteIndex].timeText = String(millis() / 1000UL) + "s";
  logs[logWriteIndex].device = device;
  logs[logWriteIndex].message = message;
  logWriteIndex = (logWriteIndex + 1) % MAX_LOGS;
  if (logCount < MAX_LOGS) logCount++;
  Serial.printf("[LOG] %s: %s\n", device.c_str(), message.c_str());
}

String makeStatusJson() {
  String j;
  j.reserve(1800);
  uint32_t now = millis();
  j += "{\"system\":\"Dziupla Vision\",\"role\":\"SVR\",\"status\":\"online\",\"ip\":\"";
  j += AP_IP.toString();
  j += "\",\"ssid\":\"" + String(AP_SSID) + "\",\"uptime\":\"" + uptimeText();
  j += "\",\"uptime_ms\":" + String(now);
  j += ",\"clients\":" + String(WiFi.softAPgetStationNum());
  j += ",\"requests\":" + String(requestCount) + ",\"nodes\":[";
  for (size_t i = 0; i < NODE_COUNT; i++) {
    if (i) j += ",";
    bool online = nodes[i].lastSeen != 0 && (now - nodes[i].lastSeen) <= NODE_TIMEOUT_MS;
    j += "{\"id\":\"" + String(nodes[i].id) + "\",\"name\":\"" + String(nodes[i].name);
    j += "\",\"ip\":\"" + String(nodes[i].fixedIp) + "\",\"online\":";
    j += online ? "true" : "false";
    j += ",\"last_seen\":\"" + elapsedText(nodes[i].lastSeen) + "\",\"heartbeats\":";
    j += String(nodes[i].heartbeatCount) + ",\"message\":\"" + jsonEscape(nodes[i].lastMessage) + "\"}";
  }
  j += "],\"logs\":[";
  for (uint8_t n = 0; n < logCount; n++) {
    int idx = (int(logWriteIndex) - 1 - n + MAX_LOGS) % MAX_LOGS;
    if (n) j += ",";
    j += "{\"time\":\"" + jsonEscape(logs[idx].timeText) + "\",\"device\":\"";
    j += jsonEscape(logs[idx].device) + "\",\"message\":\"" + jsonEscape(logs[idx].message) + "\"}";
  }
  j += "]}";
  return j;
}





bool isAuthenticated() {
  if (sessionToken.length() == 0) return false;
  if ((int32_t)(sessionExpiresAt - millis()) <= 0) return false;
  const String cookie = server.header("Cookie");
  const String expected = "DVSESSION=" + sessionToken;
  int pos = cookie.indexOf(expected);
  if (pos < 0) return false;
  int end = pos + expected.length();
  return end == cookie.length() || cookie[end] == ';' || cookie[end] == ' ';
}

void sendLoginPage() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", LOGIN_HTML);
}

void handleRoot() {
  requestCount++;
  if (!isAuthenticated()) { sendLoginPage(); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleLogin() {
  requestCount++;
  const String login = server.arg("login");
  const String password = server.arg("password");
  if (login == PANEL_LOGIN && password == PANEL_PASSWORD) {
    char token[17];
    snprintf(token, sizeof(token), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
    sessionToken = String(token);
    sessionExpiresAt = millis() + SESSION_DURATION_MS;
    server.sendHeader("Set-Cookie", "DVSESSION=" + sessionToken + "; Path=/; HttpOnly; SameSite=Strict");
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json", "{\"ok\":true}");
    addLog("SVR", "Administrator logged in");
  } else {
    server.sendHeader("Cache-Control", "no-store");
    server.send(401, "application/json", "{\"ok\":false,\"error\":\"Invalid credentials\"}");
    addLog("SVR", "Failed administrator login");
  }
}

void handleLogout() {
  requestCount++;
  sessionExpiresAt = 0;
  sessionToken = "";
  server.sendHeader("Set-Cookie", "DVSESSION=deleted; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", "{\"ok\":true}");
  addLog("SVR", "Administrator logged out");
}

void handleStatus() {
  requestCount++;
  if (!isAuthenticated()) {
    server.send(401, "application/json", "{\"ok\":false,\"error\":\"authentication required\"}");
    return;
  }
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", makeStatusJson());
}

void handleHeartbeat() {
  requestCount++;
  if (!server.hasArg("device")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"missing device\"}");
    return;
  }
  String device = server.arg("device");
  int idx = findNodeIndex(device);
  if (idx < 0) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"unknown device\"}");
    return;
  }
  nodes[idx].lastSeen = millis();
  nodes[idx].heartbeatCount++;
  nodes[idx].lastMessage = server.hasArg("message") ? server.arg("message") : "Heartbeat received";
  addLog(device, "Heartbeat received");
  server.send(200, "application/json", "{\"ok\":true,\"device\":\"" + String(nodes[idx].id) + "\",\"server\":\"SVR\"}");
}

void handleLog() {
  requestCount++;
  if (!server.hasArg("device") || !server.hasArg("message")) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"device and message required\"}");
    return;
  }
  String device = server.arg("device");
  String message = server.arg("message");
  int idx = findNodeIndex(device);
  if (idx < 0) {
    server.send(404, "application/json", "{\"ok\":false,\"error\":\"unknown device\"}");
    return;
  }
  nodes[idx].lastMessage = message;
  nodes[idx].lastSeen = millis();
  addLog(device, message);
  server.send(200, "application/json", "{\"ok\":true}");
}


String readJsonStringField(const String& body, const String& key) {
  String token = "\"" + key + "\"";
  int p = body.indexOf(token);
  if (p < 0) return "";
  p = body.indexOf(':', p + token.length());
  if (p < 0) return "";
  int q1 = body.indexOf('"', p + 1);
  if (q1 < 0) return "";
  int q2 = q1 + 1;
  bool escaped = false;
  while (q2 < (int)body.length()) {
    char c = body[q2];
    if (c == '"' && !escaped) break;
    if (c == '\\' && !escaped) escaped = true; else escaped = false;
    q2++;
  }
  if (q2 >= (int)body.length()) return "";
  String value = body.substring(q1 + 1, q2);
  value.replace("\\\"", "\"");
  value.replace("\\\\", "\\");
  return value;
}

void handleLogPost() {
  requestCount++;
  String body = server.arg("plain");
  if (body.length() == 0 || body.length() > 1024) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid log body\"}");
    return;
  }
  String device = readJsonStringField(body, "device");
  String message = readJsonStringField(body, "message");
  int idx = findNodeIndex(device);
  if (idx < 0 || message.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid device or message\"}");
    return;
  }
  nodes[idx].lastSeen = millis();
  nodes[idx].lastMessage = message;
  addLog(device, message);
  server.send(200, "application/json", "{\"ok\":true}");
}

void addCorsHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

void handleOptions() {
  addCorsHeaders();
  server.sendHeader("Access-Control-Max-Age", "600");
  server.send(204);
}

bool validEmailAddress(const String& email) {
  if (email.length() < 5 || email.length() > 100) return false;
  int at = email.indexOf('@');
  int dot = email.lastIndexOf('.');
  if (at < 1 || dot < at + 2 || dot >= (int)email.length() - 1) return false;
  for (size_t i = 0; i < email.length(); i++) {
    char c = email[i];
    if (c <= 32 || c == '"' || c == '\\' || c == '\r' || c == '\n') return false;
  }
  return true;
}

// Callback uploadu multipart/form-data. IMG wysyła pole "email" przed plikiem "image".
void handleImageUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    imageBytesReceived = 0;
    imageUploadValid = false;
    imageRelayConnected = false;
    imageUploadError = "";
    imageRecipient = server.arg("email");

    if (!validEmailAddress(imageRecipient)) {
      imageUploadError = "Nieprawidlowy adres e-mail";
      addLog("IMG", "Upload rejected: invalid recipient email");
      return;
    }

    if (upload.name != "image") {
      imageUploadError = "Brak pola pliku image";
      return;
    }

    aiClient.stop();
    aiClient.setTimeout(30000);
    if (!aiClient.connect(AI_HOST, AI_PORT)) {
      imageUploadError = "Modul AI jest niedostepny";
      addLog("SVR", "IMG upload rejected: AI offline");
      return;
    }

    String request =
      String("POST /api/classify HTTP/1.1\r\n") +
      "Host: " + AI_HOST + "\r\n" +
      "Content-Type: application/octet-stream\r\n" +
      "Content-Length: " + String(EXPECTED_IMAGE_BYTES) + "\r\n" +
      "X-Recipient-Email: " + imageRecipient + "\r\n" +
      "Connection: close\r\n\r\n";

    aiClient.print(request);
    imageRelayConnected = true;
    addLog("IMG", "Image transfer started; forwarding to AI");
  }
  else if (upload.status == UPLOAD_FILE_WRITE) {
    imageBytesReceived += upload.currentSize;
    if (imageBytesReceived > EXPECTED_IMAGE_BYTES) {
      imageUploadError = "Obraz ma za duzo danych";
      imageRelayConnected = false;
      aiClient.stop();
      return;
    }
    if (imageRelayConnected) {
      size_t sent = aiClient.write(upload.buf, upload.currentSize);
      if (sent != upload.currentSize) {
        imageUploadError = "Blad przekazywania danych do AI";
        imageRelayConnected = false;
        aiClient.stop();
      }
    }
  }
  else if (upload.status == UPLOAD_FILE_END) {
    if (imageBytesReceived != EXPECTED_IMAGE_BYTES) {
      imageUploadError = "Nieprawidlowy rozmiar RGB. Oczekiwano 172800 bajtow";
      imageRelayConnected = false;
      aiClient.stop();
    } else if (imageRelayConnected) {
      imageUploadValid = true;
    }
  }
  else if (upload.status == UPLOAD_FILE_ABORTED) {
    imageUploadError = "Przesylanie zostalo przerwane";
    imageRelayConnected = false;
    aiClient.stop();
  }
}

String readAiLine(uint32_t timeoutMs = 30000) {
  String line;
  uint32_t started = millis();
  while (millis() - started < timeoutMs) {
    while (aiClient.available()) {
      char c = (char)aiClient.read();
      if (c == '\n') {
        line.trim();
        return line;
      }
      if (c != '\r') line += c;
    }
    if (!aiClient.connected() && !aiClient.available()) break;
    delay(1);
  }
  return line;
}

void handleImageComplete() {
  requestCount++;
  addCorsHeaders();

  if (imageUploadError.length() || !imageUploadValid || !imageRelayConnected) {
    aiClient.stop();
    int code = imageUploadError.indexOf("niedostepny") >= 0 ? 503 : 400;
    String msg = imageUploadError.length() ? imageUploadError : "Nie udalo sie odebrac obrazu";
    server.send(code, "application/json; charset=utf-8",
      String("{\"ok\":false,\"error\":\"") + jsonEscape(msg) + "\"}");
    addLog("IMG", "Image upload failed: " + msg);
    return;
  }

  // AI powinno zwrocic zwykly HTTP/1.1 z Content-Length i odpowiedzia JSON.
  String statusLine = readAiLine(45000);
  if (!statusLine.startsWith("HTTP/")) {
    aiClient.stop();
    server.send(502, "application/json; charset=utf-8",
      "{\"ok\":false,\"error\":\"Brak poprawnej odpowiedzi HTTP z AI\"}");
    addLog("AI", "No valid HTTP response");
    return;
  }

  int aiStatus = 502;
  int firstSpace = statusLine.indexOf(' ');
  if (firstSpace > 0) aiStatus = statusLine.substring(firstSpace + 1, firstSpace + 4).toInt();

  int contentLength = -1;
  bool headersDone = false;
  uint32_t headerStart = millis();
  while (millis() - headerStart < 10000) {
    String line = readAiLine(10000);
    if (line.length() == 0) { headersDone = true; break; }
    String lower = line;
    lower.toLowerCase();
    if (lower.startsWith("content-length:")) contentLength = lower.substring(15).toInt();
  }

  String body;
  if (headersDone && contentLength >= 0 && contentLength < 4096) {
    body.reserve(contentLength + 1);
    uint32_t bodyStart = millis();
    while ((int)body.length() < contentLength && millis() - bodyStart < 30000) {
      while (aiClient.available() && (int)body.length() < contentLength) body += (char)aiClient.read();
      if (!aiClient.connected() && !aiClient.available()) break;
      delay(1);
    }
  }

  aiClient.stop();
  if (!headersDone || body.length() == 0) {
    server.send(502, "application/json; charset=utf-8",
      "{\"ok\":false,\"error\":\"AI nie zwrocilo odpowiedzi JSON\"}");
    addLog("AI", "Empty or incomplete response");
    return;
  }

  server.send(aiStatus, "application/json; charset=utf-8", body);
  addLog("AI", "Classification response: " + body.substring(0, 180));
}


void handleAIResult() {
  requestCount++;
  String body = server.arg("plain");
  if (body.length() == 0 || body.length() > 2048) {
    server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid result body\"}");
    return;
  }

  HTTPClient http;
  http.setTimeout(8000);
  if (!http.begin("http://192.168.4.12/api/task")) {
    server.send(503, "application/json", "{\"ok\":false,\"error\":\"cannot reach MAIL\"}");
    addLog("MAIL", "Could not open task connection");
    return;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code >= 200 && code < 300) {
    addLog("AI", "Classification result forwarded to MAIL");
    server.send(200, "application/json", "{\"ok\":true,\"mail_queued\":true}");
  } else {
    addLog("MAIL", "Task forwarding failed, HTTP " + String(code));
    server.send(502, "application/json",
      String("{\"ok\":false,\"error\":\"MAIL unavailable\",\"http\":") + String(code) + "}");
  }
}

void handleNotFound() {
  requestCount++;
  server.send(404, "application/json", "{\"ok\":false,\"error\":\"endpoint not found\",\"server\":\"Dziupla Vision SVR\"}");
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println();
  Serial.println("[SVR] Starting Dziupla Vision...");
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_AP);
  if (!WiFi.softAPConfig(AP_IP, AP_GATEWAY, AP_SUBNET)) Serial.println("[SVR] ERROR: softAPConfig failed");
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, 6, false, 8);
  if (!apStarted) Serial.println("[SVR] ERROR: Access Point start failed");
  delay(300);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", HTTP_POST, handleLogout);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/heartbeat", HTTP_GET, handleHeartbeat);
  server.on("/api/log", HTTP_GET, handleLog);
  server.on("/api/log", HTTP_POST, handleLogPost);
  server.on("/api/result", HTTP_POST, handleAIResult);
  server.on("/api/image", HTTP_OPTIONS, handleOptions);
  server.on("/api/image", HTTP_POST, handleImageComplete, handleImageUpload);
  server.onNotFound(handleNotFound);
  const char* headerKeys[] = {"Cookie"};
  server.collectHeaders(headerKeys, 1);
  server.begin();
  addLog("SVR", "Server started");
  addLog("SVR", "Access Point ready");
  addLog("SVR", "Waiting for IMG, AI and MAIL");
  Serial.println("========================================");
  Serial.println("       DZIUPLA VISION - SVR");
  Serial.println("========================================");
  Serial.printf("SSID: %s\n", AP_SSID);
  Serial.printf("Password: %s\n", AP_PASSWORD);
  Serial.print("Server IP: "); Serial.println(WiFi.softAPIP());
  Serial.printf("Dashboard: http://%s\n", WiFi.softAPIP().toString().c_str());
  Serial.println("========================================");
}

void loop() {
  server.handleClient();
  delay(2);
}
