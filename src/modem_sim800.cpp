#include "modem_sim800.h"
#include "config.h"
#include <ArduinoJson.h>

// ─────────────────────────────────────────────
//  UART2 para SIM800L — TCP crudo (CIP stack)
//  NO usar GPIO43/GPIO44
// ─────────────────────────────────────────────

#define SIM_SERIAL  Serial1
#define APN         "internet.itelcel.com"
#define APN_USER    "webgprs"
#define APN_PASS    "webgprs2002"

#define AT_SHORT    2000
#define AT_LONG     8000
#define TCP_TIMEOUT 30000

static ModemState s_state = ModemState::OFFLINE;

// ─────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────

static void sim_flush() {
    delay(50);
    while (SIM_SERIAL.available()) SIM_SERIAL.read();
}

// Envía un AT y espera que la respuesta contenga `expected` dentro de timeout_ms.
// Retorna true si lo encuentra. Copia respuesta completa en buf si se provee.
static bool sim_cmd(const char *cmd, const char *expected,
                    uint32_t timeout_ms = AT_SHORT,
                    char *buf = nullptr, size_t buf_len = 0) {
    sim_flush();
    SIM_SERIAL.println(cmd);
    Serial.printf("[SIM] >> %s\n", cmd);

    String resp = "";
    uint32_t t  = millis();
    while (millis() - t < timeout_ms) {
        while (SIM_SERIAL.available()) resp += (char)SIM_SERIAL.read();
        if (resp.indexOf(expected) != -1) {
            resp.trim();
            Serial.printf("[SIM] << %s\n", resp.c_str());
            if (buf && buf_len) {
                strncpy(buf, resp.c_str(), buf_len - 1);
                buf[buf_len - 1] = '\0';
            }
            return true;
        }
        delay(10);
    }
    resp.trim();
    if (resp.length()) Serial.printf("[SIM] TIMEOUT '%s'. Resp: %s\n", expected, resp.c_str());
    else               Serial.printf("[SIM] TIMEOUT '%s'. Sin respuesta.\n", expected);
    return false;
}

// Soft-reset del SIM800L vía AT+CFUN=1,1
static void sim_soft_reset() {
    Serial.println("[MODEM] Intentando escape +++ ...");
    delay(1200);
    SIM_SERIAL.print("+++");
    delay(1200);
    sim_flush();

    Serial.println("[MODEM] Soft-reset SIM800L (AT+CFUN=1,1)...");
    SIM_SERIAL.println("AT+CFUN=1,1");
    delay(8000);   // SIM800L tarda ~8s en reiniciar completamente
    sim_flush();
}

// ─────────────────────────────────────────────
//  modem_init
// ─────────────────────────────────────────────

bool modem_init() {
    SIM_SERIAL.begin(SIM800L_BAUDRATE, SERIAL_8N1,
                     SIM800L_RX_PIN, SIM800L_TX_PIN);
    delay(1000);

    Serial.println("[MODEM] Iniciando SIM800L...");

    // Intenta AT hasta 15 veces; si no responde, intenta soft-reset
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 15; i++) {
            delay(1000);
            sim_flush();
            SIM_SERIAL.println("AT");
            delay(500);
            String r = "";
            uint32_t t = millis();
            while (millis() - t < 500) {
                while (SIM_SERIAL.available()) r += (char)SIM_SERIAL.read();
            }
            r.trim();
            Serial.printf("[MODEM] AT %d/15 → '%s'\n", i + 1, r.c_str());
            if (r.indexOf("OK") != -1) goto sim_ok;
        }
        if (pass == 0) sim_soft_reset();
    }

    Serial.println("[MODEM] ERROR: SIM800L no responde. Verifica VCC=3.7V, GND, RX=21, TX=45");
    s_state = ModemState::ERROR;
    return false;

sim_ok:
    Serial.println("[MODEM] SIM800L OK");
    sim_cmd("ATE0",     "OK", AT_SHORT);
    sim_cmd("AT+CMEE=1","OK", AT_SHORT);

    // Limpia solo el stack TCP para no interferir con el registro GPRS
    sim_cmd("AT+CIPSHUT", "SHUT OK", AT_LONG);

    s_state = ModemState::OFFLINE;
    return true;
}

