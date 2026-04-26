#pragma once

#include <Arduino.h>

enum class StatusColor {
  Green,
  Red,
  Warn,
  Off
};

namespace UI {
void begin();
void fillScreenBlack();
void drawWaitingScreen(const String &miniId);
void drawStatusDot(StatusColor color, bool on = true);
StatusColor currentStatusColor();
void setStatusColor(StatusColor color);
}
