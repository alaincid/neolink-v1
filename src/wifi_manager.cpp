#include "wifi_manager.h"
#include "preferences_store.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClient.h>
#include <time.h>

// ── Estado ────────────────────────────────────────────────────────────────
static WifiInfo  s_info            = { WIFI_IDLE, 0, "0.0.0.0", "" };
static uint32_t  s_connect_start   = 0;
static bool      s_reconnect_req   = false;
static char      s_pend_ssid[33]   = "";
static char      s_pend_pass[65]   = "";

// ── Helpers ───────────────────────────────────────────────────────────────
static void start_ap() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
    strlcpy(s_info.ip,   "192.168.4.1",  sizeof(s_info.ip));
    strlcpy(s_info.ssid, WIFI_AP_SSID,   sizeof(s_info.ssid));
    s_info.state = WIFI_AP_MODE;
    s_info.rssi  = 0;
    Serial.printf("[WIFI] AP mode → SSID: %s  IP: %s\n", WIFI_AP_SSID, s_info.ip);
}

static void try_connect(const char *ssid, const char *pass) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    s_info.state = WIFI_CONNECTING;
    s_info.rssi  = 0;
    s_connect_start = millis();
    strlcpy(s_info.ssid, ssid, sizeof(s_info.ssid));
    Serial.printf("[WIFI] Connecting to '%s'...\n", ssid);
}

// ── API ───────────────────────────────────────────────────────────────────
void wifi_manager_begin() {
    WiFi.setAutoReconnect(false);
    String ssid = prefs_wifi_ssid();
    if (ssid.length() > 0) {
        try_connect(ssid.c_str(), prefs_wifi_pass().c_str());
    } else {
        Serial.println("[WIFI] No credentials saved");
        start_ap();
    }
}

void wifi_manager_loop() {
    // Reconexión pedida desde el portal
    if (s_reconnect_req) {
        s_reconnect_req = false;
        WiFi.disconnect(true);
        vTaskDelay(pdMS_TO_TICKS(200));
        try_connect(s_pend_ssid, s_pend_pass);
        return;
    }

    switch (s_info.state) {
        case WIFI_CONNECTING:
            if (WiFi.status() == WL_CONNECTED) {
                s_info.state = WIFI_CONNECTED;
                s_info.rssi  = WiFi.RSSI();
                strlcpy(s_info.ip, WiFi.localIP().toString().c_str(), sizeof(s_info.ip));
                Serial.printf("[WIFI] Connected → IP: %s  RSSI: %d dBm\n",
                              s_info.ip, s_info.rssi);
                // NTP sync — México CDMX: UTC-6 permanente (sin horario de verano desde 2023)
                configTime(-6 * 3600, 0, "pool.ntp.org", "time.google.com");
                Serial.println("[WIFI] NTP sync iniciado");
            } else if (millis() - s_connect_start > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("[WIFI] Timeout — starting AP mode");
                start_ap();
            }
            break;

        case WIFI_CONNECTED:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WIFI] Lost connection, reconnecting...");
                s_info.state = WIFI_CONNECTING;
                s_info.rssi  = 0;
                s_connect_start = millis();
                WiFi.reconnect();
            } else {
                // Actualiza RSSI periódicamente (barato)
                static uint32_t last_rssi = 0;
                if (millis() - last_rssi > 5000) {
                    last_rssi = millis();
                    s_info.rssi = WiFi.RSSI();
                    strlcpy(s_info.ip, WiFi.localIP().toString().c_str(), sizeof(s_info.ip));
                }
            }
            break;

        default:
            break;
    }
}

WifiInfo wifi_get_info() { return s_info; }

bool wifi_is_sta_connected() { return s_info.state == WIFI_CONNECTED; }

void wifi_save_and_reconnect(const char *ssid, const char *pass) {
    prefs_save_wifi(ssid, pass);
    strlcpy(s_pend_ssid, ssid, sizeof(s_pend_ssid));
    strlcpy(s_pend_pass, pass, sizeof(s_pend_pass));
    s_reconnect_req = true;
}

// ── HTTP POST vía WiFi ────────────────────────────────────────────────────
int wifi_http_post(float temp_pt100, float temp_sht, float hum,
                   uint8_t bat, bool alarm) {
    if (!wifi_is_sta_connected()) return -3;

    WiFiClient client;
    String host = prefs_server_host();
    int    port = prefs_server_port();
    String name = prefs_device_name();

    if (!client.connect(host.c_str(), port)) {
        Serial.println("[WIFI] TCP connect failed");
        return -2;
    }

    char payload[300];
    snprintf(payload, sizeof(payload),
        "{\"device_id\":\"%s\",\"token\":\"%s\","
        "\"temp_pt100\":%.2f,\"temp_sht35\":%.2f,"
        "\"humidity\":%.2f,\"battery\":%u,\"alarm\":%s}",
        name.c_str(), DEVICE_TOKEN,
        temp_pt100, temp_sht, hum, bat,
        alarm ? "true" : "false");

    int plen = strlen(payload);
    client.printf(
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        BACKEND_PATH, host.c_str(), port, plen, payload);

    // Espera respuesta (máx 5s)
    String resp = "";
    uint32_t t = millis();
    while (millis() - t < 5000) {
        while (client.available()) resp += (char)client.read();
        if (resp.indexOf("\r\n\r\n") >= 0) break;
        delay(10);
    }
    client.stop();

    int status = -1;
    if (resp.startsWith("HTTP/1.")) {
        int sp = resp.indexOf(' ');
        if (sp >= 0) status = resp.substring(sp + 1, sp + 4).toInt();
    }
    Serial.printf("[WIFI] POST → HTTP %d\n", status);
    return status;
}
