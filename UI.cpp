#include "UI.h"

#include "Config.h"
#include "Display_ST7789.h"

#if defined(__has_include)
#if __has_include(<TFT_eSPI.h>)
#include <TFT_eSPI.h>
#define OMNIMINI_HAS_TFT_ESPI 1
#else
#define OMNIMINI_HAS_TFT_ESPI 0
#endif
#else
#define OMNIMINI_HAS_TFT_ESPI 0
#endif

namespace {
uint16_t s_lineBuf[LINE_BUF_MAX];
StatusColor s_statusColor = StatusColor::Off;
#if OMNIMINI_HAS_TFT_ESPI
TFT_eSPI tft = TFT_eSPI();
#endif

uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

uint16_t statusColorTo565(StatusColor color) {
  switch (color) {
    case StatusColor::Green:
      return rgb565(0, 255, 0);
    case StatusColor::Red:
      return rgb565(255, 0, 0);
    case StatusColor::Warn:
      return rgb565(255, 170, 0);
    case StatusColor::Off:
    default:
      return 0;
  }
}
}

namespace UI {

void begin() {
#if OMNIMINI_HAS_TFT_ESPI
  tft.init();
  tft.setRotation(0);
#endif
}

void fillScreenBlack() {
  for (int i = 0; i < LCD_WIDTH; i++) {
    s_lineBuf[i] = 0;
  }
  for (uint16_t y = 0; y < LCD_HEIGHT; y++) {
    LCD_addWindow(0, y, LCD_WIDTH - 1, y, s_lineBuf);
  }
}

void drawWaitingScreen(const String &miniId) {
#if OMNIMINI_HAS_TFT_ESPI
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(MC_DATUM);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextFont(4);
  tft.drawString("OMNIMINI", LCD_WIDTH / 2, 78);

  tft.setTextFont(8);
  tft.drawString(miniId, LCD_WIDTH / 2, LCD_HEIGHT / 2);

  tft.setTextColor(tft.color565(120, 120, 120), TFT_BLACK);
  tft.setTextFont(2);
  tft.drawString("waiting...", LCD_WIDTH / 2, LCD_HEIGHT - 44);
  drawStatusDot(s_statusColor == StatusColor::Off ? StatusColor::Green : s_statusColor);
#else
  fillScreenBlack();
  (void)miniId;
  drawStatusDot(s_statusColor == StatusColor::Off ? StatusColor::Green : s_statusColor);
#endif
}

void drawStatusDot(StatusColor color, bool on) {
  s_statusColor = color;
  uint16_t c = on ? statusColorTo565(color) : 0;
#if OMNIMINI_HAS_TFT_ESPI
  tft.fillRect(LCD_WIDTH - 5, 1, 4, 4, c);
#else
  uint16_t pixels[16];
  for (uint8_t i = 0; i < 16; i++) {
    pixels[i] = c;
  }
  LCD_addWindow(LCD_WIDTH - 5, 1, LCD_WIDTH - 2, 4, pixels);
#endif
}

StatusColor currentStatusColor() {
  return s_statusColor;
}

void setStatusColor(StatusColor color) {
  s_statusColor = color;
  drawStatusDot(color);
}

}
