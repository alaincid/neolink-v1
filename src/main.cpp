#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "sensors.h"
#include "modem_sim800.h"
#include "display.h"
#include "preferences_store.h"
#include "wifi_manager.h"
#include "config_portal.h"

// ─────────────────────────────────────────────
//  NEOLINK V1 — main.cpp
//  Core 1: sensores + modem + display
//  Core 0: WiFi + portal web
// ─────────────────────────────────────────────

#define POST_EVERY_N_READINGS    5
#define DISPLAY_EVERY_N_READINGS 1

static SensorData   data;
static ModemStatus  modem_status;
static DisplayData  disp;
static uint32_t     last_read_ms  = 0;
static uint8_t      reading_count = 0;
static bool         modem_ready   = false;
static uint32_t     last_post_ms  = 0;

static uint8_t battery_pct() { return 100; }

// ─────────────────────────────────────────────
//  Task Core 0: WiFi + web portal
// ─────────────────────────────────────────────
static void wifi_portal_task(void *pvParameters) {
    wifi_manager_begin();

    // Espera breve para que el WiFi inicie antes de lanzar el servidor
    vTaskDelay(pdMS_TO_TICKS(2000));
    portal_begin();

    for (;;) {
        wifi_manager_loop();
        portal_loop();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// ─────────────────────────────────────────────
//  setup  (Core 1)
// ─────────────────────────────────────────────
void setup() {
    Serial.begin(SERIAL_BAUDRATE);
    delay(1500);

    Serial.println("=============================");
    Serial.println("  NEOLINK V1 — boot          ");
    Serial.println("=============================");
    Serial.printf("  Device ID : %s\n", DEVICE_ID);
    Serial.println("-----------------------------");

    // NVS (necesario antes del WiFi manager)
    prefs_init();

    // Display
    display_init();

    // Sensores
    if (!sensors_init()) {
        Serial.println("[MAIN] Advertencia: sensor(es) no inicializaron");
    }

    // SIM800L
    if (modem_init()) {
        modem_ready = modem_connect();
        Serial.println(modem_ready
            ? "[MAIN] GPRS activo"
            : "[MAIN] GPRS no disponible");
    } else {
        Serial.println("[MAIN] SIM800L no responde — solo lectura local");
    }

    // WiFi + portal en Core 0 (stack 8K — suficiente para WebServer)
    xTaskCreatePinnedToCore(
        wifi_portal_task,
        "wifi_portal",
        8192,
        nullptr,
        1,
        nullptr,
        0   // Core 0
    );

    Serial.println("[MAIN] Setup completo.\n");
}

// ─────────────────────────────────────────────
//  loop  (Core 1)
// ─────────────────────────────────────────────
void loop() {
    uint32_t now = millis();

    if (now - last_read_ms >= SENSOR_READ_INTERVAL_MS) {
        last_read_ms = now;
        reading_count++;

        sensors_read(data);

        // ── Log serial ───────────────────────
        Serial.println("─────────────────────────────");
        Serial.printf("  T+%lu ms  (#%u)\n", now, reading_count);
        if (data.pt100_ok)
            Serial.printf("  PT100  : %.2f C\n",   data.temp_pt100);
        else
            Serial.printf("  PT100  : FAULT (0x%02X)\n", data.pt100_fault);
        if (data.sht35_ok)
            Serial.printf("  SHT35  : %.2f C  %.1f %%RH\n",
                           data.temp_sht35, data.humidity);
        else
            Serial.println("  SHT35  : ERROR");

        // ── Display ──────────────────────────
        if (reading_count % DISPLAY_EVERY_N_READINGS == 0) {
            modem_read_status(modem_status);
            WifiInfo winfo = wifi_get_info();

            disp.temp_sht35      = data.temp_sht35;
            disp.humidity        = data.humidity;
            disp.sht35_ok        = data.sht35_ok;
            disp.temp_pt100      = data.temp_pt100;
            disp.pt100_ok        = data.pt100_ok;
            disp.gsm_rssi        = modem_status.rssi;
            disp.gsm_connected   = modem_ready;
            disp.wifi_rssi       = winfo.rssi;
            disp.wifi_connected  = (winfo.state == WIFI_CONNECTED);
            disp.wifi_ap_mode    = (winfo.state == WIFI_AP_MODE);
            disp.battery_pct     = battery_pct();
            disp.last_post_ms    = last_post_ms;
            disp.current_time    = time(nullptr);  // 0 hasta NTP sync

            display_update(disp);
        }

        // ── Portal web (datos en vivo) ────────
        {
            PortalSensorData ps;
            ps.temp_sht    = data.temp_sht35;
            ps.humidity    = data.humidity;
            ps.sht_ok      = data.sht35_ok;
            ps.temp_pt100  = data.temp_pt100;
            ps.pt100_ok    = data.pt100_ok;
            ps.gsm_rssi    = modem_status.rssi;
            ps.gsm_connected = modem_ready;
            ps.battery_pct = battery_pct();
            ps.last_post_ms = last_post_ms;
            ps.uptime_s    = millis() / 1000;
            portal_update(ps);
        }

        // ── POST al backend ─────────────────
        if (reading_count % POST_EVERY_N_READINGS == 0) {
            bool alarm = (data.pt100_ok && data.temp_pt100 > TEMP_ALARM_HIGH) ||
                         (data.pt100_ok && data.temp_pt100 < TEMP_ALARM_LOW);

            float t_pt100 = data.pt100_ok ? data.temp_pt100 : -999.0f;
            float t_sht   = data.sht35_ok ? data.temp_sht35 : -999.0f;
            float hum     = data.sht35_ok ? data.humidity    : -999.0f;

            // Prioridad: WiFi (rápido y gratuito) → GSM (fallback)
            if (wifi_is_sta_connected()) {
                Serial.println("[MAIN] Enviando POST vía WiFi...");
                int code = wifi_http_post(t_pt100, t_sht, hum, battery_pct(), alarm);
                if (code == 200 || code == 201) {
                    Serial.printf("[MAIN] WiFi POST OK (HTTP %d)\n", code);
                    last_post_ms = millis();
                } else {
                    Serial.printf("[MAIN] WiFi POST falló (%d) — intentando GSM\n", code);
                    goto try_gsm;
                }
            } else if (modem_ready) {
                try_gsm:
                Serial.println("[MAIN] Enviando POST vía GSM...");
                int code = modem_http_post(t_pt100, t_sht, hum, battery_pct(), alarm);
                if (code == 200 || code == 201) {
                    Serial.printf("[MAIN] GSM POST OK (HTTP %d)\n", code);
                    last_post_ms = millis();
                } else if (code == -2) {
                    Serial.println("[MAIN] TCP falló — reconectando GPRS...");
                    modem_ready = modem_connect();
                } else {
                    Serial.printf("[MAIN] GSM POST %d — reintentando\n", code);
                }
            }
        }
    }
}
