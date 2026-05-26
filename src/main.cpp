#include <Arduino.h>
#include "config.h"
#include "sensors.h"
#include "modem_sim800.h"
#include "display.h"

// ─────────────────────────────────────────────
//  NEOLINK V1 — main.cpp
//  Fase 3: Sensores + SIM800L + HTTP POST + Display
// ─────────────────────────────────────────────

// Cuántas lecturas acumular antes de enviar por GPRS
#define POST_EVERY_N_READINGS  5

// Cada cuántas lecturas refrescar la pantalla
#define DISPLAY_EVERY_N_READINGS 1

static SensorData   data;
static ModemStatus  modem_status;
static DisplayData  disp;
static uint32_t     last_read_ms   = 0;
static uint8_t      reading_count  = 0;
static bool         modem_ready    = false;
static uint32_t     last_post_ms   = 0;   // millis() del último POST OK

// Batería fija en 100% hasta implementar AXP2101 en Fase 4
static uint8_t battery_pct() { return 100; }

// ─────────────────────────────────────────────
//  setup
// ─────────────────────────────────────────────

void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(2000);

    Serial.println("=============================");
    Serial.println("  NEOLINK V1 — boot          ");
    Serial.println("=============================");
    Serial.printf("  Device ID : %s\n", DEVICE_ID);
    Serial.println("-----------------------------");

    // Pantalla (primero para dar feedback visual inmediato)
    display_init();

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

        // ── Actualiza display ────────────────
        if (reading_count % DISPLAY_EVERY_N_READINGS == 0) {
            modem_read_status(modem_status);

            disp.temp_sht35       = data.temp_sht35;
            disp.humidity         = data.humidity;
            disp.sht35_ok         = data.sht35_ok;
            disp.rssi             = modem_status.rssi;
            disp.battery_pct      = battery_pct();
            disp.modem_connected  = modem_ready;
            disp.last_post_ms     = last_post_ms;

            display_update(disp);
        }

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
                last_post_ms = millis();
            } else if (http_code == -2) {
                Serial.println("[MAIN] TCP falló — reconectando GPRS...");
                modem_ready = modem_connect();
            } else {
                Serial.printf("[MAIN] POST status %d — reintentando proxima vez\n",
                              http_code);
            }
        }
    }
}
