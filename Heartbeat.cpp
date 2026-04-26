#include "Heartbeat.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>

#include "Config.h"
#include "LedController.h"
#include "UI.h"

namespace {
String s_deviceName;
String s_serverUrl;
LedController *s_ledController = nullptr;
uint32_t s_lastTick = 0;
uint32_t s_lastReconnectAttempt = 0;
uint8_t s_failCount = 0;
uint8_t s_reconnectAttempts = 0;
bool s_wasDown = false;

String heartbeatUrl() {
  if (s_serverUrl.length() == 0) {
    return String();
  }
  if (s_serverUrl.endsWith("/")) {
    return s_serverUrl + "heartbeat";
  }
  return s_serverUrl + "/heartbeat";
}

void setSystemLed(const char *mode, uint32_t color, int speed, int brightness) {
  if (s_ledController) {
    s_ledController->updateLedFromConfig(mode, {color}, speed, brightness);
  }
}

void setWarnState() {
  UI::setStatusColor(StatusColor::Warn);
  setSystemLed("breathe", LED_COLOR_WARN, 1200, 180);
}

void setErrorState() {
  UI::setStatusColor(StatusColor::Red);
  setSystemLed("blink", LED_COLOR_ERROR, 500, 255);
}

void setOkState() {
  UI::setStatusColor(StatusColor::Green);
  setSystemLed("static", LED_COLOR_CONNECTED, 0, 255);
}

bool postHeartbeat() {
  String url = heartbeatUrl();
  if (url.length() == 0) {
    return true;
  }

  JsonDocument doc;
  doc["id"] = s_deviceName;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;

  String body;
  serializeJson(doc, body);

  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(body);
  http.end();
  return code >= 200 && code < 300;
}
}

namespace Heartbeat {

void begin(const String &deviceName, LedController *led) {
  s_deviceName = deviceName;
  s_ledController = led;
  Preferences prefs;
  prefs.begin("omniboard", true);
  s_serverUrl = prefs.getString("server_url", "");
  prefs.end();
  s_lastTick = millis() - HEARTBEAT_INTERVAL_MS;
}

void setServerUrl(const String &serverUrl) {
  s_serverUrl = serverUrl;
}

void tick() {
  uint32_t now = millis();

  if (WiFi.status() != WL_CONNECTED) {
    s_wasDown = true;
    s_failCount = HEARTBEAT_FAIL_MAX;
    setErrorState();

    if (now - s_lastReconnectAttempt >= 1000) {
      s_lastReconnectAttempt = now;
      s_reconnectAttempts++;
      WiFi.reconnect();
      if (s_reconnectAttempts >= RECONNECT_MAX) {
        ESP.restart();
      }
    }
    bool blinkOn = ((now / 500) & 1) == 0;
    UI::drawStatusDot(StatusColor::Red, blinkOn);
    return;
  }

  s_reconnectAttempts = 0;

  if (now - s_lastTick < HEARTBEAT_INTERVAL_MS) {
    return;
  }
  s_lastTick = now;

  if (postHeartbeat()) {
    bool recovered = s_wasDown || s_failCount >= HEARTBEAT_FAIL_MAX;
    s_failCount = 0;
    s_wasDown = false;
    if (recovered) {
      setOkState();
    }
    return;
  }

  if (s_failCount < 255) {
    s_failCount++;
  }
  if (s_failCount >= HEARTBEAT_FAIL_MAX) {
    s_wasDown = true;
    setWarnState();
  }
}

}
