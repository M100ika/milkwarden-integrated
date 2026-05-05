# Milkwarden ESP32

IoT-устройство на ESP32 для мониторинга молока: измерение веса через тензодатчик, передача данных по ESP-NOW на мастер-устройство и/или в облако по HTTPS.

## Содержание

- [Требования](#требования)
- [Быстрый старт](#быстрый-старт)
- [Конфигурация](#конфигурация)
- [Прошивка и файловая система](#прошивка-и-файловая-система)
- [Подключение тензодатчика](#подключение-тензодатчика)
- [Калибровка весов](#калибровка-весов)
- [Telnet CLI](#telnet-cli)
- [ESP-NOW](#esp-now)
- [OTA обновление](#ota-обновление)
- [Структура проекта](#структура-проекта)

---

## Требования

- **Плата:** ESP32 (esp32dev)
- **IDE:** [PlatformIO](https://platformio.org/) (VS Code или CLI)
- **Библиотеки** (устанавливаются автоматически через `platformio.ini`):
  - `bogde/HX711 @ ^0.7.5`
  - `lennarthennigs/ESP Telnet @ ^2.2.3`

---

## Быстрый старт

```bash
# 1. Клонировать репозиторий
git clone https://github.com/M100ika/milkwarden-integrated
cd milkwarden-integrated/esp32/esp32_milkwarden

# 2. Заполнить конфиг (см. раздел Конфигурация)
#    src/config.h  — WiFi, MAC мастера, URL облака
#    data/config.ini — параметры калибровки

# 3. Собрать и прошить
pio run --target upload

# 4. Загрузить файловую систему (config.ini)
pio run --target uploadfs

# 5. Открыть монитор
pio device monitor
```

После старта устройство:
1. Подключается к WiFi (список сетей из `config.h`)
2. Инициализирует ESP-NOW и регистрирует мастер-пир
3. Запускает Telnet-сервер на порту **23**
4. Начинает считывать вес через HX711

---

## Конфигурация

### `src/config.h` — compile-time константы

| Параметр | Описание |
|---|---|
| `FIRMWARE_VERSION` | Версия прошивки (строка) |
| `ESP_DEVICE_ID` | Номер слейва (1–4) |
| `WIFI_CREDENTIALS` | Массив `{ssid, password}` — поддерживается несколько сетей |
| `WIFI_LED_PIN` | GPIO светодиода статуса WiFi (по умолчанию `2`) |
| `WIFI_RECONNECT_INTERVAL_MS` | Интервал попытки переподключения |
| `MASTER_MAC` | MAC-адрес мастер-ESP32 для ESP-NOW |
| `ESPNOW_DEFAULT_CHANNEL` | Базовый Wi-Fi канал для ESP-NOW |
| `CLOUD_ENDPOINT_URL` | URL для HTTPS POST (`sendToCloud`) |
| `CLOUD_POST_TIMEOUT_MS` | Таймаут HTTP-запроса в облако |
| `LOADCELL_DOUT_PIN` / `LOADCELL_SCK_PIN` | GPIO для HX711 |
| `OTA_FIRMWARE_URL` | URL бинарника прошивки для OTA |

### `data/config.ini` — runtime параметры (LittleFS)

Значения здесь переопределяют NVS при каждом старте. Удалите ключ, чтобы использовать сохранённое значение из NVS.

```ini
[scale]
factor = 420.0   ; ADC единиц на грамм
samples = 15     ; количество замеров для фильтра

[autozero]
enabled   = false
threshold = 10.0  ; грамм — порог "около нуля"
hold_ms   = 8000  ; мс удержания перед авторасчётом

[wifi]
; ssid и password пока не используются — задаются в config.h
```

> **Внимание:** `config.ini` загружается командой `pio run --target uploadfs`. После изменения файла нужно повторить загрузку ФС.

---

## Прошивка и файловая система

```bash
pio run                         # только сборка
pio run --target upload         # сборка + прошивка через USB
pio run --target uploadfs       # загрузка data/ на LittleFS
pio device monitor              # Serial-монитор (115200 baud)
```

**Релиз бинарника для OTA:**

```bash
pio run
mkdir -p build
cp .pio/build/esp32dev/firmware.bin build/firmware.bin
git add build/firmware.bin && git commit -m "Update firmware binary" && git push
```

После пуша команда `update` в Telnet запустит OTA-обновление.

---

## Подключение тензодатчика

| Провод тензодатчика | HX711 |
|---|---|
| Красный | E+ |
| Чёрный | E- |
| Зелёный | A+ |
| Белый | A- |

HX711 → ESP32:

| HX711 | ESP32 |
|---|---|
| DOUT | GPIO 34 (`LOADCELL_DOUT_PIN`) |
| SCK | GPIO 32 (`LOADCELL_SCK_PIN`) |
| VCC | 3.3V или 5V |
| GND | GND |

Если показания `-8388608` (насыщение): проверьте полярность A+/A- и питание HX711.

---

## Калибровка весов

Подключитесь Telnet-клиентом (`telnet <IP> 23`) и выполните:

```
cal_tare              # зафиксировать ноль (платформа пустая)
cal_weight 500        # положить известный груз (500 г) → рассчитает factor
factor                # проверить полученные значения
save                  # сохранить в NVS
```

**Проверка углов платформы** (опционально):

```
corner_test FL        # измерить передний-левый угол
corner_test FR        # передний-правый
corner_test BL        # задний-левый
corner_test BR        # задний-правый
corner_report         # отчёт: какой угол нужно поднять/опустить
corner_clear          # сбросить данные углов
```

---

## Telnet CLI

Подключение: `telnet <IP устройства> 23`

### Измерение

| Команда | Описание |
|---|---|
| `start` | Начать измерение |
| `stop` | Остановить измерение |
| `tare` | Обнулить весы |
| `samples <n>` | Количество замеров для фильтра (1–64) |

### Калибровка

| Команда | Описание |
|---|---|
| `cal_tare` | Зафиксировать ноль |
| `cal_weight <г>` | Рассчитать factor по известному грузу |
| `calib <factor>` | Задать factor вручную |
| `factor` | Показать текущие factor / offset / samples |

### Auto-Zero

| Команда | Описание |
|---|---|
| `autozero on/off` | Включить/выключить авторасчёт нуля |
| `az_thr <г>` | Порог "около нуля" в граммах |
| `az_time <мс>` | Время удержания перед авторасчётом |

### Диагностика

| Команда | Описание |
|---|---|
| `diag` | Полная диагностика HX711 (связь, шум, насыщение) |
| `raw [n]` | Показать сырые и фильтрованные значения |
| `noise [n]` | Тест шума: peak-to-peak за n замеров |
| `gain <128\|64\|32>` | Изменить усиление HX711 |
| `wiring` | Справка по подключению |

### Система

| Команда | Описание |
|---|---|
| `status` | Версия, IP, состояние HX711, текущий вес |
| `save` | Сохранить настройки в NVS (flash) |
| `update` | Запустить OTA-обновление прошивки |

---

## ESP-NOW

Устройство работает в режиме **WiFi + ESP-NOW одновременно** (`WIFI_AP_STA`). При подключении к WiFi канал ESP-NOW автоматически синхронизируется с каналом точки доступа.

**Настройка перед прошивкой:**

1. Узнать MAC мастер-ESP32: `WiFi.macAddress()` на мастере
2. Прописать в `src/config.h`:
   ```cpp
   static const uint8_t MASTER_MAC[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
   ```

**Формат пакета (`DataPacket`):**

| Поле | Тип | Описание |
|---|---|---|
| `esp_id` | uint8 | Номер устройства (из `ESP_DEVICE_ID`) |
| `weight` | float | Вес в граммах |
| `rfid_tag` | char[15] | RFID-метка |
| `beam_state` | uint8 | 0=свободно, 1=перекрыто |
| `container_count` | uint8 | Счётчик контейнеров |
| `status_flags` | uint8 | bit0:финал, bit1:ошибка облака, bit2:WiFi |
| `ip_addr` | uint32 | IP-адрес устройства |

---

## OTA обновление

OTA работает через `HTTPClient` + `httpUpdate` по HTTPS. URL задаётся в `config.h`:

```cpp
#define OTA_FIRMWARE_URL \
    "https://github.com/M100ika/milkwarden-integrated/raw/refs/heads/main/esp32/esp32_milkwarden/build/firmware.bin"
```

Для запуска: подключиться по Telnet и выполнить команду `update`. Устройство скачает и применит прошивку, после чего перезагрузится.

---

## Структура проекта

```
src/
├── main.cpp
├── config.h
├── devices/
│   ├── loadcell/       # HX711: считывание, фильтрация, калибровка
│   ├── wifi/           # WiFi (AP+STA), автоматическое переподключение
│   ├── espnow/         # ESP-NOW: отправка DataPacket мастеру
│   └── telnet/         # Telnet-сервер + CLI
├── modules/
│   ├── heartbeat/      # Watchdog / признак жизни
│   ├── ota/            # OTA по HTTPS
│   ├── storage/        # NVS: сохранение настроек в flash
│   └── cloud/          # HTTPS POST в облако
└── tasks/
    └── freeRTOS_tasks  # FreeRTOS задачи (HX711 на Core 1)
data/
└── config.ini          # Runtime-конфиг (LittleFS)
build/
└── firmware.bin        # Готовый бинарник для OTA
```
