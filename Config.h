#pragma once

// Display
#define LCD_WIDTH         172
#define LCD_HEIGHT        320

// Pins
#define RGB_PIN           8
#define NUMPIXELS         1
#define BTN_RESET_PIN     9

// Timing
#define RESET_HOLD_MS     5000
#define WDT_TIMEOUT_MS    15000
#define HEARTBEAT_INTERVAL_MS  15000
#define HEARTBEAT_FAIL_MAX     3
#define RECONNECT_MAX          12

// Buffers
#define POST_BODY_MAX     4096
#define PNG_DOWNLOAD_MAX  (256 * 1024)
#define LINE_BUF_MAX      LCD_WIDTH

// LED semantic colors (RECOMMENDATION ONLY - not enforced in game commands)
#define LED_COLOR_BOOT       0x000064   // dark blue - initializing
#define LED_COLOR_CONNECTED  0x006400   // dark green - WiFi ok
#define LED_COLOR_WARN       0xFFAA00   // yellow - server unreachable (system reserved)
#define LED_COLOR_ERROR      0xFF0000   // red - WiFi lost (system reserved)
#define LED_COLOR_RECONNECT  0x000080   // blue - reconnecting