// ─────────────────────────────────────────────
//  modem_connect — usa CIP stack (no SAPBR)
// ─────────────────────────────────────────────

bool modem_connect() {
    sim_flush();
    sim_cmd("ATE0", "OK", AT_SHORT);
    Serial.println("[MODEM] Conectando a red GSM...");

    // Espera registro (hasta 30 s)
    bool registered = false;
    for (int i = 0; i < 10; i++) {
        char buf[64];
        if (sim_cmd("AT+CREG?", "+CREG:", AT_SHORT, buf, sizeof(buf))) {
            if (strstr(buf, ",1") || strstr(buf, ",5")) {
                registered = true; break;
            }
        }
        Serial.printf("[MODEM] Esperando GSM (%d/10)...\n", i + 1);
        delay(3000);
    }
    if (!registered) {
        Serial.println("[MODEM] ERROR: No se registró en GSM");
        s_state = ModemState::ERROR;
        return false;
    }
    Serial.println("[MODEM] Registrado en GSM");
    s_state = ModemState::REGISTERED;

    // Limpia stack TCP previo
    sim_cmd("AT+CIPSHUT", "SHUT OK", AT_LONG);
    delay(500);

    // Asegura adjunto GPRS (puede ya estar activo — ignoramos error)
    sim_cmd("AT+CGATT=1", "OK", 10000);
    delay(1000);

    // Configurar APN
    {
        char cmd[80];
        snprintf(cmd, sizeof(cmd), "AT+CSTT=\"%s\",\"%s\",\"%s\"", APN, APN_USER, APN_PASS);
        if (!sim_cmd(cmd, "OK", AT_SHORT)) {
            Serial.println("[MODEM] ERROR: AT+CSTT falló");
            s_state = ModemState::ERROR;
            return false;
        }
    }

    // Activar conexión GPRS — reintenta hasta 3 veces con pausa si hay PDP DEACT
    bool ciicr_ok = false;
    for (int i = 0; i < 3; i++) {
        if (sim_cmd("AT+CIICR", "OK", 30000)) { ciicr_ok = true; break; }
        Serial.printf("[MODEM] CIICR intento %d/3 falló, esperando 10s...\n", i + 1);
        sim_cmd("AT+CIPSHUT", "SHUT OK", AT_LONG);
        delay(10000);
        sim_cmd("AT+CGATT=1", "OK", 10000);
        delay(2000);
        sim_cmd("AT+CSTT=\"" APN "\",\"" APN_USER "\",\"" APN_PASS "\"", "OK", AT_SHORT);
    }
    if (!ciicr_ok) {
        Serial.println("[MODEM] ERROR: AT+CIICR falló tras 3 intentos");
        s_state = ModemState::ERROR;
        return false;
    }

    // Obtener IP
    char ip[32] = {0};
    if (!sim_cmd("AT+CIFSR", ".", AT_SHORT, ip, sizeof(ip))) {
        Serial.println("[MODEM] ERROR: No se obtuvo IP");
        s_state = ModemState::ERROR;
        return false;
    }
    Serial.printf("[MODEM] IP GPRS: %s\n", ip);

    s_state = ModemState::GPRS_UP;
    return true;
}

// ─────────────────────────────────────────────
//  modem_http_post — TCP crudo con CIPSTART
// ─────────────────────────────────────────────

