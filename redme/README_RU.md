# Прошивка Omnimini 1.2

Omnimini - это WiFi-управляемый дисплей для миниатюр в настольных RPG на базе ESP32-C6. Прошивка управляет дисплеем ST7789 172x320 и одним RGB-светодиодом WS2812B, принимает HTTP-команды, скачивает или принимает PNG-кадры и отправляет серверу состояние устройства.

## Целевое железо

- Плата: Waveshare ESP32-C6-LCD-1.47 или совместимая плата на ESP32-C6
- Дисплей: ST7789, 172x320
- Светодиод: один WS2812B / NeoPixel на пине 8
- Кнопка сброса: пин 9, удержание 5 секунд сбрасывает настройки WiFiManager

Основные константы собраны в `Config.h`.

## Основные возможности

- Captive portal WiFiManager для первичной настройки
- Кастомный темный портал настройки Omnimini
- Номер устройства сохраняется как `mini_id`
- URL сервера сохраняется как `server_url`
- Поддержка OTA через ArduinoOTA
- Отрисовка PNG по URL через `/update`
- Прямая загрузка PNG-кадра через `/frame`
- Управление RGB LED из JSON
- Экран ожидания после подключения WiFi
- Индикатор состояния в правом верхнем углу
- Heartbeat POST на сервер каждые 15 секунд
- Watchdog подключения с состояниями WARN / ERROR
- Transition effects для обновления изображения
- Инфраструктура boot animation через `boot_anim.h`

## Настройка сети

При первом запуске или после удержания кнопки сброса 5 секунд Omnimini открывает captive portal WiFiManager.

Поля портала:

- `mini_id`: номер миниатюры, например `7`
- `server_url`: базовый URL сервера, например `http://192.168.1.10:8080`

После настройки:

- id устройства становится `omnimini_<mini_id>`, например `omnimini_7`
- mDNS hostname становится `omnimini-<mini_id>`, например `omnimini-7`
- HTTP-сервер слушает порт `80`

Примеры локальных URL:

```text
http://omnimini-7.local/update
http://omnimini-7.local/frame
```

Если mDNS в сети недоступен, используйте IP-адрес устройства из роутера.

## Последовательность загрузки

1. Инициализация дисплея и подсветки.
2. Инициализация LED и установка boot color.
3. Проигрывание boot animation из `boot_anim.h`.
4. Отрисовка splash image из `splash.h`.
5. Запуск WiFiManager при необходимости.
6. После подключения WiFi отображается экран ожидания:

```text
OMNIMINI

7

waiting...
```

7. Запуск HTTP-сервера, OTA, heartbeat и основного цикла.

## Индикатор состояния

В правом верхнем углу рисуется точка 4x4 пикселя.

- Зеленая: WiFi и heartbeat OK
- Желтая: сервер не ответил на heartbeat 3 раза подряд
- Красная мигающая: WiFi потерян

Точка перерисовывается после полных обновлений изображения.

## Поведение LED

LED может управляться игровыми командами через `/update`, но часть цветов зарезервирована как соглашение для системных состояний:

- `LED_COLOR_BOOT`: темно-синий, инициализация
- `LED_COLOR_CONNECTED`: темно-зеленый, WiFi OK
- `LED_COLOR_WARN`: желтый, сервер недоступен
- `LED_COLOR_ERROR`: красный, WiFi потерян
- `LED_COLOR_RECONNECT`: синий, переподключение

Игровые команды все равно могут отправлять любые цвета. Эти semantic colors являются рекомендацией, а не жестким ограничением.

## Endpoint `/update`

`/update` используется для отправки JSON-команды. Команда может менять яркость экрана, режим LED, URL изображения и transition effect.

Метод:

```text
POST /update
Content-Type: application/json
```

Успешный ответ:

```json
{"status":"success"}
```

Ошибки:

```json
{"status":"error","msg":"method"}
{"status":"error","msg":"body"}
{"status":"error","msg":"json"}
```

### Минимальное обновление изображения

```json
{
  "img_url": "http://192.168.1.10:8080/images/character.png"
}
```

