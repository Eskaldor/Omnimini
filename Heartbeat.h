#pragma once

#include <Arduino.h>

class LedController;

namespace Heartbeat {
void begin(const String &deviceName, LedController *led);
void setServerUrl(const String &serverUrl);
void tick();
}
