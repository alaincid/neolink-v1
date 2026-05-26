#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — modem_sim800.h
//  SIM800L via UART2 (RX=GPIO21, TX=GPIO45)
//  APN Telcel: internet.itelcel.com
// ─────────────────────────────────────────────

enum class ModemState {
    OFFLINE,       // sin respuesta al AT
    REGISTERED,    // registrado en red GSM
    GPRS_UP,       // GPRS activo, IP asignada
    ERROR
};

struct ModemStatus {
    ModemState state;
    int8_t     rssi;          // dBm aproximado (-113 a -51), 0 = desconocido
    uint8_t    rssi_raw;      // valor crudo de AT+CSQ (0-31, 99=desconocido)
    bool       sim_present;
    char       ip[16];        // IP asignada por GPRS, ej: "10.0.0.1"
};

// Inicializa UART2 y verifica que el SIM800L responda.
// Retorna true si el módem responde al AT básico.
bool modem_init();

// Registra en red GSM y activa GPRS.
// Retorna true si GPRS quedó activo con IP.
bool modem_connect();

// Envía HTTP POST con las lecturas al backend.
// Retorna el HTTP status code (200 = OK), -1 si falla.
int modem_http_post(float temp_pt100, float temp_sht35,
                    float humidity,   uint8_t battery,
                    bool alarm);

// Desconecta GPRS y cierra sesión.
void modem_disconnect();

// Lee estado actual del módem (señal, registro, IP).
void modem_read_status(ModemStatus &out);

// Retorna el último estado conocido sin consultar al módem.
ModemState modem_get_state();
