#include "Renderer.h"

#include <HTTPClient.h>
#include <PNGdec.h>
#include <WiFi.h>
#include <pgmspace.h>

#include "Config.h"
#include "Display_ST7789.h"
#include "UI.h"
#include "boot_anim.h"
#include "esp_task_wdt.h"
#include "splash.h"

namespace {
PNG png;
uint16_t s_lineBuf[LINE_BUF_MAX];
uint16_t *s_decodeTarget = nullptr;

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
  LCD_addWindow(0, (uint16_t)y, LCD_WIDTH - 1, (uint16_t)y, s_lineBuf);
}

void pngStoreLine(PNGDRAW *pDraw) {
  if (!s_decodeTarget || pDraw->y < 0 || pDraw->y >= LCD_HEIGHT || pDraw->iWidth > LINE_BUF_MAX) {
    return;
  }
  png.getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  int w = pDraw->iWidth < LCD_WIDTH ? pDraw->iWidth : LCD_WIDTH;
  uint16_t *dst = s_decodeTarget + (pDraw->y * LCD_WIDTH);
  memcpy(dst, s_lineBuf, (size_t)w * sizeof(uint16_t));
  for (int x = w; x < LCD_WIDTH; x++) {
    dst[x] = 0;
  }
}

void redrawStatusDot() {
  StatusColor color = UI::currentStatusColor();
  if (color != StatusColor::Off) {
    UI::drawStatusDot(color);
  }
}

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t parseColor(const char *hex, uint16_t fallback) {
  if (!hex || !hex[0]) {
    return fallback;
  }
  if (hex[0] == '#') {
    hex++;
  }
  char *end = nullptr;
  uint32_t raw = strtoul(hex, &end, 16);
  if (end == hex) {
    return fallback;
  }
  return rgb565((raw >> 16) & 0xFF, (raw >> 8) & 0xFF, raw & 0xFF);
}

void serviceLongEffect() {
  esp_task_wdt_reset();
  yield();
}

void waitUntil(uint32_t start, uint32_t duration, int step, int totalSteps) {
  if (duration <= 200 || totalSteps <= 0) {
    serviceLongEffect();
    return;
  }
  uint32_t target = start + ((uint64_t)duration * (uint32_t)(step + 1) / (uint32_t)totalSteps);
  while ((int32_t)(millis() - target) < 0) {
    serviceLongEffect();
  }
}

void fillScreen565(uint16_t color) {
  for (uint16_t x = 0; x < LCD_WIDTH; x++) {
    s_lineBuf[x] = color;
  }
  for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
    LCD_addWindow(0, y, LCD_WIDTH - 1, y, s_lineBuf);
    if ((y & 0x0F) == 0) {
      serviceLongEffect();
    }
  }
}

bool decodePngRamWithCallback(uint8_t *pngData, int32_t len, void (*callback)(PNGDRAW *)) {
  if (!pngData || len <= 0 || !callback) {
    return false;
  }
  int rc = png.openRAM(pngData, len, callback);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG openRAM transition error: %d\n", rc);
    return false;
  }
  png.decode(NULL, 0);
  png.close();
  return true;
}

bool isShimmer(int x, int y, int frame) {
  int wave = ((x + y - frame * 4) % 480 + 480) % 480;
  return wave > 0 && wave < 40;
}

void bitmapSet(uint8_t *bits, uint32_t idx) {
  bits[idx >> 3] |= (uint8_t)(1U << (idx & 7));
}

bool bitmapGet(const uint8_t *bits, uint32_t idx) {
  return (bits[idx >> 3] & (uint8_t)(1U << (idx & 7))) != 0;
}

uint32_t s_effectStart = 0;
uint32_t s_effectDuration = 0;
uint16_t s_clipX = 0;
uint8_t *s_progressBits = nullptr;
uint16_t s_blockCols = 0;
uint16_t s_blockRows = 0;
uint8_t s_blockSize = 8;
int16_t *s_matrixHeads = nullptr;
uint8_t *s_matrixTails = nullptr;
uint16_t s_matrixColor = 0;

