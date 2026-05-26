#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — config_portal.h
//  Servidor web embebido (puerto 80)
//  Accesible en modo AP (192.168.4.1) o STA (IP local)
//  Rutas:
//    GET  /             → Panel de config HTML
//    GET  /api/status   → JSON con lecturas en vivo
//    GET  /api/config   → JSON con configuración actual
//    POST /api/config/wifi   → Guardar SSID/pass
//    POST /api/config/device → Guardar nombre/servidor
//    POST /ota          → Actualizar firmware (.bin)
// ─────────────────────────────────────────────

struct PortalSensorData {
    float    temp_sht;
    float    humidity;
    bool     sht_ok;
    float    temp_pt100;
    bool     pt100_ok;
    int8_t   gsm_rssi;
    bool     gsm_connected;
    int8_t   wifi_rssi;
    bool     wifi_connected;
    uint8_t  battery_pct;
    uint32_t last_post_ms;
    uint32_t uptime_s;
};

void portal_begin();
void portal_loop();
void portal_update(const PortalSensorData &data);
