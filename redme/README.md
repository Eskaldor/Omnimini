# Omnimini Firmware 1.2

Omnimini is an ESP32-C6 based WiFi-controlled tabletop RPG miniature display. The firmware controls a 172x320 ST7789 display and one WS2812B RGB LED, receives commands over HTTP, downloads or accepts PNG frames, and reports health to a server.

## Hardware Target

- Board: Waveshare ESP32-C6-LCD-1.47 or compatible ESP32-C6 board
- Display: ST7789, 172x320
- LED: one WS2812B / NeoPixel on pin 8
- Reset button: pin 9, hold for 5 seconds to reset WiFiManager settings

Important constants are centralized in `Config.h`.

## Main Features

- WiFiManager captive portal for first setup
- Custom dark Omnimini setup portal
- Device number saved as `mini_id`
- Server URL saved as `server_url`
- OTA support through ArduinoOTA
- PNG rendering from URL through `/update`
- Raw PNG frame upload through `/frame`
- RGB LED control from JSON
- Waiting screen after WiFi connects
- Status dot in the top-right corner
- Heartbeat POST to the server every 15 seconds
- Connection watchdog with WARN / ERROR states
- Optional transition effects for image updates
- Boot animation infrastructure through `boot_anim.h`

## Network Setup

On first boot, or after holding the reset button for 5 seconds, Omnimini opens a WiFiManager captive portal.

Portal fields:

- `mini_id`: miniature number, for example `7`
- `server_url`: base server URL, for example `http://192.168.1.10:8080`

After setup:

- device id becomes `omnimini_<mini_id>`, for example `omnimini_7`
- mDNS hostname becomes `omnimini-<mini_id>`, for example `omnimini-7`
- HTTP server listens on port `80`

Example local URLs:

```text
http://omnimini-7.local/update
http://omnimini-7.local/frame
```

If mDNS is not available on the network, use the IP address shown by your router.

## Boot Flow

1. Initialize display and backlight.
2. Initialize LED and set boot color.
3. Play boot animation from `boot_anim.h`.
4. Draw splash image from `splash.h`.
5. Start WiFiManager if needed.
6. After WiFi connects, draw waiting screen:

```text
OMNIMINI

7

waiting...
```

7. Start HTTP server, OTA, heartbeat, and main loop.

## Status Dot

A 4x4 pixel status dot is drawn in the top-right corner.

- Green: WiFi and heartbeat OK
- Yellow: server heartbeat failed 3 times
- Red blinking: WiFi lost

The dot is redrawn after full image updates.

## LED Behavior

The LED can be controlled by game commands through `/update`, but some colors are reserved by convention for system states:

- `LED_COLOR_BOOT`: dark blue, initializing
- `LED_COLOR_CONNECTED`: dark green, WiFi OK
- `LED_COLOR_WARN`: yellow, server unreachable
- `LED_COLOR_ERROR`: red, WiFi lost
- `LED_COLOR_RECONNECT`: blue, reconnecting

Game commands may still send any color. These semantic colors are recommendations, not enforced restrictions.

## `/update` Endpoint

Use `/update` to send a JSON command. It can update screen brightness, LED mode, image URL, and transition effect.

Method:

```text
POST /update
Content-Type: application/json
```

Success response:

```json
{"status":"success"}
```

Error responses:

```json
{"status":"error","msg":"method"}
{"status":"error","msg":"body"}
{"status":"error","msg":"json"}
```

### Minimal Image Update

```json
{
  "img_url": "http://192.168.1.10:8080/images/character.png"
}
```

If `transition` is absent, behavior is the same as `"none"`: the new PNG is drawn immediately.

### Full Update Example

```json
{
  "screen_bri": 200,
  "led": {
    "mode": "breathe",
    "colors": ["#0000FF"],
    "speed": 1000,
    "brightness": 200
  },
  "img_url": "http://192.168.1.10:8080/images/character.png",
  "transition": "shimmer",
  "transition_params": {
    "color": "#88CCFF",
    "duration_ms": 800
  }
}
```

### `screen_bri`

Display brightness value from `0` to `255`.

Internally it is mapped to backlight percentage `0` to `100`.

```json
{
  "screen_bri": 128
}
```

### `led`

LED command format:

```json
{
  "led": {
    "mode": "static",
    "colors": ["#00FF00"],
    "speed": 500,
    "brightness": 255
  }
}
```

Supported modes:

- `static`: constant color
- `cycle`: cycle through colors
- `blink`: blink color on/off
- `breathe`: smooth brightness breathing
- `pulse`: short pulse pattern
- `rainbow`: hue wheel animation

Fields:

- `colors`: array of hex RGB strings, for example `"#FFAA00"`
- `speed`: mode period in milliseconds
- `brightness`: `0` to `255`

If `colors` is empty or absent, the default color is green.

## Transition Effects

The `/update` endpoint accepts:

```json
{
  "img_url": "http://server/next.png",
  "transition": "flash",
  "transition_params": {
    "duration_ms": 300
  }
}
```

