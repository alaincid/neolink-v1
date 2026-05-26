#include <Arduino.h>
#include "config.h"
#include "sensors.h"

// ─────────────────────────────────────────────
//  NEOLINK V1 — main.cpp
//  Fase 1: Lectura de sensores por Serial
// ─────────────────────────────────────────────

static SensorData data;
static uint32_t   last_read_ms = 0;

// ─────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(1500);   // espera a que el USB-CDC esté listo

    Serial.println("=============================");
    Serial.println("  NEOLINK V1 — Fase 1 boot  ");
    Serial.println("=============================");
    Serial.printf("  Device ID : %s\n", DEVICE_ID);
    Serial.printf("  Read interval: %d ms\n", SENSOR_READ_INTERVAL_MS);
    Serial.println("-----------------------------");

    bool sensors_ok = sensors_init();

    if (!sensors_ok) {
        Serial.println("[MAIN] Advertencia: uno o más sensores no inicializaron.");
        Serial.println("[MAIN] Continuando de todas formas...");
    }

    Serial.println("[MAIN] Setup completo. Iniciando lecturas...\n");
}

// ─────────────────────────────────────────────
//  loop
// ─────────────────────────────────────────────

void loop() {
    uint32_t now = millis();

    if (now - last_read_ms >= SENSOR_READ_INTERVAL_MS) {
        last_read_ms = now;

        sensors_read(data);

        Serial.println("─────────────────────────────");
        Serial.printf("  T+%lu ms\n", now);

        // PT100 / MAX31865
        if (data.pt100_ok) {
            Serial.printf("  PT100  : %.2f °C\n", data.temp_pt100);
        } else {
            Serial.printf("  PT100  : FAULT (0x%02X)\n", data.pt100_fault);
        }

        // SHT35
        if (data.sht35_ok) {
            Serial.printf("  SHT35  : %.2f °C  %.1f %%RH\n",
                          data.temp_sht35, data.humidity);
        } else {
            Serial.println("  SHT35  : ERROR");
        }

        Serial.println("─────────────────────────────");
    }
}