int modem_http_post(float temp_pt100, float temp_sht35,
                    float humidity,   uint8_t battery,
                    bool alarm) {

    // Lee RSSI
    int8_t rssi = 0;
    char csq_buf[32];
    if (sim_cmd("AT+CSQ", "+CSQ:", AT_SHORT, csq_buf, sizeof(csq_buf))) {
        int raw = 99;
        sscanf(csq_buf, "+CSQ: %d", &raw);
        if (raw != 99) rssi = -113 + raw * 2;
    }

    // Construye payload JSON
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

    // Construye petición HTTP/1.0 completa
    char http_req[512];
    int  body_len = strlen(payload);
    snprintf(http_req, sizeof(http_req),
        "POST %s HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        BACKEND_PATH, BACKEND_HOST, BACKEND_PORT, body_len, payload);

    int req_len = strlen(http_req);
    Serial.printf("[MODEM] POST %s:%d%s (%d bytes)\n",
                  BACKEND_HOST, BACKEND_PORT, BACKEND_PATH, body_len);

    // Abre conexión TCP
    {
        char cmd[80];
        snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"TCP\",\"%s\",\"%d\"",
                 BACKEND_HOST, BACKEND_PORT);
        if (!sim_cmd(cmd, "CONNECT", TCP_TIMEOUT)) {
            Serial.println("[MODEM] ERROR: TCP no conectó");
            sim_cmd("AT+CIPCLOSE", "CLOSE OK", AT_SHORT);
            return -2;   // -2 = TCP falló → reconectar GPRS
        }
    }
    Serial.println("[MODEM] TCP conectado");
    delay(100);

    // Envía datos
    {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%d", req_len);
        if (!sim_cmd(cmd, ">", AT_SHORT)) {
            Serial.println("[MODEM] ERROR: CIPSEND no retornó prompt");
            sim_cmd("AT+CIPCLOSE", "CLOSE OK", AT_SHORT);
            return -2;   // -2 = TCP falló → reconectar GPRS
        }
    }
    SIM_SERIAL.print(http_req);
    Serial.println("[MODEM] Request enviado, esperando respuesta...");

    // Lee respuesta HTTP — dejamos que el servidor cierre la conexión (Connection: close)
    // El SIM800L notifica con "CLOSED" cuando la conexión TCP termina
    String resp = "";
    uint32_t t  = millis();
    bool got_send_ok = false;

    while (millis() - t < TCP_TIMEOUT) {
        while (SIM_SERIAL.available()) resp += (char)SIM_SERIAL.read();

        if (!got_send_ok && resp.indexOf("SEND OK") != -1) {
            got_send_ok = true;
            Serial.println("[MODEM] SEND OK — esperando respuesta del servidor...");
            t = millis();   // reinicia timer desde SEND OK
            resp = "";
            continue;
        }

        if (got_send_ok) {
            // El servidor cierra la conexión → SIM800L emite "CLOSED"
            if (resp.indexOf("CLOSED") != -1) {
                Serial.println("[MODEM] Conexion cerrada por servidor");
                break;
            }
        }
        delay(50);
    }

    // AT+CIPCLOSE solo como limpieza si la conexión no se cerró sola
    if (resp.indexOf("CLOSED") == -1) {
        sim_cmd("AT+CIPCLOSE", "CLOSE OK", AT_SHORT);
    }

    // Extrae status code: puede estar en "+IPD,xx:HTTP/1.x NNN" o directo
    int status_code = -1;
    int idx = resp.indexOf("HTTP/1.");
    if (idx != -1) {
        sscanf(resp.c_str() + idx, "HTTP/1.%*c %d", &status_code);
    }

    Serial.printf("[MODEM] HTTP status: %d\n", status_code);
    if (status_code == -1) {
        Serial.printf("[MODEM] Raw(%d): %s\n",
                      resp.length(), resp.substring(0, 150).c_str());
    }
    return status_code;
}

// ─────────────────────────────────────────────
//  modem_disconnect
// ─────────────────────────────────────────────

void modem_disconnect() {
    sim_cmd("AT+CIPSHUT", "SHUT OK", AT_LONG);
    s_state = ModemState::OFFLINE;
    Serial.println("[MODEM] GPRS desconectado");
}

// ─────────────────────────────────────────────
//  modem_read_status
// ─────────────────────────────────────────────

void modem_read_status(ModemStatus &out) {
    out.state = s_state;
    char buf[64];
    if (sim_cmd("AT+CSQ", "+CSQ:", AT_SHORT, buf, sizeof(buf))) {
        int raw = 99;
        sscanf(buf, "+CSQ: %d", &raw);
        out.rssi_raw = (uint8_t)raw;
        out.rssi     = (raw != 99) ? (-113 + raw * 2) : 0;
    }
    out.sim_present = sim_cmd("AT+CIMI", "334", AT_SHORT);
}

// ─────────────────────────────────────────────
//  modem_get_state
// ─────────────────────────────────────────────

ModemState modem_get_state() { return s_state; }
