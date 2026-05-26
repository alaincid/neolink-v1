#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — display.h
//  Driver para Waveshare ESP32-S3-Touch-LCD-3.5
//  ST7796 SPI + TCA9554 I/O expander
// ─────────────────────────────────────────────

struct DisplayData {
    float    temp_sht35;   // temperatura SHT35 (°C)
    float    humidity;     // humedad SHT35 (%RH)
    bool     sht35_ok;     // sensor válido

    int8_t   rssi;         // RSSI GSM (dBm, 0 = desconocido)
    uint8_t  battery_pct;  // batería 0-100 %
    bool     modem_connected;  // GPRS activo

    uint32_t last_post_ms; // millis() del último POST exitoso (0 = nunca)
};

bool display_init();
void display_update(const DisplayData &d);
