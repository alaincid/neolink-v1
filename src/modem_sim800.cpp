#include "modem_sim800.h"
#include "config.h"
#include <ArduinoJson.h>

// ─────────────────────────────────────────────
//  UART2 para SIM800L
//  IMPORTANTE: NO usar GPIO43/GPIO44
// ─────────────────────────────────────────────

#define SIM_SERIAL       Serial1
#define SIM_SERIAL_NUM   1

#define APN              "internet.itelcel.com"
#define APN_USER         "webgprs"
#define APN_PASS         "webgprs2002"

#define AT_TIMEOUT_MS    3000
#define HTTP_TIMEOUT_MS  15000
#define GPRS_RETRY       5

static ModemState s_state = ModemState::OFFLINE;

// ─────────────────────────────────────────────
//  Helpers internos
// ─────────────────────────────────────────────

// Vacía el buffer de entrada del SIM800L
static void sim_flush() {
    while (SIM_SERIAL.available()) SIM_SERIAL.read();
}

// Envía un comando AT y espera una respuesta que contenga `expected`.
// Retorna true si la encuentra dentro de `timeout_ms`.
static bool sim_cmd(const char *cmd, const char *expected,
                    uint32_t timeout_ms = AT_TIMEOUT_MS,
                    char *buf = nullptr, size_t buf_len = 0) {
    sim_flush();
    SIM_SERIAL.println(cmd);
    Serial.printf("[SIM] >> %s\n", cmd);

    uint32_t start = millis();
    String   resp  = "";

    while (millis() - start < timeout_ms) {
        while (SIM_SERIAL.available()) {
            char c = SIM_SERIAL.read();
            resp += c;
        }
        if (resp.indexOf(expected) != -1) {
            resp.trim();
            Serial.printf("[SIM] << %s\n", resp.c_str());
            if (buf && buf_len > 0) {
                strncpy(buf, resp.c_str(), buf_len - 1);
                buf[buf_len - 1] = '\0';
            }
            return true;
        }
        // También detecta ERROR para salir rápido
        if (resp.indexOf("ERROR") != -1) {
            resp.trim();
            Serial.printf("[SIM] << %s\n", resp.c_str());
            return false;
        }
        delay(10);
    }

    resp.trim();
    Serial.printf("[SIM] TIMEOUT esperando '%s'. Resp: %s\n", expected, resp.c_str());
    return false;
}

// ─────────────────────────────────────────────
//  modem_init
// ─────────────────────────────────────────────

bool modem_init() {
    SIM_SERIAL.begin(SIM800L_BAUDRATE, SERIAL_8N1,
                     SIM800L_RX_PIN, SIM800L_TX_PIN);

    Serial.println("[MODEM] Esperando arranque SIM800L (13s max)...");

    // El SIM800L puede tardar hasta 13 s desde cold start
    // Intentamos AT cada 1 s durante 15 intentos
    bool responded = false;
    for (int i = 0; i < 15; i++) {
        delay(1000);
        sim_flush();
        SIM_SERIAL.println("AT");
        delay(300);
        String r = "";
        uint32_t t = millis();
        while (millis() - t < 700) {
            while (SIM_SERIAL.available()) r += (char)SIM_SERIAL.read();
        }
        r.trim();
        Serial.printf("[MODEM] AT intento %d/15 → '%s'\n", i + 1, r.c_str());
        if (r.indexOf("OK") != -1) {
            responded = true;
            break;
        }
    }

    if (!responded) {
        Serial.println("[MODEM] ERROR: SIM800L no responde tras 15 intentos");
        Serial.println("[MODEM] Verifica: VCC=LiPo 3.7V, GND comun, RX=21, TX=45, cap 1000uF");
        s_state = ModemState::ERROR;
        return false;
    }

    Serial.println("[MODEM] SIM800L OK");

    // Limpia sesiones previas
    sim_cmd("AT+HTTPTERM", "OK", 1000);
    sim_cmd("AT+SAPBR=0,1", "OK", 3000);
    delay(300);

    sim_cmd("ATE0", "OK");
    sim_cmd("AT+CMEE=1", "OK");

    s_state = ModemState::OFFLINE;
    return true;
}

// ─────────────────────────────────────────────
//  modem_connect
// ─────────────────────────────────────────────