If `transition` is absent or `"none"`, the image is rendered instantly.

### `none`

Immediate image swap.

```json
{
  "img_url": "http://server/next.png",
  "transition": "none"
}
```

### `flash`

Shows a full-screen flash, then displays the new image.

Params:

- `color`: optional hex RGB, default `"#FFFFFF"`
- `duration_ms`: optional, default `200`

```json
{
  "img_url": "http://server/next.png",
  "transition": "flash",
  "transition_params": {
    "color": "#FFFFFF",
    "duration_ms": 250
  }
}
```

### `wipe_down`

Draws the new image from top to bottom.

Params:

- `duration_ms`: optional, default `500`

```json
{
  "img_url": "http://server/next.png",
  "transition": "wipe_down",
  "transition_params": {
    "duration_ms": 700
  }
}
```

### `wipe_right`

Draws the new image from left to right.

Params:

- `duration_ms`: optional, default `500`

```json
{
  "img_url": "http://server/next.png",
  "transition": "wipe_right",
  "transition_params": {
    "duration_ms": 700
  }
}
```

### `shimmer`

Draws the new image, overlays a diagonal shine wave, then redraws the final image.

Params:

- `color`: optional hex RGB, default `"#88CCFF"`
- `duration_ms`: optional, default `800`

```json
{
  "img_url": "http://server/card.png",
  "transition": "shimmer",
  "transition_params": {
    "color": "#88CCFF",
    "duration_ms": 800
  }
}
```

### `dissolve`

Randomly reveals pixels of the new image using a 1-bit progress bitmap.

Params:

- `duration_ms`: optional, default `800`

```json
{
  "img_url": "http://server/next.png",
  "transition": "dissolve",
  "transition_params": {
    "duration_ms": 900
  }
}
```

### `pixelate`

Reveals the new image in randomized blocks.

Params:

- `block_size`: optional, default `8`
- `duration_ms`: optional, default `800`

```json
{
  "img_url": "http://server/next.png",
  "transition": "pixelate",
  "transition_params": {
    "block_size": 8,
    "duration_ms": 800
  }
}
```

### `matrix`

Column rain effect reveals the new image.

Params:

- `color`: optional hex RGB, default `"#00FF66"`
- `duration_ms`: optional, default `900`

```json
{
  "img_url": "http://server/next.png",
  "transition": "matrix",
  "transition_params": {
    "color": "#00FF66",
    "duration_ms": 900
  }
}
```

## Sending Commands with `curl`

Update image and LED:

```bash
curl -X POST "http://omnimini-7.local/update" \
  -H "Content-Type: application/json" \
  -d '{
    "screen_bri": 200,
    "led": {
      "mode": "static",
      "colors": ["#00FF00"],
      "brightness": 180
    },
    "img_url": "http://192.168.1.10:8080/characters/rogue.png",
    "transition": "wipe_down",
    "transition_params": {
      "duration_ms": 500
    }
  }'
```

Use an IP address if mDNS is unavailable:

```bash
curl -X POST "http://192.168.1.42/update" \
  -H "Content-Type: application/json" \
  -d '{"img_url":"http://192.168.1.10:8080/frame.png"}'
```

## `/frame` Endpoint: Direct PNG Upload

Use `/frame` when the server already has PNG bytes and wants to push a frame directly without JSON and without a separate image URL.

Method:

```text
POST /frame
Content-Type: image/png
Body: raw PNG bytes
```

Success response:

```json
{"status":"success"}
```

Error responses:

```json
{"status":"error","msg":"method"}
{"status":"error","msg":"body"}
{"status":"error","msg":"png"}
```

### Upload One PNG with `curl`

```bash
curl -X POST "http://omnimini-7.local/frame" \
  -H "Content-Type: image/png" \
  --data-binary "@frame.png"
```

### Frame-by-Frame Animation Streaming

To stream animation, generate PNG frames on the server and POST them one by one to `/frame`.

Recommended frame properties:

- PNG format
- Resolution: 172x320
- Keep file size small
- Avoid very high FPS over WiFi
- Start with 5-10 FPS, then tune

Python example:

```python
import time
import requests
from pathlib import Path

DEVICE = "http://omnimini-7.local"
FPS = 8
FRAME_DELAY = 1.0 / FPS

frames = sorted(Path("frames").glob("*.png"))

for frame_path in frames:
    data = frame_path.read_bytes()
    r = requests.post(
        f"{DEVICE}/frame",
        data=data,
        headers={"Content-Type": "image/png"},
        timeout=3,
    )
    print(frame_path.name, r.status_code, r.text)
    time.sleep(FRAME_DELAY)
```

For looped animation:

```python
import itertools
import time
import requests
from pathlib import Path

DEVICE = "http://192.168.1.42"
frames = [p.read_bytes() for p in sorted(Path("frames").glob("*.png"))]

for data in itertools.cycle(frames):
    requests.post(
        f"{DEVICE}/frame",
        data=data,
        headers={"Content-Type": "image/png"},
        timeout=3,
    )
    time.sleep(0.125)  # 8 FPS
```

