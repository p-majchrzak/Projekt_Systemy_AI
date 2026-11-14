/*
  DZIUPLA VISION - IMG
  Board: Seeed XIAO ESP32-C3
  Wi-Fi: DZIUPLA-VISION
  Static IP: 192.168.4.10
  Web: http://192.168.4.10

  Browser prepares RGB888 240x240 (172800 bytes), then sends multipart
  form-data to SVR /api/image. SVR streams the raw RGB data to AI /api/classify.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include "ImgPage.h"

const char* WIFI_SSID = "DZIUPLA-VISION";
const char* WIFI_PASSWORD = "Dziupla2026";

const IPAddress IMG_IP(192, 168, 4, 10);
const IPAddress GATEWAY(192, 168, 4, 1);
const IPAddress SUBNET(255, 255, 255, 0);
const char* SVR_URL = "http://192.168.4.1";

WebServer web(80);
uint32_t lastHeartbeat = 0;
uint32_t heartbeatCount = 0;
String lastState = "Uruchamianie";
String lastRecipient = "-";
uint32_t imagesSent = 0;



void addCors() {
  web.sendHeader("Access-Control-Allow-Origin", "*");
  web.sendHeader("Cache-Control", "no-store");
}

void handleRoot() {
  web.send_P(200, "text/html; charset=utf-8", PAGE);
}

void handleStatus() {
  String json = "{\"device\":\"IMG\",\"status\":\"online\",\"ip\":\"" + WiFi.localIP().toString() +
    "\",\"wifi\":\"" + String(WiFi.status() == WL_CONNECTED ? "connected" : "disconnected") +
    "\",\"heartbeats\":" + String(heartbeatCount) + ",\"images_sent\":" + String(imagesSent) +
    ",\"last_state\":\"" + lastState + "\",\"last_recipient\":\"" + lastRecipient + "\"}";
  addCors();
  web.send(200, "application/json", json);
}

void handleNotFound() {
  web.send(404, "text/plain", "Not found");
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = String(SVR_URL) + "/api/heartbeat?device=IMG&message=IMG%20ready";
  http.begin(url);
  http.setTimeout(2500);
  int code = http.GET();
  if (code > 0 && code < 300) {
    heartbeatCount++;
    lastHeartbeat = millis();
    lastState = "Połączono z SVR";
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  delay(500);
  WiFi.persistent(false);
  WiFi.setSleep(false);
  WiFi.mode(WIFI_STA);
  WiFi.config(IMG_IP, GATEWAY, SUBNET, GATEWAY);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println();
  Serial.println("[IMG] Laczenie z DZIUPLA-VISION...");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  web.on("/", HTTP_GET, handleRoot);
  web.on("/api/status", HTTP_GET, handleStatus);
  web.onNotFound(handleNotFound);
  web.begin();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[IMG] Wi-Fi connected");
    Serial.print("[IMG] IP: "); Serial.println(WiFi.localIP());
    lastState = "Połączono z SVR";
    sendHeartbeat();
  } else {
    Serial.println("[IMG] Wi-Fi connection failed; retry in loop");
    lastState = "Brak połączenia Wi-Fi";
  }
  Serial.println("[IMG] Open: http://192.168.4.10");
}

void loop() {
  web.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t lastReconnect = 0;
    if (millis() - lastReconnect > 5000) {
      lastReconnect = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  } else if (millis() - lastHeartbeat >= 5000) {
    lastHeartbeat = millis();
    sendHeartbeat();
  }
  delay(2);
}
