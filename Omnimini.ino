#include <WiFi.h>
#include <HTTPClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <vector>
#include <PNGdec.h>
#include <pgmspace.h>
#include <WiFiManager.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include "esp_task_wdt.h"

#include "Display_ST7789.h"
#include "LedController.h"
#include "omnimini_bg.h"

#define RGB_PIN       8
#define NUMPIXELS     1
#define BTN_RESET_PIN 9
#define RESET_HOLD_MS 5000

#define POST_BODY_MAX 4096
#define PNG_DOWNLOAD_MAX (256 * 1024)
#define LINE_BUF_MAX 512

LedController ledController(NUMPIXELS, RGB_PIN, NEO_RGB + NEO_KHZ800);

Preferences preferences;
String device_name;
String mdns_hostname;
WiFiManager wm;
WebServer server(80);
PNG png;

static uint16_t s_lineBuf[LINE_BUF_MAX];

static void fillScreenBlack() {
  for (int i = 0; i < LCD_WIDTH; i++) {
    s_lineBuf[i] = 0;
  }
  for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
    // ИСПРАВЛЕНО: (x0=0, y0=y, x1=Width-1, y1=y)
    LCD_addWindow(0, y, LCD_WIDTH - 1, y, s_lineBuf);
  }
}

void pngDrawLine(PNGDRAW *pDraw) {
  int iw = pDraw->iWidth;
  if (iw > LINE_BUF_MAX) {
    return;
  }
  int y = pDraw->y;
  if (y < 0 || y >= LCD_HEIGHT) {
    return;
  }
  png.getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  int w = iw < LCD_WIDTH ? iw : LCD_WIDTH;
  for (int i = w; i < LCD_WIDTH; i++) {
    s_lineBuf[i] = 0;
  }
  // ИСПРАВЛЕНО: рисуем горизонтальную линию от 0 до LCD_WIDTH - 1 на высоте y
  LCD_addWindow(0, (uint16_t)y, LCD_WIDTH - 1, (uint16_t)y, s_lineBuf);
}

void * pngOpen(const char *filename, int32_t *size) { return NULL; }
void pngClose(void *handle) {}
void * pngRead(void *handle, uint8_t *buffer, int32_t length) { return NULL; }
void * pngSeek(void *handle, int32_t position) { return NULL; }

static bool decodePngRamToDisplay(uint8_t *png_data, int32_t len) {
  if (!png_data || len <= 0) {
    return false;
  }
  int rc = png.openRAM(png_data, len, pngDrawLine);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG openRAM error: %d\n", rc);
    return false;
  }
  png.decode(NULL, 0);
  png.close();
  return true;
}

static bool drawSplashFromProgmem() {
  fillScreenBlack();
  int rc = png.openFLASH((uint8_t *)splash_png, sizeof(splash_png), pngDrawLine);
  if (rc != PNG_SUCCESS) {
    Serial.printf("Splash PNG openFLASH error: %d\n", rc);
    return false;
  }
  png.decode(NULL, 0);
  png.close();
  return true;
}

static void applyScreenBrightness(int screen_bri) {
  if (screen_bri < 0) {
    screen_bri = 0;
  }
  if (screen_bri > 255) {
    screen_bri = 255;
  }
  uint8_t pct = (uint8_t)((screen_bri * 100 + 127) / 255);
  if (pct > 100) {
    pct = 100;
  }
  Set_Backlight(pct);
}

static bool readJsonBody(String &out) {
  if (server.hasArg("plain")) {
    out = server.arg("plain");
    return true;
  }
  
  // Иногда парсер не создает аргумент, но тело все равно можно достать так:
  out = server.arg("plain");
  if (out.length() > 0) {
    return true;
  }

  return false;
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

static bool downloadPngAndDraw(const String &url) {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return false;
  }
  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed: %s (%d)\n", http.errorToString(httpCode).c_str(), httpCode);
    http.end();
    return false;
  }
  int len = http.getSize();
  uint8_t *png_data = nullptr;

  if (len > 0 && len <= PNG_DOWNLOAD_MAX) {
    png_data = (uint8_t *)malloc((size_t)len);
    if (!png_data) {
      Serial.println("malloc PNG buffer failed");
      http.end();
      return false;
    }
    WiFiClient *stream = http.getStreamPtr();
    int totalRead = 0;
    uint32_t t0 = millis();
    while (totalRead < len) {
      if (stream->available()) {
        int n = stream->read(png_data + totalRead, len - totalRead);
        if (n > 0) {
          totalRead += n;
        }
      } else if (!stream->connected()) {
        break;
      } else if (millis() - t0 > 60000) {
        break;
      } else {
        yield();
      }
    }
    http.end();
    if (totalRead != len) {
      Serial.printf("PNG read incomplete: %d / %d\n", totalRead, len);
      free(png_data);
      return false;
    }
  } else if (len <= 0) {
    Serial.println("Missing or invalid Content-Length; trying getString()");
    String body = http.getString();
    http.end();
    len = body.length();
    if (len <= 0 || len > PNG_DOWNLOAD_MAX) {
      Serial.println("Body empty or too large");
      return false;
    }
    png_data = (uint8_t *)malloc((size_t)len);
    if (!png_data) {
      Serial.println("malloc PNG buffer failed");
      return false;
    }
    memcpy(png_data, body.c_str(), (size_t)len);
  } else {
    Serial.printf("PNG too large: %d\n", len);
    http.end();
    return false;
  }

  //fillScreenBlack();
  bool ok = decodePngRamToDisplay(png_data, len);
  free(png_data);
  return ok;
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
    applyScreenBrightness(doc["screen_bri"].as<int>());
  }

  if (doc.containsKey("led") && doc["led"].is<JsonObject>()) {
    parseLedFromJson(doc["led"].as<JsonObject>());
  }

  if (!doc["img_url"].isNull()) {
    const char *u = doc["img_url"].as<const char *>();
    if (u && u[0]) {
      downloadPngAndDraw(String(u));
    }
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
  Backlight_Init();
  Set_Backlight(100);

  ledController.begin();
  ledController.updateLedFromConfig("static", {0x000064}, 0, 255);

  drawSplashFromProgmem();

  preferences.begin("omniboard", false);
  String saved_id = preferences.getString("mini_id", "1");
  device_name = "omnimini_" + saved_id;
  mdns_hostname = "omnimini-" + saved_id;

  static WiFiManagerParameter custom_id("mini_id", "Номер (1-99)", saved_id.c_str(), 3, "type=\"number\" min=\"1\" max=\"99\"");
  wm.addParameter(&custom_id);
  wm.setSaveConfigCallback([]() {
    String new_id = custom_id.getValue();
    preferences.putString("mini_id", new_id);
    device_name = "omnimini_" + new_id;
    mdns_hostname = "omnimini-" + new_id;
  });

  WiFi.mode(WIFI_STA);
  if (!wm.autoConnect("Omniboard-Mini")) {
    Serial.println("WiFi config failed, restarting...");
    waitMillisNonBlocking(3000);
    ESP.restart();
  }

  ledController.updateLedFromConfig("static", {0x006400}, 0, 255);

  if (!MDNS.begin(mdns_hostname.c_str())) {
    Serial.println("mDNS begin failed");
  } else {
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/update", HTTP_POST, handleUpdate);
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

  ledController.tick();
  esp_task_wdt_reset();
}
