#include <WebServer.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <vector>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "esp_task_wdt.h"

#include "Config.h"
#include "Display_ST7789.h"
#include "Heartbeat.h"
#include "LedController.h"
#include "Renderer.h"
#include "UI.h"

LedController ledController(NUMPIXELS, RGB_PIN, NEO_RGB + NEO_KHZ800);

Preferences preferences;
String device_name;
String mdns_hostname;
String server_url;
WiFiManager wm;
WebServer server(80);

static const char PORTAL_HEAD[] PROGMEM = R"rawliteral(
<style>
body{background:#0d0d0d;color:#e0e0e0;font-family:system-ui,-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;}
.wrap{background:#171717;border:1px solid #262626;border-radius:16px;padding:18px;}
h1,h2,h3,label{color:#e0e0e0;}
input{background:#101010!important;color:#e0e0e0!important;border:1px solid #333!important;border-radius:8px!important;}
button,.button,input[type=submit]{background:#4f98a3!important;color:#fff!important;border:0!important;border-radius:8px!important;}
a{color:#4f98a3;}
.omni-logo{display:block;margin:20px auto 8px;max-width:220px;}
</style>
<svg class="omni-logo" viewBox="0 0 320 72" xmlns="http://www.w3.org/2000/svg" role="img" aria-label="OMNIMINI">
<rect width="320" height="72" rx="18" fill="#121212" stroke="#4f98a3" stroke-width="3"/>
<text x="160" y="46" text-anchor="middle" font-family="system-ui, sans-serif" font-size="30" font-weight="700" fill="#e0e0e0">OMNIMINI</text>
</svg>
<script>document.title='Omnimini Setup';</script>
)rawliteral";

static bool readJsonBody(String &out) {
  out = server.arg("plain");
  return server.hasArg("plain");
}

static void parseLedFromJson(JsonObject led) {
  String mode = led["mode"] | "static";
  std::vector<uint32_t> colors;
  if (led["colors"].is<JsonArray>()) {
    for (JsonVariant v : led["colors"].as<JsonArray>()) {
      if (v.is<const char *>()) {
        const char *s = v.as<const char *>();
        colors.push_back((uint32_t)strtoul(s + (s[0] == '#' ? 1 : 0), NULL, 16));
      }
    }
  }
  if (colors.empty()) {
    colors.push_back(0x008000);
  }
  int speed = led["speed"] | 500;
  int brightness = led["brightness"] | 255;
  ledController.updateLedFromConfig(mode, colors, speed, brightness);
}

static void handleUpdate() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"status\":\"error\",\"msg\":\"method\"}");
    return;
  }
  String body;
  if (!readJsonBody(body)) {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"body\"}");
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"json\"}");
    return;
  }

  if (doc.containsKey("screen_bri")) {
    Renderer::applyScreenBrightness(doc["screen_bri"].as<int>());
  }

  if (doc.containsKey("led") && doc["led"].is<JsonObject>()) {
    parseLedFromJson(doc["led"].as<JsonObject>());
  }

  if (!doc["img_url"].isNull()) {
    const char *u = doc["img_url"].as<const char *>();
    if (u && u[0]) {
      const char *transition = doc["transition"] | "none";
      JsonObject params;
      if (doc["transition_params"].is<JsonObject>()) {
        params = doc["transition_params"].as<JsonObject>();
      }
      Renderer::downloadPngAndDraw(String(u), transition, params);
    }
  }

  server.send(200, "application/json", "{\"status\":\"success\"}");
}

static void handleFrame() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"status\":\"error\",\"msg\":\"method\"}");
    return;
  }
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"body\"}");
    return;
  }

  String body = server.arg("plain");
  bool ok = Renderer::decodePngRamToDisplay((uint8_t *)body.c_str(), body.length());
  if (!ok) {
    server.send(400, "application/json", "{\"status\":\"error\",\"msg\":\"png\"}");
    return;
  }
  server.send(200, "application/json", "{\"status\":\"success\"}");
}

static void waitMillisNonBlocking(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    yield();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(BTN_RESET_PIN, INPUT_PULLUP);

  LCD_Init();
  Set_Backlight(100);
  UI::begin();
  Renderer::begin();

  ledController.begin();
  ledController.updateLedFromConfig("static", {LED_COLOR_BOOT}, 0, 255);

  Renderer::playBootAnimation();
  Renderer::drawSplashFromProgmem();

  preferences.begin("omniboard", false);
  String saved_id = preferences.getString("mini_id", "1");
  server_url = preferences.getString("server_url", "");
  device_name = "omnimini_" + saved_id;
  mdns_hostname = "omnimini-" + saved_id;

  static WiFiManagerParameter custom_id("mini_id", "Номер (1-99)", saved_id.c_str(), 3, "type=\"number\" min=\"1\" max=\"99\"");
  static WiFiManagerParameter custom_server_url("server_url", "Server URL", server_url.c_str(), 96, "placeholder=\"http://192.168.1.10:8080\"");
  wm.setCustomHeadElement(PORTAL_HEAD);
  wm.addParameter(&custom_id);
  wm.addParameter(&custom_server_url);
  wm.setSaveConfigCallback([]() {
    String new_id = custom_id.getValue();
    String new_server_url = custom_server_url.getValue();
    preferences.putString("mini_id", new_id);
    preferences.putString("server_url", new_server_url);
    device_name = "omnimini_" + new_id;
    mdns_hostname = "omnimini-" + new_id;
    server_url = new_server_url;
    Heartbeat::setServerUrl(server_url);
  });

  WiFi.mode(WIFI_STA);
  if (!wm.autoConnect("Omniboard-Mini")) {
    Serial.println("WiFi config failed, restarting...");
    waitMillisNonBlocking(3000);
    ESP.restart();
  }

  UI::setStatusColor(StatusColor::Green);
  UI::drawWaitingScreen(saved_id);
  ledController.updateLedFromConfig("static", {LED_COLOR_CONNECTED}, 0, 255);
  Heartbeat::begin(device_name, &ledController);
  Heartbeat::setServerUrl(server_url);

  if (!MDNS.begin(mdns_hostname.c_str())) {
    Serial.println("mDNS begin failed");
  } else {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/frame", HTTP_POST, handleFrame);
  server.begin();

  esp_task_wdt_config_t wdt_config = {};
  wdt_config.timeout_ms = 15000;
  wdt_config.idle_core_mask = 0;
  wdt_config.trigger_panic = true;
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  ArduinoOTA.setHostname(mdns_hostname.c_str());
  ArduinoOTA.begin();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  ledController.tick();
  Heartbeat::tick();

  static uint32_t btnPressedAt = 0;
  if (digitalRead(BTN_RESET_PIN) == LOW) {
    if (btnPressedAt == 0) {
      btnPressedAt = millis();
    } else if ((millis() - btnPressedAt) >= (uint32_t)RESET_HOLD_MS) {
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    btnPressedAt = 0;
  }

  esp_task_wdt_reset();
}