bool modem_connect() {
    Serial.println("[MODEM] Conectando a red GSM...");

    // Espera registro en red (hasta 30 s)
    bool registered = false;
    for (int i = 0; i < 10; i++) {
        char buf[64];
        if (sim_cmd("AT+CREG?", "+CREG:", AT_TIMEOUT_MS, buf, sizeof(buf))) {
            // +CREG: 0,1 = registrado home / 0,5 = roaming
            if (strstr(buf, ",1") || strstr(buf, ",5")) {
                registered = true;
                break;
            }
        }
        Serial.printf("[MODEM] Esperando registro GSM (%d/10)...\n", i + 1);
        delay(3000);
    }

    if (!registered) {
        Serial.println("[MODEM] ERROR: No se registró en red GSM");
        s_state = ModemState::ERROR;
        return false;
    }

    Serial.println("[MODEM] Registrado en red GSM");
    s_state = ModemState::REGISTERED;

    // Verifica SIM — IMSI México Telcel empieza con 334020
    char imsi_buf[32];
    if (sim_cmd("AT+CIMI", "334", AT_TIMEOUT_MS, imsi_buf, sizeof(imsi_buf))) {
        Serial.printf("[MODEM] SIM OK — IMSI: %s\n", imsi_buf);
    } else {
        Serial.println("[MODEM] Advertencia: IMSI no reconocido");
    }

    // Configura APN
    sim_cmd("AT+SAPBR=3,1,\"Contype\",\"GPRS\"", "OK");
    sim_cmd("AT+SAPBR=3,1,\"APN\",\"" APN "\"",  "OK");
    sim_cmd("AT+SAPBR=3,1,\"USER\",\"" APN_USER "\"", "OK");
    sim_cmd("AT+SAPBR=3,1,\"PWD\",\"" APN_PASS "\"",  "OK");

    // Cierra contexto previo si estaba abierto, luego abre fresco
    Serial.println("[MODEM] Activando GPRS...");
    sim_cmd("AT+SAPBR=0,1", "OK", 5000);
    delay(500);
    if (!sim_cmd("AT+SAPBR=1,1", "OK", 15000)) {
        Serial.println("[MODEM] SAPBR open falló");
    }

    // Lee IP asignada
    char ip_buf[64];
    if (sim_cmd("AT+SAPBR=2,1", "+SAPBR:", AT_TIMEOUT_MS, ip_buf, sizeof(ip_buf))) {
        Serial.printf("[MODEM] GPRS activo: %s\n", ip_buf);
        s_state = ModemState::GPRS_UP;
        return true;
    }

    Serial.println("[MODEM] ERROR: GPRS no activo");
    s_state = ModemState::ERROR;
    return false;
}

// ─────────────────────────────────────────────
//  modem_http_post
// ─────────────────────────────────────────────

