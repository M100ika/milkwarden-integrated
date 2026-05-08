# Инструкция: Отправка IP адреса в SnapshotPacket

## Обзор

Slave ESP32 теперь должен включать свой локальный IP адрес в каждый SnapshotPacket перед отправкой master'у по ESP-NOW.

Формат: IP адрес хранится как `uint32_t ip_addr` в сетевом порядке байт (little-endian на ESP32).

## Реализация на Slave

### 1. Получение текущего IP адреса

После подключения к роутеру и получения IP адреса через DHCP:

```cpp
#include <WiFi.h>

// В функции, которая подготавливает SnapshotPacket:
IPAddress localIP = WiFi.localIP();
uint32_t ip_value = localIP;  // автоматическое преобразование operator uint32_t()
```

**Или явно:**

```cpp
IPAddress localIP = WiFi.localIP();
uint32_t ip_addr = (localIP[0] << 0) | (localIP[1] << 8) | (localIP[2] << 16) | (localIP[3] << 24);
```

### 2. Добавление в SnapshotPacket

В функции, которая заполняет SnapshotPacket перед отправкой:

```cpp
void prepareSnapshot(uint8_t espId, const char* rfidTag, uint8_t beamState, 
                     uint8_t deviceState, float weight, uint32_t timestamp) {
    SnapshotPacket snap;
    snap.type = PKT_TYPE_SNAPSHOT;
    snap.esp_id = espId;
    snap.ip_addr = WiFi.localIP();  // ← ВАЖНО: добавить IP адрес
    strncpy(snap.rfid_tag, rfidTag, sizeof(snap.rfid_tag) - 1);
    snap.rfid_tag[sizeof(snap.rfid_tag) - 1] = '\0';
    snap.beam_state = beamState;
    snap.device_state = deviceState;
    snap.weight = weight;
    snap.timestamp = timestamp;
    snap.msg_state = 1;  // ok
    
    // Отправить по ESP-NOW
    espNowSendToMaster((uint8_t*)&snap, sizeof(snap));
}
```

### 3. Убедитесь, что:

- ✅ WiFi подключен к роутеру и получен IP адрес (`WiFi.status() == WL_CONNECTED`)
- ✅ `ip_addr` заполняется **для каждого SnapshotPacket** перед отправкой
- ✅ SessionPacket **тоже содержит** поле `ip_addr` — заполняйте его одинаково
- ✅ IP адрес хранится в стандартном сетевом формате (автоматически при присвоении)

### 4. Пример полной функции отправки

```cpp
void sendSnapshotToMaster() {
    // Проверить, что WiFi готов
    if (WiFi.status() != WL_CONNECTED) {
        return;  // IP адрес не доступен
    }
    
    SnapshotPacket snap = {};
    snap.type = PKT_TYPE_SNAPSHOT;
    snap.esp_id = ESP_ID;  // 1-4
    snap.ip_addr = WiFi.localIP();  // Текущий локальный IP
    
    // Заполнить остальные поля...
    strncpy(snap.rfid_tag, currentRfid.c_str(), sizeof(snap.rfid_tag) - 1);
    snap.beam_state = (beamInterrupted) ? 1 : 0;
    snap.device_state = getCurrentState();  // 0, 1 или 2
    snap.weight = scale.getWeight();
    snap.timestamp = time(nullptr);         // NTP sync required
    snap.msg_state = 1;  // ok
    
    // Отправить master'у
    uint8_t masterMac[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};  // из slave config.h
    esp_now_send(masterMac, (uint8_t*)&snap, sizeof(snap));
}
```

## Вывод на Master (RPi)

Master будет выводить JSON строку на UART в этом формате:

```json
{"type":"snap","id":1,"ip":"192.168.1.100","rfid":"E20068151234ABCD","beam":0,"state":2,"weight":14250.5,"ts":1746492180}
```

RPi может теперь читать IP адрес каждого slave из этого поля.

## Проверка

Телнет на master'е:

```
telnet <MASTER_IP> 23
> last 1
{"type":"snap","id":1,"ip":"192.168.1.100",...}
```

Если `"ip":"0.0.0.0"` — slave не получил IP адрес. Проверить WiFi подключение.
