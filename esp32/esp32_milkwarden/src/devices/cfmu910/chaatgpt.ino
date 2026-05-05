#include <HardwareSerial.h>

#define RX2_PIN 16
#define TX2_PIN 17

HardwareSerial RFIDSerial(2);

uint16_t crc16(uint8_t *buf, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc ^= buf[i];
    for (int j = 0; j < 8; j++)
      crc = (crc & 1) ? (crc >> 1) ^ 0x8408 : (crc >> 1);
  }
  return crc;
}

void sendCmd(uint8_t addr, uint16_t cmd, uint8_t *data, uint8_t dataLen) {
  uint8_t buf[64];
  uint8_t idx = 0;
  buf[idx++] = 0xCF;
  buf[idx++] = addr;
  buf[idx++] = cmd >> 8;
  buf[idx++] = cmd & 0xFF;
  buf[idx++] = dataLen;
  for (int i = 0; i < dataLen; i++) buf[idx++] = data[i];
  uint16_t c = crc16(buf, idx);
  buf[idx++] = c >> 8;
  buf[idx++] = c & 0xFF;
  RFIDSerial.write(buf, idx);
}

// Парсим один пакет из потока данных
// Формат: CF 00 CMD(2) LEN STATUS [RSSI(2) ANT EPC_LEN EPC(...)] CRC(2)
bool parsePacket(uint8_t *buf, uint8_t len, uint8_t *epcOut, uint8_t &epcLen, int16_t &rssi) {
  if (len < 8) return false;
  if (buf[0] != 0xCF) return false;

  uint8_t status = buf[5];
  if (status != 0x00) return false; // только успешные пакеты с тегом

  // RSSI = signed int16, единица 0.1 дБм
  rssi = (int16_t)((buf[6] << 8) | buf[7]);

  // buf[8] = Antenna
  epcLen = buf[9]; // длина EPC в байтах

  if (10 + epcLen + 2 > len) return false;

  memcpy(epcOut, buf + 10, epcLen);
  return true;
}

void waitForBoot() {
  String acc = "";
  uint32_t t = millis();
  while (millis() - t < 8000) {
    if (RFIDSerial.available()) {
      char c = RFIDSerial.read();
      acc += c;
      t = millis();
      if (acc.endsWith("ConfigReader OK\n")) {
        delay(100);
        return;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);
  RFIDSerial.begin(115200, SERIAL_8N1, RX2_PIN, TX2_PIN);

  Serial.println("Загрузка модуля...");
  waitForBoot();
  Serial.println("Готов!\n");

  // Инициализация
  sendCmd(0xFF, 0x0050, NULL, 0);
  delay(500);
  while (RFIDSerial.available()) RFIDSerial.read();
}

void loop() {
  // Запуск инвентаризации на 2 сек
  uint8_t invData[] = {0x00, 0x00, 0x00, 0x00, 0x02};
  sendCmd(0xFF, 0x0001, invData, 5);

  // Читаем поток ответов в течение 3 сек
  uint8_t stream[1024];
  uint16_t streamLen = 0;
  uint32_t t = millis();

  while (millis() - t < 3000) {
    if (RFIDSerial.available()) {
      stream[streamLen++] = RFIDSerial.read();
      t = millis();
      if (streamLen >= sizeof(stream) - 1) break;
    }
  }

  // Ищем пакеты в потоке по заголовку 0xCF
  int found = 0;
  for (int i = 0; i < streamLen - 1; i++) {
    if (stream[i] == 0xCF && stream[i+1] == 0x00) {
      // Размер пакета: HEAD(1)+ADDR(1)+CMD(2)+LEN(1)+DATA(LEN)+CRC(2)
      if (i + 5 >= streamLen) break;
      uint8_t pktLen = stream[i + 4]; // LEN
      uint8_t totalSize = 1 + 1 + 2 + 1 + pktLen + 2;

      if (i + totalSize > streamLen) break;

      uint8_t epc[32];
      uint8_t epcLen = 0;
      int16_t rssi = 0;

      if (parsePacket(stream + i, totalSize, epc, epcLen, rssi)) {
        Serial.print("EPC: ");
        for (int j = 0; j < epcLen; j++) Serial.printf("%02X ", epc[j]);
        Serial.printf(" | RSSI: %.1f dBm\n", rssi / 10.0);
        found++;
      }

      i += totalSize - 1; // перешагиваем пакет
    }
  }

  if (found == 0) {
    Serial.println("Тег не найден");
  } else {
    Serial.printf("Всего считываний: %d\n", found);
  }

  Serial.println("─────────────────────");
  delay(500);
}
