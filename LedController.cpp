#include "LedController.h"

LedController::LedController(uint16_t numPixels, uint16_t pin, neoPixelType type)
    : strip_(numPixels, pin, type),
      state_(Static),
      numColors_(1),
      speed_(500),
      brightness_(255),
      currentColorIdx_(0) {
    colors_[0] = 0x008000; // зелёный по умолчанию
}

void LedController::begin() {
    strip_.begin();
    strip_.show();
}

LedController::State LedController::modeFromString(const String& mode) const {
    if (mode.equalsIgnoreCase("cycle")) return Cycle;
    if (mode.equalsIgnoreCase("blink")) return Blink;
    if (mode.equalsIgnoreCase("breathe")) return Breathe;
    if (mode.equalsIgnoreCase("pulse")) return Pulse;
    if (mode.equalsIgnoreCase("rainbow")) return Rainbow;
    return Static;
}

void LedController::updateLedFromConfig(const String& mode, const std::vector<uint32_t>& colors, int speed, int brightness) {
    state_ = modeFromString(mode);
    speed_ = (speed > 0) ? speed : 500;
    brightness_ = brightness;
    if (brightness_ < 0) brightness_ = 0;
    if (brightness_ > 255) brightness_ = 255;

    numColors_ = 0;
    for (size_t i = 0; i < colors.size() && i < LED_MAX_COLORS; i++) {
        colors_[i] = colors[i];
        numColors_++;
    }
    if (numColors_ == 0) {
        colors_[0] = 0x008000;
        numColors_ = 1;
    }

    currentColorIdx_ = 0;
}

uint32_t LedController::colorWheel(uint8_t pos) const {
    if (pos < 85) {
        return strip_.Color(255 - pos * 3, pos * 3, 0);
    }
    if (pos < 170) {
        pos -= 85;
        return strip_.Color(0, 255 - pos * 3, pos * 3);
    }
    pos -= 170;
    return strip_.Color(pos * 3, 0, 255 - pos * 3);
}

void LedController::applyColorToStrip(uint32_t color, uint8_t bri) {
    strip_.setBrightness(bri);
    strip_.setPixelColor(0, color);
    strip_.show();
}

void LedController::tick() {
    const uint32_t now = millis();

    switch (state_) {
        case Static:
            applyColorToStrip(colors_[0], (uint8_t)brightness_);
            break;

        case Cycle: {
            uint32_t period = (uint32_t)speed_;
            if (period < 100) {
                period = 100;
            }
            uint8_t colorIdx = (uint8_t)((now / period) % numColors_);
            applyColorToStrip(colors_[colorIdx], (uint8_t)brightness_);
            break;
        }

        case Blink: {
            uint32_t period = (uint32_t)speed_;
            if (period < 100) {
                period = 100;
            }
            uint8_t colorIdx = (uint8_t)((now / period) % numColors_);
            uint32_t localPhase = now % period;
            uint8_t bri = (localPhase < (period / 2)) ? (uint8_t)brightness_ : 0;
            applyColorToStrip(colors_[colorIdx], bri);
            break;
        }

        case Breathe: {
            uint32_t period = (uint32_t)speed_;
            if (period < 100) {
                period = 100;
            }
            uint8_t colorIdx = (uint8_t)((now / period) % numColors_);
            uint32_t localPhase = now % period;
            uint32_t half = period / 2;
            uint8_t bri;
            if (half == 0) {
                bri = (uint8_t)brightness_;
            } else if (localPhase < half) {
                bri = (uint8_t)((uint32_t)brightness_ * localPhase / half);
            } else {
                bri = (uint8_t)((uint32_t)brightness_ * (period - localPhase) / half);
            }
            applyColorToStrip(colors_[colorIdx], bri);
            break;
        }

        case Pulse: {
            uint32_t period = (uint32_t)speed_;
            if (period < 100) {
                period = 100;
            }
            uint8_t colorIdx = (uint8_t)((now / period) % numColors_);
            uint32_t localPhase = now % period;
            uint8_t bri = 0;
            if (localPhase < 100) {
                bri = (uint8_t)brightness_;
            } else if (localPhase < 200) {
                bri = 0;
            } else if (localPhase < 300) {
                bri = (uint8_t)brightness_;
            } else {
                uint32_t tail = period - 300;
                if (tail > 0) {
                    bri = (uint8_t)((uint32_t)brightness_ - (uint32_t)brightness_ * (localPhase - 300) / tail);
                }
            }
            applyColorToStrip(colors_[colorIdx], bri);
            break;
        }

        case Rainbow: {
            uint32_t period = (uint32_t)speed_;
            if (period < 200) {
                period = 200;
            }
            uint8_t hue = (uint8_t)((now * 256 / period) & 0xFF);
            uint32_t c = colorWheel(hue);
            applyColorToStrip(c, (uint8_t)brightness_);
            break;
        }
    }
}