int modem_http_post(float temp_pt100, float temp_sht35,
                    float humidity,   uint8_t battery,
                    bool alarm) {

    // Lee RSSI para incluirlo en el payload
    int8_t rssi = 0;
    char csq_buf[32];
    if (sim_cmd("AT+CSQ", "+CSQ:", AT_TIMEOUT_MS, csq_buf, sizeof(csq_buf))) {
        int raw = 0;
        sscanf(csq_buf, "+CSQ: %d", &raw);
        if (raw != 99) rssi = -113 + (raw * 2);
    }

    // Construye JSON payload
    JsonDocument doc;
    doc["device_id"] = DEVICE_ID;
    doc["token"]     = DEVICE_TOKEN;
    doc["temp_pt100"] = temp_pt100;
    doc["temp_sht35"] = temp_sht35;
    doc["humidity"]   = humidity;
    doc["battery"]    = battery;
    doc["rssi"]       = rssi;
    doc["alarm"]      = alarm;

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    Serial.printf("[MODEM] POST payload: %s\n", payload);

    // Inicializa HTTP
    if (!sim_cmd("AT+HTTPINIT", "OK", AT_TIMEOUT_MS)) {
        sim_cmd("AT+HTTPTERM", "OK", 1000);  // cierra sesión anterior
        if (!sim_cmd("AT+HTTPINIT", "OK", AT_TIMEOUT_MS)) {
            Serial.println("[MODEM] ERROR: HTTPINIT falló");
            return -1;
        }
    }

    sim_cmd("AT+HTTPPARA=\"CID\",1", "OK");

    char url_cmd[128];
    snprintf(url_cmd, sizeof(url_cmd),
             "AT+HTTPPARA=\"URL\",\"http://%s:%d%s\"",
             BACKEND_HOST, BACKEND_PORT, BACKEND_PATH);
    sim_cmd(url_cmd, "OK");

    sim_cmd("AT+HTTPPARA=\"CONTENT\",\"application/json\"", "OK");

    // Envía el body — SIM800L puede responder OK luego DOWNLOAD en líneas separadas
    char data_cmd[32];
    snprintf(data_cmd, sizeof(data_cmd), "AT+HTTPDATA=%d,5000", strlen(payload));
    SIM_SERIAL.println(data_cmd);
    Serial.printf("[SIM] >> %s\n", data_cmd);

    // Espera hasta 4 s a que aparezca DOWNLOAD (puede venir después de OK)
    uint32_t dl_start = millis();
    String   dl_resp  = "";
    bool     got_dl   = false;
    while (millis() - dl_start < 4000) {
        while (SIM_SERIAL.available()) dl_resp += (char)SIM_SERIAL.read();
        if (dl_resp.indexOf("DOWNLOAD") != -1) { got_dl = true; break; }
        delay(10);
    }
    if (!got_dl) {
        Serial.println("[SIM] ERROR: no llegó DOWNLOAD para HTTPDATA");
        sim_cmd("AT+HTTPTERM", "OK", 1000);
        return -1;
    }
    SIM_SERIAL.print(payload);
    delay(300);

    // Ejecuta POST y espera la respuesta completa del +HTTPACTION asíncrono
    SIM_SERIAL.println("AT+HTTPACTION=1");
    Serial.println("[SIM] >> AT+HTTPACTION=1");

    // El SIM800L responde primero con OK y luego envía +HTTPACTION: 1,STATUS,LEN
    // Esperamos hasta HTTP_TIMEOUT_MS a que llegue el status completo
    String http_resp = "";
    int    status_code = -1;
    uint32_t ha_start = millis();

    while (millis() - ha_start < HTTP_TIMEOUT_MS) {
        while (SIM_SERIAL.available()) http_resp += (char)SIM_SERIAL.read();

        // Busca la línea completa: +HTTPACTION: 1,NNN,NNN
        int idx = http_resp.indexOf("+HTTPACTION:");
        if (idx != -1) {
            // Verifica que haya al menos dos comas después (status y len)
            int c1 = http_resp.indexOf(',', idx);
            int c2 = (c1 != -1) ? http_resp.indexOf(',', c1 + 1) : -1;
            if (c1 != -1 && c2 != -1) {
                sscanf(http_resp.c_str() + idx, "+HTTPACTION: 1,%d", &status_code);
                Serial.printf("[SIM] HTTPACTION resp: %s\n",
                              http_resp.substring(idx).c_str());
                break;
            }
        }
        delay(50);
    }

    if (status_code == -1) {
        Serial.printf("[SIM] TIMEOUT HTTPACTION. Resp: %s\n", http_resp.c_str());
    }

    sim_cmd("AT+HTTPTERM", "OK", AT_TIMEOUT_MS);

    Serial.printf("[MODEM] HTTP status: %d\n", status_code);
    return status_code;
}

// ─────────────────────────────────────────────
//  modem_disconnect
// ─────────────────────────────────────────────

void modem_disconnect() {
    sim_cmd("AT+SAPBR=0,1", "OK", 5000);
    s_state = ModemState::OFFLINE;
    Serial.println("[MODEM] GPRS desconectado");
}

// ─────────────────────────────────────────────
//  modem_read_status
// ─────────────────────────────────────────────

void modem_read_status(ModemStatus &out) {
    out.state = s_state;

    char buf[64];
    if (sim_cmd("AT+CSQ", "+CSQ:", AT_TIMEOUT_MS, buf, sizeof(buf))) {
        int raw = 99;
        sscanf(buf, "+CSQ: %d", &raw);
        out.rssi_raw = (uint8_t)raw;
        out.rssi     = (raw != 99) ? (-113 + raw * 2) : 0;
    }

    out.sim_present = sim_cmd("AT+CIMI", "89", AT_TIMEOUT_MS);

    if (s_state == ModemState::GPRS_UP) {
        char ip_buf[64];
        if (sim_cmd("AT+SAPBR=2,1", "+SAPBR:", AT_TIMEOUT_MS, ip_buf, sizeof(ip_buf))) {
            // Extrae IP del formato: +SAPBR: 1,1,"10.x.x.x"
            char *q1 = strchr(ip_buf, '"');
            char *q2 = q1 ? strchr(q1 + 1, '"') : nullptr;
            if (q1 && q2) {
                size_t len = q2 - q1 - 1;
                if (len < sizeof(out.ip)) {
                    strncpy(out.ip, q1 + 1, len);
                    out.ip[len] = '\0';
                }
            }
        }
    }
}

// ─────────────────────────────────────────────
//  modem_get_state
// ─────────────────────────────────────────────

ModemState modem_get_state() {
    return s_state;
}
