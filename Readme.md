# Heap-Map — GPS Monitor Pro


## О проекте

Heap-Map — desktop-приложение для мониторинга и визуализации GPS-треков, параметров сотовой связи (LTE/GSM/WCDMA) и сетевого трафика. Данные собираются с мобильного устройства в реальном времени, сохраняются в PostgreSQL и отображаются на интерактивной карте с наложением тепловых карт сигнала.

**Ключевые возможности:**
- Приём данных с телефона через ZeroMQ в реальном времени
- Визуализация GPS-треков на OpenStreetMap с поддержкой тайлов
- Графики уровня сигнала (RSRP/RSSI) по каждой соте (PCI)
- Мониторинг трафика (RX/TX) в реальном времени
- Фильтрация данных по типу (локация, телеметрия, трафик) и технологии (LTE/GSM/WCDMA)
- Сохранение всех данных в PostgreSQL с автоматическим импортом JSON
- Встроенный HTTP-сервер для веб-доступа к данным
- Поддержка импорта существующих JSON-файлов

## Технологический стек

| Компонент       | Технология                |
|-----------------|---------------------------|
| Язык            | C++17                     |
| GUI             | Dear ImGui + ImPlot       |
| Графика         | OpenGL 3.3+               |
| База данных     | PostgreSQL 15             |
| Сеть            | ZeroMQ (REQ/REP), libcurl |
| Тайлы карт      | OpenStreetMap + STB Image |
| Сборка          | Makefile                  |
| Контейнеризация | Docker Compose            |

## Структура проекта
```
Heap-Map/
├── data/ # JSON-файлы с данными
│ ├── all_data.json # Полные данные
│ └── location_danil.json # Пример GPS-данных
├── database/ # Конфигурация БД
│ ├── docker-compose.yaml # PostgreSQL + pgAdmin
│ └── init.sql # Схема базы данных
├── include/ # Заголовочные файлы
├── src/ # Исходный код
│ ├── main.cpp # Точка входа
│ ├── server.cpp # ZeroMQ + HTTP сервер
│ ├── gui.cpp # ImGui интерфейс
│ ├── db_client.cpp # Клиент PostgreSQL
│ ├── heatmap.cpp # Отрисовка карты
│ ├── tile_manager.cpp # Загрузка тайлов OSM
│ ├── curl_client.cpp # HTTP-клиент (резерв)
│ └── test_client # Тестовый клиент
├── third-party/ # Внешние библиотеки
│ ├── imgui/ # Immediate Mode GUI
│ ├── implot/ # Графики
│ ├── json/ # nlohmann/json
│ └── stb/ # STB Image
├── imgui.ini # Настройки ImGui
├── Makefile # Система сборки
```

## Быстрый старт

### 1. Установка зависимостей

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y build-essential cmake libglfw3-dev libglew-dev \
    libpqxx-dev libcurl4-openssl-dev libzmq3-dev postgresql-server-dev-all
```

### 2. Запуск базы данных

```bash
cd database
docker-compose up -d
```

# Параметры подключения: 
Host: localhost, Port: 5434, DB: cellmap, User/Pass: postgres.

### 3. Сборка и запуск приложения

```bash
make clean
make run
```

### 3. Использование

# Интерфейс

|Вкладка	     |                      Описание                           |
|----------------|---------------------------------------------------------|
|Dashboard	     |  Общая статистика (количество записей, сот, точек)      |
|Heatmap         |	Интерактивная карта с точками GPS и наложением сигнала |
|Signal Graphs   |	Графики RSRP/RSSI по каждой соте (PCI)                 |
|Location Graphs |	Графики широты, долготы, высоты и точности             |
|Traffic Graphs  |	Графики RX/TX трафика в реальном времени               |
|Cell Info       |	Детальная информация по всем обнаруженным сотам        |
|Filters         |	Фильтрация входящих данных                             |