Notes:

- `/frame` does not apply transition effects.
- `/frame` is intended for animation frames, direct pushes, and low-latency server rendering.
- `/update` is better for semantic state changes, LED commands, brightness changes, and transitions.

## Heartbeat

Omnimini sends heartbeat POST requests every 15 seconds to:

```text
<server_url>/heartbeat
```

Example body:

```json
{
  "id": "omnimini_7",
  "ip": "192.168.1.42",
  "rssi": -62,
  "uptime": 3600
}
```

Server response should be any HTTP `2xx` status.

Watchdog behavior:

- one failed heartbeat increments fail counter
- 3 consecutive heartbeat failures set WARN state
- WARN state: yellow breathing LED, yellow status dot
- WiFi loss immediately sets ERROR state
- ERROR state: red blinking LED, red blinking status dot
- after 12 failed reconnect attempts, ESP restarts
- first successful heartbeat after downtime sets OK state

The server is responsible for resending the last desired image/LED state after downtime.

## Boot Animation

Boot animation lives in `boot_anim.h`.

Current placeholder:

- one black PNG frame
- 172x320
- stored in PROGMEM

The renderer calls:

```cpp
Renderer::playBootAnimation();
```

before WiFiManager starts.

### How to Add Real Boot Frames

1. Prepare PNG frames at exactly 172x320.
2. Convert each PNG to a C byte array in PROGMEM.
3. Add the arrays to `boot_anim.h`.
4. Add each frame pointer to `boot_anim_frames`.
5. Add each frame size to `boot_anim_frame_sizes`.
6. Update `boot_anim_frame_count`.

Example structure:

```cpp
static const uint8_t boot_frame_0_png[] PROGMEM = {
  // PNG bytes...
};

static const uint8_t boot_frame_1_png[] PROGMEM = {
  // PNG bytes...
};

static const uint8_t *const boot_anim_frames[] PROGMEM = {
  boot_frame_0_png,
  boot_frame_1_png
};

static const size_t boot_anim_frame_sizes[] PROGMEM = {
  sizeof(boot_frame_0_png),
  sizeof(boot_frame_1_png)
};

static const uint8_t boot_anim_frame_count = 2;
```

Recommended frame count:

- keep it small
- prefer compressed PNGs
- avoid large uncompressed arrays

Every frame decode yields and resets the watchdog when applicable.

## Splash Image

The splash image is stored in:

```text
splash.h
```

It defines:

```cpp
#define splash_png ...
```

The splash is shown after boot animation and before WiFi setup completes.

## Server Integration Pattern

Typical server responsibilities:

1. Render final 172x320 PNG images.
2. Host images for `/update` via `img_url`, or push raw PNG frames to `/frame`.
3. Receive heartbeat at `/heartbeat`.
4. Track device state by `id`, for example `omnimini_7`.
5. Resend last state after device reconnection.

Suggested endpoints on the server:

```text
GET  /characters/<id>.png
POST /heartbeat
POST /command-to-device     // optional server-side management endpoint
```

Device-facing examples:

```json
{
  "img_url": "http://server/characters/7.png",
  "transition": "shimmer",
  "transition_params": {
    "color": "#88CCFF",
    "duration_ms": 800
  }
}
```

```json
{
  "led": {
    "mode": "blink",
    "colors": ["#FF0000"],
    "speed": 400,
    "brightness": 255
  }
}
```

## Limits and Recommendations

- Max downloaded PNG size: `256 * 1024` bytes.
- Recommended image resolution: exactly 172x320.
- Keep PNGs optimized for WiFi speed.
- Do not stream too fast over `/frame`; start with 5-10 FPS.
- Avoid long blocking server timeouts.
- Prefer `/update` for game state and `/frame` for short animations.
- Keep boot animation small to save flash.

## File Map

```text
Omnimini.ino          setup, loop, WiFiManager, HTTP handlers
Config.h             pins, sizes, timing, semantic colors
Display_ST7789.*     low-level ST7789 SPI driver
LedController.*      WS2812B LED modes
Renderer.*           PNG decode, downloads, transitions, boot animation
UI.*                 waiting screen and status dot
Heartbeat.*          heartbeat sender and connection watchdog
splash.h             splash PNG in PROGMEM
boot_anim.h          boot animation frames in PROGMEM
```

## Quick Test Checklist

1. Flash firmware.
2. Open WiFiManager portal.
3. Set `mini_id` and `server_url`.
4. Wait for the waiting screen.
5. Send `/update` with only `img_url`.
6. Send `/update` with LED config.
7. Test each transition.
8. Upload one PNG to `/frame`.
9. Stream a short PNG sequence to `/frame`.
10. Stop the server and confirm WARN state.
11. Disconnect WiFi and confirm ERROR state.
12. Reconnect and confirm OK state after heartbeat.
