# NEOLINK V1 — CONTEXTO COMPLETO

## HARDWARE CONFIRMADO

### ESP32-S3 Waveshare Touch LCD 3.5

### PINOUT FINAL

#### SHT35 I2C:
- GPIO47 = SDA
- GPIO42 = SCL
- VCC = 3.3V
- GND = GND

#### MAX31865 SPI:
- GPIO38 = CS
- GPIO39 = CLK/SCK
- GPIO40 = SDI/MOSI
- GPIO41 = SDO/MISO
- VIN/3V3 = 3.3V
- GND = GND

#### SIM800L UART:
- ESP32 RX = GPIO21
- ESP32 TX = GPIO45
- Baudrate = 9600
- VCC = LiPo 3.7V directo
- GND = común con ESP32
- NO usar GPIO43 ni GPIO44

#### Pantalla:
- 3.5" touch integrada en la board Waveshare
- Usar librería oficial Waveshare o LVGL

## ALIMENTACIÓN
- Sensores: 3.3V
- SIM800L: LiPo 3.7V directo (NO 3.3V, NO 5V)
- Picos de corriente SIM800L: ~2A
- Capacitor recomendado: 1000uF cerca del SIM800L
- Todo comparte GND

## FIRMWARE STACK
- PlatformIO + Arduino framework
- Librerías:
  - Adafruit SHT31
  - Adafruit MAX31865
  - ArduinoJson
  - TFT_eSPI o LVGL
  - AsyncTCP
  - ESPAsyncWebServer

## ESTRUCTURA DEL PROYECTO
src/
  main.cpp
  config.h
  sensors.h
  sensors.cpp
  modem_sim800.h
  modem_sim800.cpp
  ui.h
  ui.cpp

## FUNCIONES DEL FIRMWARE

### Sensores
- Leer SHT35: temperatura ambiente + humedad
- Leer MAX31865/PT100: temperatura principal
- Detectar errores de sensor
- Guardar última lectura válida

### Pantalla
- Título: NEOLINK
- Temp PT100 grande
- Temp/Humedad SHT35 pequeña
- Estado GSM
- Batería
- Hora
- Gráfica de últimas lecturas
- Estados: NORMAL, WARNING, ALARM, SENSOR ERROR, GSM OFFLINE

### Historial local
- Buffer circular en RAM
- Últimas 120 lecturas
- Campos: timestamp, temp_pt100, temp_sht35, humidity, gsm_status

### SIM800L — APN Telcel
- APN: internet.itelcel.com
- User: webgprs
- Pass: webgprs2002

### HTTP POST al backend
POST http://EC2_PUBLIC_IP:8080/api/readings
{
  "device_id": "neolink-v1-001",
  "token": "DEVICE_SECRET",
  "temp_pt100": 25.0,
  "temp_sht35": 24.3,
  "humidity": 44.2,
  "battery": 87,
  "rssi": 18,
  "alarm": false
}

## BACKEND AWS EC2
- Ubuntu 22.04
- Node.js 20
- Express
- SQLite
- PM2
- Puerto 8080

### Endpoints:
- POST /api/readings
- GET /api/latest
- GET /api/history?device_id=neolink-v1-001&limit=200
- GET /dashboard

## FASES DE DESARROLLO
1. Fase 1: Sensores + pantalla local
2. Fase 2: SIM800L AT + GPRS
3. Fase 3: Backend EC2 + POST
4. Fase 4: Alarmas + OTA + buffer offline

## PUNTOS CRÍTICOS
- NO usar GPIO43/GPIO44 para SIM800L
- SIM800L NO se alimenta de 3.3V ni 5V
- Agregar capacitor 1000uF al SIM800L
- Si PT100 no conectada, MAX31865 dará fault
- SIM Telcel confirmada y registrada en red