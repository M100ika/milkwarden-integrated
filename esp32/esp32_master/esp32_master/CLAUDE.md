# Milkwarden ESP32 Master — CLAUDE.md

## Проект
**Milkwarden Master** — шлюз между 4 slave ESP32 и Raspberry Pi.
Принимает SnapshotPacket + SessionPacket по ESP-NOW, пересылает на RPi как JSON по UART.
Никакой бизнес-логики, никакого дисплея — только приём и ретрансляция.

Суперпроект: `Milkwarden-superproject/integrated/esp32/esp32_master`
Slave-проект (уже готов): `../esp32_milkwarden`

## Архитектура системы

```
[Slave 1 (ID=1)] ──┐
[Slave 2 (ID=2)] ──┤  ESP-NOW   ┌──────────────┐  USB/UART0  ┌─────────────┐  UART  ┌─────────┐
[Slave 3 (ID=3)] ──┼───────────►│ Master ESP32 │────────────►│ Raspberry Pi│───────►│ Nextion │
[Slave 4 (ID=4)] ──┘            └──────────────┘             └─────────────┘        └─────────┘
```

**Master ESP32** — только шлюз ESP-NOW → UART → RPi.
**Raspberry Pi** — вся логика: хранение данных, управление Nextion, сервер.

## Стек
- **Платформа:** ESP32
- **Framework:** Arduino + PlatformIO (env: `esp32dev`)
- **RTOS:** FreeRTOS
- **Язык:** C / C++

## Аппаратные подключения

| Интерфейс | ESP32 GPIO | Назначение | Baud |
|-----------|-----------|------------|------|
| UART0 | GPIO1 (TX) / GPIO3 (RX) | **USB → Raspberry Pi** | 115200 |

**Важно:**
- UART0 (Serial) занят под RPi — `Serial.print` для логов **нельзя**
- Единственный инструмент отладки: **Telnet** (порт 23, WiFi)
- Nextion подключён к Raspberry Pi напрямую, мастер его не касается

## WiFi мастера

Мастер и все slave-ы подключаются к **одному внешнему роутеру**.
Slave-ы получают интернет → NTP работает. Все на одном WiFi-канале → ESP-NOW работает.

Режим мастера: `WIFI_STA` (только клиент, без AP).
Slave-ы остаются в `WIFI_AP_STA` как есть.

**MASTER_MAC** = STA MAC мастера (`WiFi.macAddress()`), **не** softAP MAC.

```cpp
// Получить MAC мастера — временно добавить в setup() до initEspNow():
Serial.begin(115200);
WiFi.mode(WIFI_STA);
Serial.println(WiFi.macAddress());   // скопировать → прописать в slave config.h
```

Slave-ы синхронизируют канал ESP-NOW с каналом роутера автоматически при подключении
(уже реализовано в slave wifi.cpp через `esp_wifi_set_channel` в `onGotIp`).

## Форматы входящих пакетов (ESP-NOW от slave)

Различаются по первому байту `type`.

### SnapshotPacket (`type = 0x01`) — каждые 500 мс от каждого slave

```cpp
struct __attribute__((packed)) SnapshotPacket {
    uint8_t  type;           // 0x01
    uint8_t  esp_id;         // номер slave 1-4
    uint32_t ip_addr;        // IP slave
    char     rfid_tag[25];   // подтверждённый EPC hex, "" если нет
    uint8_t  beam_state;     // 0=луч есть  1=прерван
    uint8_t  device_state;   // 0=IDLE 1=COW_PRESENT 2=MILKING
    float    weight;         // текущий вес в граммах
    uint32_t timestamp;      // Unix time (NTP UTC+5 Kazakhstan)
    uint8_t  msg_state;      // 1=ok 2=fail
};
// sizeof = 40 байт
```

### SessionPacket (`type = 0x02`) — по завершении сессии / смене бидона

```cpp
struct __attribute__((packed)) SessionPacket {
    uint8_t  type;           // 0x02
    uint8_t  esp_id;         // номер slave 1-4
    uint32_t ip_addr;
    char     rfid_tag[25];   // RFID коровы, "" если не считали
    float    weight_initial; // вес в начале сессии (граммы)
    float    weight_final;   // вес в конце (граммы)
    uint32_t start_time;     // Unix time
    uint32_t end_time;       // Unix time
    uint8_t  end_reason;     // 0=корова ушла  1=смена бидона
    uint8_t  msg_state;      // 1=ok 2=fail
};
// sizeof = 48 байт
```

### Константы

```cpp
#define DEV_STATE_IDLE        0
#define DEV_STATE_COW_PRESENT 1
#define DEV_STATE_MILKING     2

#define END_REASON_COW_LEFT      0
#define END_REASON_BUCKET_CHANGE 1
```

## Формат вывода на RPi (UART, JSON)

**Только SessionPacket пересылается на RPi** (SnapshotPacket — только для Nextion,
которым управляет RPi; RPi может запрашивать live-данные отдельно если нужно).

Каждый SessionPacket → одна строка JSON + `\n`:

```json
{"type":"session","id":1,"rfid":"E20068151234ABCD","w_init":0.0,"w_final":14500.0,"t_start":1746492000,"t_end":1746492300,"reason":0}
```

SnapshotPacket — тоже пересылать, отдельной строкой:

```json
{"type":"snap","id":1,"rfid":"E20068151234ABCD","beam":0,"state":2,"weight":14250.5,"ts":1746492180}
```

> RPi читает построчно (`readline()`), разбирает по полю `"type"`.

## Логика мастера (минимальная)

```
ESP-NOW onReceive(buf, len):
  if buf[0] == 0x01 → распаковать SnapshotPacket → отправить JSON snap на RPi
  if buf[0] == 0x02 → распаковать SessionPacket  → отправить JSON session на RPi
```

Никакого хранения состояния, никакой бизнес-логики. Просто конвертация и пересылка.

## Telnet CLI (минимальный)

Поскольку Serial занят — Telnet обязателен для отладки:

| Команда | Описание |
|---------|---------|
| `status` | uptime, кол-во принятых пакетов по каждому slave, WiFi AP статус |
| `last <id>` | последний снапшот от slave N |
| `stats` | счётчики snap/session пакетов |
| `reboot` | перезагрузка |

## Структура проекта (рекомендуемая)

```
src/
├── main.cpp
├── config.h              # AP SSID/pass/channel, baud, packet structs
├── devices/
│   ├── wifi/             # SoftAP + ESP-NOW init + onReceive callback
│   └── telnet/           # Telnet CLI
└── modules/
    └── gateway/          # конвертация пакетов → JSON → Serial (RPi)
```

Задачи FreeRTOS не нужны — ESP-NOW callback работает асинхронно,
`Serial.println()` из callback безопасен для коротких строк.

## Сборка

```bash
pio run                    # сборка
pio run --target upload    # прошивка
# Serial монитор не используется (занят RPi)
# Отладка: telnet <IP мастера> 23
```

## MAC-адрес мастера

Использовать **STA MAC** (`WiFi.macAddress()`), не softAP MAC.
Временно прочитать через Serial в `setup()`:
```cpp
Serial.begin(115200);
WiFi.mode(WIFI_STA);
Serial.println(WiFi.macAddress());  // напр. "AA:BB:CC:DD:EE:FF"
```
Прописать в `src/config.h` всех slave-ов:
```cpp
static const uint8_t MASTER_MAC[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };
```

## Git

Репозиторий: https://github.com/M100ika/milkwarden-integrated
Ветка: `main`