void drawLinePrefix(PNGDRAW *pDraw, uint16_t width) {
  if (pDraw->y < 0 || pDraw->y >= LCD_HEIGHT || width == 0 || pDraw->iWidth > LINE_BUF_MAX) {
    return;
  }
  png.getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  uint16_t w = min(width, (uint16_t)LCD_WIDTH);
  LCD_addWindow(0, (uint16_t)pDraw->y, w - 1, (uint16_t)pDraw->y, s_lineBuf);
}

void pngDrawLineWipeDown(PNGDRAW *pDraw) {
  pngDrawLine(pDraw);
  waitUntil(s_effectStart, s_effectDuration, pDraw->y, LCD_HEIGHT);
}

void pngDrawLineClipX(PNGDRAW *pDraw) {
  drawLinePrefix(pDraw, s_clipX);
}

void pngDrawLinePixelBits(PNGDRAW *pDraw) {
  if (!s_progressBits || pDraw->y < 0 || pDraw->y >= LCD_HEIGHT || pDraw->iWidth > LINE_BUF_MAX) {
    return;
  }
  png.getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  uint16_t y = (uint16_t)pDraw->y;
  uint16_t x = 0;
  while (x < LCD_WIDTH) {
    uint32_t idx = (uint32_t)y * LCD_WIDTH + x;
    while (x < LCD_WIDTH && !bitmapGet(s_progressBits, idx)) {
      x++;
      idx++;
    }
    uint16_t x0 = x;
    while (x < LCD_WIDTH && bitmapGet(s_progressBits, (uint32_t)y * LCD_WIDTH + x)) {
      x++;
    }
    if (x > x0) {
      LCD_addWindow(x0, y, x - 1, y, s_lineBuf + x0);
    }
  }
}

void pngDrawLineBlockBits(PNGDRAW *pDraw) {
  if (!s_progressBits || pDraw->y < 0 || pDraw->y >= LCD_HEIGHT || pDraw->iWidth > LINE_BUF_MAX) {
    return;
  }
  png.getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  uint16_t y = (uint16_t)pDraw->y;
  uint16_t blockY = y / s_blockSize;
  uint16_t x = 0;
  while (x < LCD_WIDTH) {
    uint16_t blockX = x / s_blockSize;
    uint32_t blockIdx = (uint32_t)blockY * s_blockCols + blockX;
    while (x < LCD_WIDTH && !bitmapGet(s_progressBits, blockIdx)) {
      x = (uint16_t)min((uint32_t)LCD_WIDTH, (uint32_t)(blockX + 1) * s_blockSize);
      blockX = x / s_blockSize;
      blockIdx = (uint32_t)blockY * s_blockCols + blockX;
    }
    uint16_t x0 = x;
    while (x < LCD_WIDTH && bitmapGet(s_progressBits, (uint32_t)blockY * s_blockCols + (x / s_blockSize))) {
      x = (uint16_t)min((uint32_t)LCD_WIDTH, (uint32_t)((x / s_blockSize) + 1) * s_blockSize);
    }
    if (x > x0) {
      LCD_addWindow(x0, y, x - 1, y, s_lineBuf + x0);
    }
  }
}

