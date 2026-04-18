#ifndef LED_CONTROLLER_H
#define LED_CONTROLLER_H

#include <Adafruit_NeoPixel.h>
#include <vector>

#define LED_MAX_COLORS 8

class LedController {
public:
    enum State {
        Static,
        Cycle,
        Blink,
        Breathe,
        Pulse,
        Rainbow
    };

    LedController(uint16_t numPixels, uint16_t pin, neoPixelType type = NEO_RGB + NEO_KHZ800);
    void begin();
    void tick();  // неблокирующее обновление по millis()

    void updateLedFromConfig(const String& mode, const std::vector<uint32_t>& colors, int speed, int brightness);

private:
    Adafruit_NeoPixel strip_;
    State state_;
    uint32_t colors_[LED_MAX_COLORS];
    uint8_t numColors_;
    int speed_;       // период/скорость в мс
    int brightness_; // 0..255

    uint8_t currentColorIdx_;

    State modeFromString(const String& mode) const;
    uint32_t colorWheel(uint8_t pos) const;
    void applyColorToStrip(uint32_t color, uint8_t bri);
};

#endif
