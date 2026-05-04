#include <HardwareSerial.h>

#define RXD2 16
#define TXD2 17

HardwareSerial rfidSerial(2);

enum Mode { IDLE, SINGLE_BATCH, CONTINUOUS };
Mode currentMode = IDLE;
int tagCount = 0;
const int MAX_TAGS_PER_BATCH = 10;

unsigned int uiCrc16Cal(unsigned char const *pucY, unsigned char ucX) {
  unsigned short int uiCrcValue = 0xFFFF;
  for (unsigned char i = 0; i < ucX; i++) {
    uiCrcValue = uiCrcValue ^ *(pucY + i);
    for (unsigned char j = 0; j < 8; j++) {
      if (uiCrcValue & 0x0001) uiCrcValue = (uiCrcValue >> 1) ^ 0x8408;
      else uiCrcValue = (uiCrcValue >> 1);
    }
  }
  return uiCrcValue;
}

void sendCommand(uint8_t addr, uint16_t cmd, uint8_t len, uint8_t* data) {
  uint8_t frame[64]; 
  frame[0] = 0xCF; frame[1] = addr;
  frame[2] = (cmd >> 8); frame[3] = (cmd & 0xFF);
  frame[4] = len;
  for (int i = 0; i < len; i++) frame[5 + i] = data[i];
  unsigned int crc = uiCrc16Cal(frame, 5 + len);
  frame[5 + len] = (crc >> 8); frame[6 + len] = (crc & 0xFF);
  rfidSerial.write(frame, 7 + len);
}

void stopInventory() {
  sendCommand(0xFF, 0x0002, 0, NULL);
  currentMode = IDLE;
  Serial.println(">>> STOPPED");
}

void startInventory() {
  uint8_t invData[] = {0x00, 0x00, 0x00, 0x00, 0x00}; 
  sendCommand(0xFF, 0x0001, 5, invData);
}

void setup() {
  Serial.begin(115200);
  rfidSerial.begin(115200, SERIAL_8N1, RXD2, TXD2); 
  delay(1000);
  Serial.println("--- UHF System Ready ---");
}

void loop() {
  // 1. Обработка входящих команд
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();
    if (input == "r1") { 
      tagCount = 0; 
      currentMode = SINGLE_BATCH; 
      Serial.println("Mode: Batch 10");
      startInventory(); 
    } 
    else if (input == "r2") { 
      currentMode = CONTINUOUS; 
      Serial.println("Mode: Continuous");
      startInventory(); 
    } 
    else if (input == "s") { 
      stopInventory(); 
    }
    else if (input.startsWith("p")) {
      int val = input.substring(1).toInt();
      if (val >= 5 && val <= 33) {
        uint8_t pwrData[] = {(uint8_t)val, 0x00}; 
        sendCommand(0xFF, 0x0053, 2, pwrData);
        Serial.printf("Power set: %d dBm\n", val);
      }
    }
  }

  // 2. Логика паузы для r1
  if (currentMode == SINGLE_BATCH && tagCount >= MAX_TAGS_PER_BATCH) {
    stopInventory();
    Serial.println("Wait 1s...");
    delay(1000); 
    tagCount = 0;
    startInventory();
  }

  // 3. Чтение данных (Упрощенный парсинг)
  while (rfidSerial.available() > 0) {
    uint8_t header = rfidSerial.read();
    
    if (header == 0xCF) { // Нашли начало кадра
      delay(5); // Даем время долететь остальным байтам пакета
      uint8_t addr = rfidSerial.read();
      uint8_t cmdH = rfidSerial.read();
      uint8_t cmdL = rfidSerial.read();
      uint8_t len  = rfidSerial.read();
      
      uint8_t data[len + 2]; // Данные + CRC
      rfidSerial.readBytes(data, len + 2);
      
      if (cmdL == 0x01 && data[0] == 0x00) { // Ответ на опрос, статус ОК
        tagCount++;
        int8_t rssi = (int8_t)data[1];
        Serial.printf("[%d] RSSI: %d | EPC: ", tagCount, rssi);
        
        // EPC начинается с data[4]
        for (int i = 4; i < len; i++) {
          if (data[i] < 0x10) Serial.print("0");
          Serial.print(data[i], HEX);
          Serial.print(" ");
        }
        Serial.println();
      }
    }
  }
}