void pngDrawLineMatrix(PNGDRAW *pDraw) {
  if (!s_matrixHeads || !s_matrixTails || pDraw->y < 0 || pDraw->y >= LCD_HEIGHT || pDraw->iWidth > LINE_BUF_MAX) {
    return;
  }
  png.getLineAsRGB565(pDraw, s_lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  uint16_t y = (uint16_t)pDraw->y;
  uint16_t x = 0;
  while (x < LCD_WIDTH) {
    while (x < LCD_WIDTH) {
      int16_t reveal = s_matrixHeads[x] - s_matrixTails[x];
      if (reveal < 0) {
        reveal = 0;
      }
      if (y < (uint16_t)reveal) {
        break;
      }
      x++;
    }
    uint16_t x0 = x;
    while (x < LCD_WIDTH) {
      int16_t reveal = s_matrixHeads[x] - s_matrixTails[x];
      if (reveal < 0) {
        reveal = 0;
      }
      if (y >= (uint16_t)reveal) {
        break;
      }
      x++;
    }
    if (x > x0) {
      LCD_addWindow(x0, y, x - 1, y, s_lineBuf + x0);
    }
  }
}

void transitionFlash(uint8_t *pngData, int32_t len, JsonObject params) {
  uint32_t duration = params["duration_ms"] | 200;
  uint16_t color = parseColor(params["color"] | "#FFFFFF", rgb565(255, 255, 255));
  fillScreen565(color);
  uint32_t start = millis();
  while (millis() - start < duration) {
    serviceLongEffect();
  }
  Renderer::decodePngRamToDisplay(pngData, len);
}

void transitionWipeDown(uint8_t *pngData, int32_t len, JsonObject params) {
  s_effectDuration = params["duration_ms"] | 500;
  s_effectStart = millis();
  decodePngRamWithCallback(pngData, len, pngDrawLineWipeDown);
  redrawStatusDot();
}

void transitionWipeRight(uint8_t *pngData, int32_t len, JsonObject params) {
  uint32_t duration = params["duration_ms"] | 500;
  uint32_t start = millis();
  for (uint16_t x = 1; x <= LCD_WIDTH; x += 4) {
    s_clipX = min(x, (uint16_t)LCD_WIDTH);
    decodePngRamWithCallback(pngData, len, pngDrawLineClipX);
    waitUntil(start, duration, s_clipX, LCD_WIDTH);
  }
  Renderer::decodePngRamToDisplay(pngData, len);
  redrawStatusDot();
}

void transitionShimmer(uint8_t *pngData, int32_t len, JsonObject params) {
  uint32_t duration = params["duration_ms"] | 800;
  uint16_t color = parseColor(params["color"] | "#88CCFF", rgb565(136, 204, 255));
  int frames = max(1, (int)(duration / 33));
  uint32_t start = millis();
  Renderer::decodePngRamToDisplay(pngData, len);
  for (int step = 0; step < frames; step++) {
    for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
      int x = 0;
      while (x < LCD_WIDTH) {
        while (x < LCD_WIDTH && !isShimmer(x, y, step)) {
          x++;
        }
        int x0 = x;
        while (x < LCD_WIDTH && isShimmer(x, y, step)) {
          s_lineBuf[x - x0] = color;
          x++;
        }
        if (x > x0) {
          LCD_addWindow(x0, y, x - 1, y, s_lineBuf);
        }
      }
    }
    waitUntil(start, duration, step, frames);
  }
  Renderer::decodePngRamToDisplay(pngData, len);
}

void transitionDissolve(uint8_t *pngData, int32_t len, JsonObject params) {
  uint32_t duration = params["duration_ms"] | 800;
  const uint32_t pixelCount = (uint32_t)LCD_WIDTH * LCD_HEIGHT;
  const uint32_t bitBytes = (pixelCount + 7) / 8;
  uint8_t *bits = (uint8_t *)malloc(bitBytes);
  if (!bits) {
    Renderer::decodePngRamToDisplay(pngData, len);
    return;
  }
  memset(bits, 0, bitBytes);
  uint32_t revealed = 0;
  int steps = max(1, (int)(duration / 33));
  uint32_t start = millis();
  for (int step = 0; step < steps; step++) {
    uint32_t target = (uint64_t)pixelCount * (uint32_t)(step + 1) / (uint32_t)steps;
    if (step == steps - 1) {
      for (uint32_t idx = 0; idx < pixelCount; idx++) {
        if (!bitmapGet(bits, idx)) {
          bitmapSet(bits, idx);
          revealed++;
          if ((revealed & 0x7F) == 0) {
            serviceLongEffect();
          }
        }
      }
    }
    while (revealed < target) {
      uint32_t idx = (uint32_t)random(pixelCount);
      if (bitmapGet(bits, idx)) {
        continue;
      }
      bitmapSet(bits, idx);
      revealed++;
      if ((revealed & 0x7F) == 0) {
        serviceLongEffect();
      }
    }
    s_progressBits = bits;
    decodePngRamWithCallback(pngData, len, pngDrawLinePixelBits);
    waitUntil(start, duration, step, steps);
  }
  s_progressBits = nullptr;
  free(bits);
  Renderer::decodePngRamToDisplay(pngData, len);
}

