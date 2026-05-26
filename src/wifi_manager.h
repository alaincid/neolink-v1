#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — wifi_manager.h
//  STA mode: conecta a WiFi guardado en NVS
//  AP  mode: punto de acceso "NEOLINK-SETUP"
//            cuando no hay credenciales o falló conexión
// ─────────────────────────────────────────────

#define WIFI_AP_SSID            "NEOLINK-SETUP"
#define WIFI_AP_PASS            "neolink1234"
#define WIFI_CONNECT_TIMEOUT_MS 15000

enum WifiConnState {
    WIFI_IDLE,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_AP_MODE
};

struct WifiInfo {
    WifiConnState state;
    int8_t  rssi;
    char    ip[20];
    char    ssid[33];
};

void     wifi_manager_begin();
void     wifi_manager_loop();         // llamar periódicamente
WifiInfo wifi_get_info();
bool     wifi_is_sta_connected();

// Guarda credenciales y dispara reconexión
void wifi_save_and_reconnect(const char *ssid, const char *pass);

// HTTP POST al backend usando WiFi (devuelve código HTTP, -2 si TCP falla, -3 si no hay WiFi)
int wifi_http_post(float temp_pt100, float temp_sht, float hum,
                   uint8_t bat, bool alarm);
