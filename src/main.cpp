#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "modem_sim800.h"

// ─────────────────────────────────────────────
//  NEOLINK V1 — main.cpp
//  Fase 2: Sensores + SIM800L + HTTP POST
// ─────────────────────────────────────────────

// Cuántas lecturas acumular antes de enviar por GPRS
// (evita abrir/cerrar HTTP en cada lectura)
#define POST_EVERY_N_READINGS  5

static SensorData  data;
static ModemStatus modem_status;
static uint32_t    last_read_ms  = 0;
static uint8_t     reading_count = 0;
static bool        modem_ready   = false;

// Batería fija en 100% hasta implementar ADC en Fase 4
static uint8_t battery_pct() { return 100; }

// ─────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(2000);

    Serial.println("=============================");
    Serial.println("  NEOLINK V1 — Fase 2 boot  ");
    Serial.println("=============================");
    Serial.printf("  Device ID : %s\n", DEVICE_ID);
    Serial.println("-----------------------------");

    // Sensores
    bool sensors_ok = sensors_init();
    if (!sensors_ok) {
        Serial.println("[MAIN] Advertencia: sensor(es) no inicializaron");
    }

    // SIM800L
    if (modem_init()) {
        modem_ready = modem_connect();
        if (modem_ready) {
            Serial.println("[MAIN] GPRS activo — listo para enviar datos");
        } else {
            Serial.println("[MAIN] GPRS no disponible — modo solo lectura local");
        }
    } else {
        Serial.println("[MAIN] SIM800L no responde — modo solo lectura local");
    }

    Serial.println("[MAIN] Setup completo.\n");
}

// ─────────────────────────────────────────────
//  loop
// ─────────────────────────────────────────────

void loop() {
    uint32_t now = millis();

    if (now - last_read_ms >= SENSOR_READ_INTERVAL_MS) {
        last_read_ms = now;
        reading_count++;

        sensors_read(data);

        // ── Imprime lectura ──────────────────
        Serial.println("─────────────────────────────");
        Serial.printf("  T+%lu ms  (#%u)\n", now, reading_count);

        if (data.pt100_ok) {
            Serial.printf("  PT100  : %.2f C\n", data.temp_pt100);
        } else {
            Serial.printf("  PT100  : FAULT (0x%02X)\n", data.pt100_fault);
        }

        if (data.sht35_ok) {
            Serial.printf("  SHT35  : %.2f C  %.1f %%RH\n",
                          data.temp_sht35, data.humidity);
        } else {
            Serial.println("  SHT35  : ERROR");
        }

        // Estado del módem
        ModemState ms = modem_get_state();
        const char *modem_str =
            ms == ModemState::GPRS_UP    ? "GPRS_UP" :
            ms == ModemState::REGISTERED ? "REGISTRADO" :
            ms == ModemState::ERROR      ? "ERROR" : "OFFLINE";
        Serial.printf("  GSM    : %s\n", modem_str);
        Serial.println("─────────────────────────────");

        // ── Envía POST cada N lecturas ───────
        if (modem_ready && reading_count % POST_EVERY_N_READINGS == 0) {

            bool alarm = (data.pt100_ok && data.temp_pt100 > TEMP_ALARM_HIGH) ||
                         (data.pt100_ok && data.temp_pt100 < TEMP_ALARM_LOW);

            float t_pt100 = data.pt100_ok ? data.temp_pt100 : -999.0f;
            float t_sht   = data.sht35_ok ? data.temp_sht35 : -999.0f;
            float hum     = data.sht35_ok ? data.humidity    : -999.0f;

            Serial.println("[MAIN] Enviando POST al backend...");
            int http_code = modem_http_post(t_pt100, t_sht, hum,
                                            battery_pct(), alarm);

            if (http_code == 200 || http_code == 201) {
                Serial.printf("[MAIN] POST OK (HTTP %d)\n", http_code);
            } else if (http_code == -2) {
                // -2 = TCP falló — reconectar GPRS
                Serial.println("[MAIN] TCP falló — reconectando GPRS...");
                modem_ready = modem_connect();
            } else {
                // -1 = HTTP ok pero respuesta no parseada — no reconectar
                Serial.printf("[MAIN] POST status %d — reintentando proxima vez\n",
                              http_code);
            }
        }
    }
}
