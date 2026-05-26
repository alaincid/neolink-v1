#pragma once

// ─────────────────────────────────────────────
//  NEOLINK V1 — Configuración central de pines
//  Hardware: ESP32-S3 Waveshare Touch LCD 3.5"
// ─────────────────────────────────────────────

// ── SHT35 — I2C ──────────────────────────────
#define SHT35_SDA_PIN   47
#define SHT35_SCL_PIN   42
#define SHT35_I2C_ADDR  0x44   // ADDR pin a GND → 0x44; a VCC → 0x45

// ── MAX31865 — SPI ───────────────────────────
#define MAX31865_CS_PIN   38
#define MAX31865_CLK_PIN  39
#define MAX31865_MOSI_PIN 40
#define MAX31865_MISO_PIN 41

// ── SIM800L — UART (Fase 2) ──────────────────
//  IMPORTANTE: NO usar GPIO43 ni GPIO44
#define SIM800L_RX_PIN   21
#define SIM800L_TX_PIN   45
#define SIM800L_BAUDRATE 9600

// ── Serial de depuración ─────────────────────
#define SERIAL_BAUDRATE  115200

// ── Intervalos de lectura ────────────────────
#define SENSOR_READ_INTERVAL_MS 2000   // cada 2 segundos

// ── MAX31865 — tipo de RTD ───────────────────
// PT100 de 2 hilos → MAX31865_2WIRE
// PT100 de 3 hilos → MAX31865_3WIRE
// PT100 de 4 hilos → MAX31865_4WIRE
#define PT100_WIRES      MAX31865_4WIRE
#define PT100_REF_R      430.0f   // resistencia de referencia en ohms (R_REF del módulo)
#define PT100_NOMINAL_R  100.0f   // PT100 = 100 Ω a 0 °C

// ── Umbrales de alarma (Fase 4) ──────────────
#define TEMP_ALARM_HIGH  80.0f   // °C
#define TEMP_ALARM_LOW  -10.0f   // °C

// ── Identificación del dispositivo ───────────
#define DEVICE_ID        "neolink-v1-001"
#define DEVICE_TOKEN     "DEVICE_SECRET"   // reemplazar antes de producción

// ── Backend (Fase 3) ─────────────────────────
#define BACKEND_HOST     "3.222.162.34"
#define BACKEND_PORT     8080
#define BACKEND_PATH     "/api/readings"

// ── Display ST7796 — SPI ─────────────────────
//  Waveshare ESP32-S3-Touch-LCD-3.5
//  CS y RST se manejan vía TCA9554 (I/O expander)
#define LCD_SCLK_PIN     5
#define LCD_MOSI_PIN     1
#define LCD_MISO_PIN     2
#define LCD_DC_PIN       3
#define LCD_BL_PIN       6    // Backlight PWM
#define LCD_BL_CHANNEL   1    // LEDC canal (0 reservado por si acaso)
#define LCD_BL_DUTY      200  // 0-255 (200 ≈ 78% brillo)
#define LCD_WIDTH        320
#define LCD_HEIGHT       480

// ── I2C interna del board ─────────────────────
//  Misma para TCA9554, FT6336 touch, PCF85063 RTC
//  (SHT35 usa Wire en GPIO47/42 — esta usa Wire1)
#define BOARD_I2C_SDA    8
#define BOARD_I2C_SCL    7

// ── TCA9554 I/O Expander ─────────────────────
#define TCA9554_ADDR     0x20
#define TCA_LCD_RST_PIN  1    // P1 → LCD RST
#define TCA_LCD_CS_PIN   2    // P2 → LCD CS (active LOW)
