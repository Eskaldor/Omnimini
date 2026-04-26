#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace Renderer {
void begin();
void applyScreenBrightness(int screen_bri);
bool drawSplashFromProgmem();
bool decodePngRamToDisplay(uint8_t *pngData, int32_t len);
bool downloadPngAndDraw(const String &url, const char *transition, JsonObject transitionParams);
void playBootAnimation();
}