void transitionPixelate(uint8_t *pngData, int32_t len, JsonObject params) {
  uint8_t blockSize = params["block_size"] | 8;
  if (blockSize == 0) {
    blockSize = 8;
  }
  uint32_t duration = params["duration_ms"] | 800;
  uint16_t cols = (LCD_WIDTH + blockSize - 1) / blockSize;
  uint16_t rows = (LCD_HEIGHT + blockSize - 1) / blockSize;
  uint16_t count = cols * rows;
  uint16_t bitBytes = (count + 7) / 8;
  uint16_t *order = (uint16_t *)malloc((size_t)count * sizeof(uint16_t));
  uint8_t *bits = (uint8_t *)malloc(bitBytes);
  if (!order || !bits) {
    free(order);
    free(bits);
    Renderer::decodePngRamToDisplay(pngData, len);
    return;
  }
  memset(bits, 0, bitBytes);
  for (uint16_t i = 0; i < count; i++) {
    order[i] = i;
  }
  for (uint16_t i = count - 1; i > 0; i--) {
    uint16_t j = random(i + 1);
    uint16_t tmp = order[i];
    order[i] = order[j];
    order[j] = tmp;
  }
  uint32_t start = millis();
  s_progressBits = bits;
  s_blockCols = cols;
  s_blockRows = rows;
  s_blockSize = blockSize;
  int steps = max(1, (int)(duration / 33));
  uint16_t revealed = 0;
  for (int step = 0; step < steps; step++) {
    uint16_t target = (uint32_t)count * (uint32_t)(step + 1) / (uint32_t)steps;
    while (revealed < target) {
      bitmapSet(bits, order[revealed]);
      revealed++;
    }
    decodePngRamWithCallback(pngData, len, pngDrawLineBlockBits);
    waitUntil(start, duration, step, steps);
  }
  s_progressBits = nullptr;
  free(order);
  free(bits);
  Renderer::decodePngRamToDisplay(pngData, len);
  redrawStatusDot();
}

void transitionMatrix(uint8_t *pngData, int32_t len, JsonObject params) {
  uint32_t duration = params["duration_ms"] | 900;
  uint16_t color = parseColor(params["color"] | "#00FF66", rgb565(0, 255, 102));
  int16_t *heads = (int16_t *)malloc((size_t)LCD_WIDTH * sizeof(int16_t));
  uint8_t *speeds = (uint8_t *)malloc((size_t)LCD_WIDTH);
  uint8_t *tails = (uint8_t *)malloc((size_t)LCD_WIDTH);
  if (!heads || !speeds || !tails) {
    free(heads);
    free(speeds);
    free(tails);
    Renderer::decodePngRamToDisplay(pngData, len);
    return;
  }
  for (uint16_t x = 0; x < LCD_WIDTH; x++) {
    heads[x] = -(int16_t)random(0, LCD_HEIGHT / 2);
    speeds[x] = random(3, 8);
    tails[x] = random(8, 24);
  }
  int steps = max(1, (int)(duration / 33));
  uint32_t start = millis();
  s_matrixHeads = heads;
  s_matrixTails = tails;
  s_matrixColor = color;
  for (int step = 0; step < steps; step++) {
    for (uint16_t x = 0; x < LCD_WIDTH; x++) {
      heads[x] += speeds[x];
    }
    decodePngRamWithCallback(pngData, len, pngDrawLineMatrix);
    for (uint16_t x = 0; x < LCD_WIDTH; x++) {
      if (heads[x] >= 0 && heads[x] < LCD_HEIGHT) {
        LCD_addWindow(x, (uint16_t)heads[x], x, (uint16_t)heads[x], &s_matrixColor);
      }
    }
    waitUntil(start, duration, step, steps);
  }
  s_matrixHeads = nullptr;
  s_matrixTails = nullptr;
  free(heads);
  free(speeds);
  free(tails);
  Renderer::decodePngRamToDisplay(pngData, len);
}