Если `transition` отсутствует, поведение такое же, как `"none"`: новый PNG рисуется сразу.

### Полный пример обновления

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

Яркость дисплея от `0` до `255`.

Внутри значение преобразуется в процент подсветки от `0` до `100`.

```json
{
  "screen_bri": 128
}
```

### `led`

Формат LED-команды:

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

Поддерживаемые режимы:

- `static`: постоянный цвет
- `cycle`: циклическое переключение цветов
- `blink`: мигание цветом
- `breathe`: плавное дыхание яркости
- `pulse`: короткий pulse pattern
- `rainbow`: анимация цветового круга

Поля:

- `colors`: массив HEX RGB строк, например `"#FFAA00"`
- `speed`: период режима в миллисекундах
- `brightness`: от `0` до `255`

Если `colors` пустой или отсутствует, используется зеленый цвет по умолчанию.

## Transition Effects

Endpoint `/update` принимает:

```json
{
  "img_url": "http://server/next.png",
  "transition": "flash",
  "transition_params": {
    "duration_ms": 300
  }
}
```

Если `transition` отсутствует или равен `"none"`, изображение рисуется мгновенно.

### `none`

Мгновенная замена изображения.

```json
{
  "img_url": "http://server/next.png",
  "transition": "none"
}
```

### `flash`

Показывает полноэкранную вспышку, затем отображает новое изображение.

Параметры:

- `color`: необязательный HEX RGB, по умолчанию `"#FFFFFF"`
- `duration_ms`: необязательный, по умолчанию `200`

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

Рисует новое изображение сверху вниз.

Параметры:

- `duration_ms`: необязательный, по умолчанию `500`

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

Рисует новое изображение слева направо.

Параметры:

- `duration_ms`: необязательный, по умолчанию `500`

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

Сначала рисует новое изображение, затем накладывает диагональную световую волну и в конце перерисовывает финальную картинку.

Параметры:

- `color`: необязательный HEX RGB, по умолчанию `"#88CCFF"`
- `duration_ms`: необязательный, по умолчанию `800`

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

Случайно проявляет пиксели нового изображения, используя 1-bit progress bitmap.

Параметры:

- `duration_ms`: необязательный, по умолчанию `800`

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

Проявляет новое изображение случайными блоками.

Параметры:

- `block_size`: необязательный, по умолчанию `8`
- `duration_ms`: необязательный, по умолчанию `800`

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

Эффект column rain, который проявляет новое изображение.

Параметры:

- `color`: необязательный HEX RGB, по умолчанию `"#00FF66"`
- `duration_ms`: необязательный, по умолчанию `900`

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

## Отправка команд через `curl`

Обновить изображение и LED:

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

Если mDNS недоступен, используйте IP-адрес:

```bash
curl -X POST "http://192.168.1.42/update" \
  -H "Content-Type: application/json" \
  -d '{"img_url":"http://192.168.1.10:8080/frame.png"}'
```

## Endpoint `/frame`: прямая загрузка PNG

`/frame` нужен, когда сервер уже имеет PNG bytes и хочет отправить кадр напрямую без JSON и без отдельного URL изображения.

Метод:

```text
POST /frame
Content-Type: image/png
Body: raw PNG bytes
```

Успешный ответ:

```json
{"status":"success"}
```

Ошибки:

```json
{"status":"error","msg":"method"}
{"status":"error","msg":"body"}
{"status":"error","msg":"png"}
```

### Загрузка одного PNG через `curl`

```bash
curl -X POST "http://omnimini-7.local/frame" \
  -H "Content-Type: image/png" \
  --data-binary "@frame.png"
```

### Покадровая отправка анимации

Чтобы стримить анимацию, сервер генерирует PNG-кадры и отправляет их по одному в `/frame`.

Рекомендуемые свойства кадров:

- формат PNG
- разрешение: 172x320
- небольшой размер файла
- не использовать слишком высокий FPS по WiFi
- начать с 5-10 FPS и затем подбирать значение

Пример на Python:

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

