# Блок-схемы интеллектуального модуля Milkwarden

Ниже приведены шесть схем, описывающих ключевые части системы. Каждая схема сопровождается пояснением о назначении и месте интеграции в общий проект.

---

## 1. Общая архитектура
Схема демонстрирует основной поток данных: от сенсорных узлов (ESP32 и Arduino/AD7797) через Raspberry Pi к серверу, БД и интерфейсам оператора. Она помогает архитекторам и разработчикам видеть полный путь телеметрии и быстро соотнести элементы с файлами `esp32_sensor_reader.py`, `ad7797_reader.ino`, `raspberry_data_router.py` и `server_api.py`.

```mermaid
flowchart LR
    subgraph Field["Уровень устройств"]
        RPI[Raspberry Pi<br/>центральный шлюз]
        ESP[ESP32-S3<br/>сенсорный концентратор]
        AD[Arduino/ESP32-C3<br/>+ AD7797]
    end
    subgraph Cloud["Серверный уровень"]
        API[FastAPI/Django API]
        DB[(PostgreSQL)]
        UI[Веб-панель / Nextion]
    end
    ESP -->|Сбор данных| RPI
    AD -->|Вес| RPI
    RPI -->|Идентификация + агрегация| API
    API -->|Запись| DB
    API -->|API/WebSocket| UI
    UI -->|Обратная связь| RPI
```

---

## 2. Алгоритм идентификации коровы
Схема показывает, как данные RFID и машинного зрения объединяются для подтверждения личности коровы. Она интегрируется с серверной БД (`cow`, `vision_matches`) и используется Raspberry для принятия решения о запуске сессии.

```mermaid
flowchart TD
    Start((Начало)) --> RFID[Считать RFID EPC]
    RFID -->|Не найден| ErrorRFID[Ошибка / запрос повтора]
    RFID -->|Найден| Vision[Машинное зрение Jetson]
    Vision --> Compare{Совпадение с БД?}
    Compare -->|Да| Confirm[Создать сессию доения]
    Compare -->|Нет| Alert[Сигнал оператору\n+ блокировка поста]
    ErrorRFID --> End((Стоп))
    Alert --> End
    Confirm --> End
```

---

## 3. Алгоритм измерения веса
Поток повторяет шаги zeroing → filtering → calibration → transmission, что напрямую связано с реализацией в `ad7797_reader.ino`. Схема нужна инженерам по встраиваемым системам для проверки логики обработки тензодатчиков.

```plantuml
@startuml
start
:Инициализация SPI и AD7797;
:Zeroing (128 выборок);
:Калибровка известным весом;
repeat
  :Считать сырые данные;
  :Фильтр (moving average);
  :Конвертация в kg;
  if (Диапазон OK?) then (да)
    :Передать JSON вес на UART;
  else (нет)
    :Записать ошибку/пропустить;
  endif
repeat while (Сессия активна?) is (да)
stop
@enduml
```

---

## 4. Контроль качества молока
Эта диаграмма объединяет показания мутности, электропроводности и температуры и описывает принятие решения о тревоге. Она соотносится с `esp32_sensor_reader.py` и серверной логикой анализа качества.

```mermaid
stateDiagram-v2
    [*] --> Sampling: Съём показаний
    Sampling --> Normalization: Температурная компенсация
    Normalization --> Interpretation
    Interpretation -->|Порог превышен| Alert: Логирование + предупреждение
    Interpretation -->|Норма| Logging: Запись в БД
    Alert --> Logging
    Logging --> [*]
```

---

## 5. Обмен Raspberry ↔ ESP32 ↔ Server
Диаграмма описывает протокол обмена, формат JSON, таймауты и политику повторов для `raspberry_data_router.py`. Даже без запуска кода можно понять, как реализованы QoS и защитные механизмы.

```plantuml
@startuml
participant ESP32
participant Raspberry
participant Server

ESP32 -> Raspberry : UART JSON\n{"device_id":...}
activate Raspberry
Raspberry -> Raspberry : Проверка схемы / таймаут
Raspberry -> Server : HTTPS POST /api/add_sensor_data
activate Server
Server --> Raspberry : 201 Created / ошибки
deactivate Server
Raspberry -> ESP32 : ACK/NAK (опционально)
deactivate Raspberry
ESP32 -> ESP32 : Повтор при отсутствии ACK
@enduml
```

---

## 6. Интерфейс Nextion
Flowchart отражает взаимодействия оператора со стартовым меню, выбором коровы, просмотром параметров и обработкой ошибок. Она связана с данными, приходящими из `server_api.py` и `raspberry_data_router.py`, которые питают UI.

```mermaid
flowchart LR
    Start((Экран приветствия)) --> Menu{Стартовое меню}
    Menu -->|Выбор поста| CowList[Список коров]
    CowList -->|RFID подтвержден| Params[Параметры коровы\nвес, поток, качество]
    Params -->|Ошибки?| ErrorMenu{Ошибка?}
    ErrorMenu -->|Да| AlertScreen[Экран ошибок\nвакуум/качество]
    AlertScreen --> Menu
    ErrorMenu -->|Нет| Menu
    Menu -->|Настройки| Settings[Конфигурация сети]
    Settings --> Menu
```

---

## 7. Опрыскивание коровы до доения
Схема описывает гигиенический цикл после идентификации и до запуска измерений. Она встроена между блоками идентификации и измерения в архитектуре, а команды реализуются в Raspberry через расширение GPIO/RS-485. Логи операций идут в серверную БД для аудита.

```mermaid
flowchart TD
    Start((Идентификация завершена)) --> CheckIn{Корова на посту?}
    CheckIn -->|Нет| Abort[Отложить опрыскивание]
    CheckIn -->|Да| Precheck[Проверка уровня раствора]
    Precheck -->|Недостаточно| AlertOps[Сообщить оператору]
    Precheck -->|OK| Spray[Активировать форсунки\n3-5 секунд]
    Spray --> Monitor[Мониторинг давления и таймаутов]
    Monitor -->|OK| LogSpray[Записать событие в БД]
    Monitor -->|Ошибка| Fault[Вызвать сервисный режим]
    LogSpray --> Continue[Запуск сессии доения]
    Fault --> Continue
```

---

## Как открыть диаграммы
- **Mermaid**: в VS Code достаточно расширения “Markdown Preview Mermaid Support” и просмотра `flowcharts.md`; GitHub отображает диаграммы напрямую, Obsidian — через плагин “Mermaid”.
- **PlantUML**: в VS Code — расширение “PlantUML” + Java, затем команда `PlantUML: Preview Current Diagram`; в IntelliJ IDEA — плагин “PlantUML Integration”; в браузере — https://www.plantuml.com/plantuml.
- **PNG экспорт**: после генерации изображений их можно открыть стандартными просмотрщиками (Eye of GNOME, Windows Photos) или вставить в отчёт `docs/Отчёт_20.11.2025.docx`.
