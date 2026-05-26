#pragma once

#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — sensors.h
//  SHT35 (I2C) + MAX31865/PT100 (SPI)
// ─────────────────────────────────────────────

struct SensorData {
    float temp_pt100;     // °C — MAX31865 / PT100
    float temp_sht35;     // °C — SHT35
    float humidity;       // %  — SHT35
    bool  pt100_ok;       // false si hay fault en MAX31865
    bool  sht35_ok;       // false si no responde por I2C
    uint8_t pt100_fault;  // código de fault (0 = sin error)
};

// Inicializa ambos sensores. Llama una sola vez en setup().
// Retorna true si ambos responden correctamente.
bool sensors_init();

// Lee ambos sensores y rellena `out`.
// Siempre escribe en `out`; verifica los campos _ok para validar.
void sensors_read(SensorData &out);

// Imprime por Serial el detalle del fault del MAX31865.
void sensors_print_pt100_fault(uint8_t fault);
