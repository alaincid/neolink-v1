#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────
//  NEOLINK V1 — power_manager.h
//  AXP2101 PMIC — Waveshare ESP32-S3-Touch-LCD-3.5
//  I2C: Wire1 (SDA=GPIO8, SCL=GPIO7), addr=0x34
// ─────────────────────────────────────────────

// Inicializa el AXP2101 y enciende los rails necesarios para la pantalla.
// Debe llamarse ANTES de display_init().
// Retorna true si el chip respondió.
bool power_init();

// Porcentaje de batería (0–100). Retorna 100 si el chip no está disponible.
uint8_t power_batt_pct();
