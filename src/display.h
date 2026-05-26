#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — display.h
//  Driver para Waveshare ESP32-S3-Touch-LCD-3.5
// ─────────────────────────────────────────────

struct DisplayData {
    // SHT35 (Card 1 — izquierda/derecha)
    float    temp_sht35;
    float    humidity;
    bool     sht35_ok;

    // PT100 / MAX31865 (Card 2 — dinámico)
    float    temp_pt100;
    bool     pt100_ok;

    // GSM
    int8_t   gsm_rssi;        // dBm, 0 = desconocido
    bool     gsm_connected;

    // WiFi
    int8_t   wifi_rssi;       // dBm, 0 = desconectado
    bool     wifi_connected;
    bool     wifi_ap_mode;    // true = modo AP de configuración

    // General
    uint8_t  battery_pct;
    uint32_t last_post_ms;    // millis() del último POST OK
    time_t   current_time;    // 0 = no sincronizado; >0 = tiempo real (NTP)
};

bool display_init();
void display_update(const DisplayData &d);