bool renderWithTransition(uint8_t *pngData, int32_t len, const char *transition, JsonObject params) {
  if (!transition || !transition[0] || strcmp(transition, "none") == 0) {
    return Renderer::decodePngRamToDisplay(pngData, len);
  }

  if (strcmp(transition, "flash") == 0) {
    transitionFlash(pngData, len, params);
  } else if (strcmp(transition, "wipe_down") == 0) {
    transitionWipeDown(pngData, len, params);
  } else if (strcmp(transition, "wipe_right") == 0) {
    transitionWipeRight(pngData, len, params);
  } else if (strcmp(transition, "shimmer") == 0) {
    transitionShimmer(pngData, len, params);
  } else if (strcmp(transition, "dissolve") == 0) {
    transitionDissolve(pngData, len, params);
  } else if (strcmp(transition, "pixelate") == 0) {
    transitionPixelate(pngData, len, params);
  } else if (strcmp(transition, "matrix") == 0) {
    transitionMatrix(pngData, len, params);
  } else {
    Renderer::decodePngRamToDisplay(pngData, len);
  }
  return true;
}
}

namespace Renderer {

void begin() {
}

void applyScreenBrightness(int screen_bri) {
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

bool decodePngRamToDisplay(uint8_t *pngData, int32_t len) {
  if (!pngData || len <= 0) {
    return false;
  }
  int rc = png.openRAM(pngData, len, pngDrawLine);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG openRAM error: %d\n", rc);
    return false;
  }
  png.decode(NULL, 0);
  png.close();
  redrawStatusDot();
  return true;
}

bool drawSplashFromProgmem() {
  UI::fillScreenBlack();
  int rc = png.openFLASH((uint8_t *)splash_png, sizeof(splash_png), pngDrawLine);
  if (rc != PNG_SUCCESS) {
    Serial.printf("Splash PNG openFLASH error: %d\n", rc);
    return false;
  }
  png.decode(NULL, 0);
  png.close();
  redrawStatusDot();
  return true;
}

bool downloadPngAndDraw(const String &url, const char *transition, JsonObject transitionParams) {
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
  int contentLength = http.getSize();
  if (contentLength <= 0) {
    Serial.println("Missing or invalid PNG Content-Length");
    http.end();
    return false;
  }
  if (contentLength > PNG_DOWNLOAD_MAX) {
    Serial.printf("PNG too large: %d\n", contentLength);
    http.end();
    return false;
  }

  uint8_t *pngData = (uint8_t *)malloc((size_t)contentLength);
  if (!pngData) {
    Serial.println("malloc PNG buffer failed");
    http.end();
    return false;
  }

  WiFiClient *stream = http.getStreamPtr();
  int totalRead = 0;
  uint32_t t0 = millis();
  while (totalRead < contentLength) {
    esp_task_wdt_reset();
    int available = stream->available();
    if (available > 0) {
      int remaining = contentLength - totalRead;
      int n = stream->read(pngData + totalRead, min(available, remaining));
      if (n > 0) {
        totalRead += n;
        t0 = millis();
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

  if (totalRead != contentLength) {
    Serial.printf("PNG read incomplete: %d / %d\n", totalRead, contentLength);
    free(pngData);
    return false;
  }

  int len = totalRead;

  bool ok = renderWithTransition(pngData, len, transition, transitionParams);
  free(pngData);
  return ok;
}

void playBootAnimation() {
  for (uint8_t i = 0; i < boot_anim_frame_count; i++) {
    const uint8_t *frame = (const uint8_t *)pgm_read_ptr(&boot_anim_frames[i]);
    size_t frameSize = pgm_read_dword(&boot_anim_frame_sizes[i]);
    if (!frame || frameSize == 0) {
      continue;
    }
    int rc = png.openFLASH((uint8_t *)frame, frameSize, pngDrawLine);
    if (rc == PNG_SUCCESS) {
      png.decode(NULL, 0);
      png.close();
    } else {
      Serial.printf("Boot frame PNG openFLASH error: %d\n", rc);
    }
    yield();
  }
}

}