Пример зацикленной анимации:

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

Примечания:

- `/frame` не применяет transition effects.
- `/frame` предназначен для animation frames, direct push и низкой задержки при серверном рендеринге.
- `/update` лучше использовать для смены игрового состояния, LED-команд, яркости и transitions.

## Heartbeat

Omnimini отправляет heartbeat POST каждые 15 секунд на:

```text
<server_url>/heartbeat
```

Пример body:

```json
{
  "id": "omnimini_7",
  "ip": "192.168.1.42",
  "rssi": -62,
  "uptime": 3600
}
```

Сервер должен отвечать любым HTTP статусом `2xx`.

Поведение watchdog:

- один failed heartbeat увеличивает счетчик ошибок
- 3 heartbeat failures подряд переводят устройство в WARN
- WARN: желтое дыхание LED, желтая status dot
- потеря WiFi сразу переводит устройство в ERROR
- ERROR: красное мигание LED, красная мигающая status dot
- после 12 неудачных попыток reconnect ESP перезапускается
- первый успешный heartbeat после downtime возвращает OK state

Сервер отвечает за повторную отправку последнего нужного image/LED state после восстановления связи.

## Boot Animation

Boot animation находится в `boot_anim.h`.

Текущий placeholder:

- один черный PNG-кадр
- 172x320
- хранится в PROGMEM

Renderer вызывает:

```cpp
Renderer::playBootAnimation();
```

до запуска WiFiManager.

### Как добавить реальные boot frames

1. Подготовьте PNG-кадры строго 172x320.
2. Конвертируйте каждый PNG в C byte array в PROGMEM.
3. Добавьте массивы в `boot_anim.h`.
4. Добавьте указатель на каждый кадр в `boot_anim_frames`.
5. Добавьте размер каждого кадра в `boot_anim_frame_sizes`.
6. Обновите `boot_anim_frame_count`.

Пример структуры:

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

Рекомендации по количеству кадров:

- держите boot animation маленькой
- используйте сжатые PNG
- избегайте больших несжатых массивов

При декодировании каждого кадра прошивка делает yield и сбрасывает watchdog, когда это применимо.

## Splash Image

Splash image хранится в:

```text
splash.h
```

Он определяет:

```cpp
#define splash_png ...
```

Splash показывается после boot animation и перед завершением WiFi setup.

## Схема интеграции с сервером

Типичные задачи сервера:

1. Рендерить финальные PNG изображения 172x320.
2. Хостить изображения для `/update` через `img_url` или отправлять raw PNG frames в `/frame`.
3. Принимать heartbeat на `/heartbeat`.
4. Отслеживать состояние устройства по `id`, например `omnimini_7`.
5. Повторно отправлять последнее состояние после reconnect устройства.

Рекомендуемые endpoints на сервере:

```text
GET  /characters/<id>.png
POST /heartbeat
POST /command-to-device     // optional server-side management endpoint
```

Примеры команд для устройства:

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

## Ограничения и рекомендации

- Максимальный размер скачиваемого PNG: `256 * 1024` bytes.
- Рекомендуемое разрешение изображения: строго 172x320.
- Оптимизируйте PNG для скорости передачи по WiFi.
- Не стримьте через `/frame` слишком быстро; начните с 5-10 FPS.
- Избегайте долгих blocking server timeouts.
- Используйте `/update` для игрового состояния, а `/frame` для коротких анимаций.
- Держите boot animation маленькой, чтобы экономить flash.

## Карта файлов

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

## Быстрый чеклист тестирования

1. Прошить firmware.
2. Открыть WiFiManager portal.
3. Установить `mini_id` и `server_url`.
4. Дождаться waiting screen.
5. Отправить `/update` только с `img_url`.
6. Отправить `/update` с LED config.
7. Проверить каждый transition.
8. Загрузить один PNG в `/frame`.
9. Отстримить короткую PNG-последовательность в `/frame`.
10. Остановить сервер и проверить WARN state.
11. Отключить WiFi и проверить ERROR state.
12. Подключить обратно и проверить OK state после heartbeat.
