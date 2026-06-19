#include <Wire.h>
#include <VL53L0X.h>
#include <LiquidCrystal_I2C.h>

VL53L0X sensor;
LiquidCrystal_I2C lcd(0x27, 16, 2); 

#define XSHUT_PIN  4   
#define INT_PIN    19  
#define BUTTON_PIN 18 // Кнопка подсветки
#define BAT_PIN    34 // Пин замера батареи

const int FILTER_WINDOW = 5; 
uint16_t distanceBuffer[FILTER_WINDOW];

bool backlightOn = false;             
unsigned long backlightTimer = 0;     
const unsigned long displayTimeout = 10000; // 10 секунд

bool lastButtonState = HIGH;
int batCharge = 0; // Переменная теперь хранит последнее замеренное значение

// Функция ОДИНОЧНОГО замера батареи (вызывается только при клике)
int readBatteryPercentage() {
  long sumADC = 0;
  for (int i = 0; i < 15; i++) { // 15 замеров для максимальной точности
    sumADC += analogRead(BAT_PIN);
    delay(1);
  }
  int rawADC = sumADC / 15;

  float pinVoltage = (rawADC / 4095.0) * 3.3;
  float batteryVoltage = pinVoltage * 2.0; // Учитываем делитель 1:1

  // Выводим отладочную инфу ОДИН РАЗ за нажатие
  Serial.print("\n--- ЗАМЕР БАТАРЕИ --- ");
  Serial.print("ADC: "); Serial.print(rawADC);
  Serial.print(" | Напряжение: "); Serial.print(batteryVoltage); Serial.println("V");

  // Скорректированная формула: 4.15V = 100%, а 3.4V = 0%.
  // Теперь 3.7V покажет около 40-45%, что соответствует реальности для лития.
  int percentage = map(batteryVoltage * 100, 310, 415, 0, 100);
  
  if (percentage > 100) percentage = 100;
  if (percentage < 0)   percentage = 0;
  
  return percentage;
}

void setup() {
  Serial.begin(115200);
  
  pinMode(XSHUT_PIN, OUTPUT);
  pinMode(INT_PIN, INPUT_PULLUP);
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  analogReadResolution(12);

  digitalWrite(XSHUT_PIN, LOW);
  delay(200); 

  Wire.begin(21, 22); 
  Wire.setClock(100000); 
  delay(100); 

  // ТЕСТ ПРИ СТАРТЕ
  lcd.init(); 
  lcd.backlight(); 
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("System Start...");
  
  // Делаем один замер при старте, чтобы экран не был пустым
  batCharge = readBatteryPercentage(); 
  
  delay(1500);     
  lcd.noBacklight(); 
  lcd.clear();

  digitalWrite(XSHUT_PIN, HIGH);
  delay(200); 

  if (!sensor.init()) {
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Sensor Error!");
    while (1);
  }

  uint16_t initialDist = sensor.readRangeSingleMillimeters();
  for (int i = 0; i < FILTER_WINDOW; i++) {
    distanceBuffer[i] = initialDist;
  }
}

void loop() {
  // 1. ОПРОС КНОПКИ
  int btnState = digitalRead(BUTTON_PIN);

  // Ловим момент именно НАЖАТИЯ (переход из HIGH в LOW)
  if (btnState == LOW && lastButtonState == HIGH) { 
    backlightTimer = millis(); 
    
    // КРИТИЧЕСКИЙ ТОЧКА: Замеряем батарею ТОЛЬКО здесь (1 раз за клик!)
    batCharge = readBatteryPercentage(); 
    
    if (!backlightOn) {
      lcd.backlight(); 
      backlightOn = true;
    }
  }
  lastButtonState = btnState; // Запоминаем состояние кнопки

  // 2. ПРОВЕРКА ТАЙМЕРА АВТОВЫКЛЮЧЕНИЯ
  if (backlightOn && (millis() - backlightTimer >= displayTimeout)) {
    lcd.noBacklight(); 
    lcd.clear();       
    backlightOn = false;
  }

  // 3. ОПРОС ДАТЧИКА РАССТОЯНИЯ (работает в фоне)
  uint16_t rawDistance = sensor.readRangeSingleMillimeters();

  if (sensor.timeoutOccurred()) {
    if (backlightOn) {
      lcd.setCursor(0, 0);
      lcd.print("TIMEOUT ERROR   ");
    }
    return;
  }

  bool outOfRange = false;
  if (rawDistance > 2000 || rawDistance == 0) {
    rawDistance = 2000; 
    outOfRange = true;
  }
  
  addValueToBuffer(rawDistance);
  uint16_t filteredDistance = getMedian();

  // 4. ВЫВОД НА ЭКРАН (выводит сохраненное значение batCharge)
  if (backlightOn) {
    lcd.setCursor(0, 0); 
    lcd.print("Battery: ");
    lcd.print(batCharge);
    lcd.print("%     "); 

    lcd.setCursor(0, 1); 
    if (outOfRange) {
      lcd.print("Dist: Out of ran"); 
    } else {
      lcd.print("Dist: ");
      lcd.print(filteredDistance);
      lcd.print(" mm    "); 
    }
  }

  // В Serial теперь выводим только статус кнопки и расстояния, без спама батареи
  Serial.print("Btn:");       Serial.print(btnState);
  Serial.print(",Raw:");       Serial.print(rawDistance);
  Serial.print(",Filtered:"); Serial.println(filteredDistance);

  delay(50); 
}

void addValueToBuffer(uint16_t value) {
  for (int i = 0; i < FILTER_WINDOW - 1; i++) distanceBuffer[i] = distanceBuffer[i + 1];
  distanceBuffer[FILTER_WINDOW - 1] = value;
}

uint16_t getMedian() {
  uint16_t sorted[FILTER_WINDOW];
  for (int i = 0; i < FILTER_WINDOW; i++) sorted[i] = distanceBuffer[i];
  for (int i = 0; i < FILTER_WINDOW - 1; i++) {
    for (int j = 0; j < FILTER_WINDOW - i - 1; j++) {
      if (sorted[j] > sorted[j + 1]) {
        uint16_t temp = sorted[j];
        sorted[j] = sorted[j + 1];
        sorted[j + 1] = temp;
      }
    }
  }
  return sorted[FILTER_WINDOW / 2];
}