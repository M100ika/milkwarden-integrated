# Milkwarden ESP32 — CLAUDE.md

## Проект
**Milkwarden** — IoT устройство на ESP32 для мониторинга молока (весовой контроль).
Часть большого суперпроекта: `Milkwarden-superproject/integrated/esp32/esp32_milkwarden`

## Стек
- **Платформа:** ESP32
- **Framework:** Arduino (milka.ino) + PlatformIO (platformio.ini)
- **RTOS:** FreeRTOS (tasks/)
- **Язык:** C / C++

## Структура проекта
src/
├── main.cpp                  # Точка входа
├── config.h                  # Глобальные настройки
├── devices/                  # Драйверы железа
│   ├── loadcell/             # Весовой датчик (HX711 или аналог)
│   ├── wifi/                 # WiFi подключение
│   └── telnet/               # Telnet отладка по сети
├── modules/                  # Бизнес-логика
│   ├── heartbeat/            # Watchdog / признак жизни
│   ├── ota/                  # OTA обновление прошивки
│   └── storage/              # NVS (энергонезависимое хранилище)
└── tasks/                    # FreeRTOS задачи
milka/                        # Arduino-версия (legacy или параллельная)
├── milka.ino
└── ota_module.ino
data/
└── config.ini                # Конфиг (SPIFFS/LittleFS)

## Ключевые модули

### loadcell (весовой датчик)
- Файлы: `src/devices/loadcell/loadcell.cpp/.h`
- Считывает вес молока

### WiFi
- Файлы: `src/devices/wifi/wifi.cpp/.h`
- Подключение к сети, переподключение при обрыве

### Telnet
- Файлы: `src/devices/telnet/telnet.cpp/.h`
- Отладочный вывод через сеть (альтернатива Serial)

### OTA (Over-The-Air обновление)
- Файлы: `src/modules/ota/ota.cpp/.h` + `milka/ota_module.ino`
- Обновление прошивки по воздуху без физического подключения

### NVS Storage
- Файлы: `src/modules/storage/nvs_manager.cpp/.h`
- Хранение настроек и данных в энергонезависимой памяти ESP32

### Heartbeat
- Файлы: `src/modules/heartbeat/heartbeat.cpp/.h`
- Признак жизни устройства, watchdog

### FreeRTOS Tasks
- Файлы: `src/tasks/freeRTOS_tasks.cpp/.h`
- Все задачи вынесены сюда, управление потоками

## Сборка

```bash
# PlatformIO
pio run                        # сборка
pio run --target upload        # прошивка
pio run --target uploadfs      # загрузка data/ (config.ini)
pio device monitor             # Serial монитор

# Готовые бинарники
milka/build/esp32.esp32.esp32/milka.ino.merged.bin
```

## Конфигурация
- `data/config.ini` — WiFi, сервер, параметры устройства (грузится через SPIFFS)
- `src/config.h` — компайл-тайм константы
- `platformio.ini` — настройки платформы, библиотеки, порт

## Git история (последнее)
- `42bc1f8` — dd
- `cca7cc4` — Prepare integrated module without server/raspberry/modeling3d
- `b65f3ca` — updating

## Контекст суперпроекта
Это **integrated** версия — без:
- server (бэкенд)
- raspberry (Raspberry Pi модуль)
- modeling3d (3D модели корпуса)

Только ESP32 прошивка как самостоятельный модуль.

## Частые задачи
- Добавить новый датчик → создать папку в `src/devices/`
- Добавить логику → создать папку в `src/modules/`
- Новая FreeRTOS задача → добавить в `src/tasks/freeRTOS_tasks`
- Изменить конфиг WiFi/сервера → `data/config.ini`