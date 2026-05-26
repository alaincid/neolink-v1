#include "sensors.h"
#include "config.h"

#include <Wire.h>
#include <SPI.h>
#include <Adafruit_SHT31.h>
#include <Adafruit_MAX31865.h>

// ─────────────────────────────────────────────
//  Instancias de sensores
// ─────────────────────────────────────────────

static Adafruit_SHT31 sht35;

// MAX31865: software SPI — pasamos CS, MOSI, MISO, CLK
static Adafruit_MAX31865 max31865(
    MAX31865_CS_PIN,
    MAX31865_MOSI_PIN,
    MAX31865_MISO_PIN,
    MAX31865_CLK_PIN
);

// ─────────────────────────────────────────────
//  sensors_init
// ─────────────────────────────────────────────

bool sensors_init() {
    bool ok = true;

    // SHT35 — I2C en pines personalizados
    Wire.begin(SHT35_SDA_PIN, SHT35_SCL_PIN);

    if (!sht35.begin(SHT35_I2C_ADDR)) {
        Serial.println("[SENSORS] ERROR: SHT35 no detectado en I2C");
        ok = false;
    } else {
        Serial.println("[SENSORS] SHT35 OK");
    }

    // MAX31865 — SPI software, PT100 4 hilos
    max31865.begin(PT100_WIRES);
    Serial.println("[SENSORS] MAX31865 OK");

    return ok;
}

// ─────────────────────────────────────────────
//  sensors_read
// ─────────────────────────────────────────────

void sensors_read(SensorData &out) {

    // ── SHT35 ────────────────────────────────
    float t = sht35.readTemperature();
    float h = sht35.readHumidity();

    if (isnan(t) || isnan(h)) {
        out.sht35_ok  = false;
        out.temp_sht35 = -999.0f;
        out.humidity   = -999.0f;
        Serial.println("[SENSORS] SHT35: lectura inválida (NaN)");
    } else {
        out.sht35_ok   = true;
        out.temp_sht35 = t;
        out.humidity   = h;
    }

    // ── MAX31865 / PT100 ─────────────────────
    uint8_t fault = max31865.readFault();

    if (fault) {
        out.pt100_ok    = false;
        out.temp_pt100  = -999.0f;
        out.pt100_fault = fault;
        sensors_print_pt100_fault(fault);
        max31865.clearFault();
    } else {
        out.pt100_ok    = true;
        out.pt100_fault = 0;
        out.temp_pt100  = max31865.temperature(PT100_NOMINAL_R, PT100_REF_R);
    }
}

// ─────────────────────────────────────────────
//  sensors_print_pt100_fault
// ─────────────────────────────────────────────

void sensors_print_pt100_fault(uint8_t fault) {
    Serial.print("[SENSORS] MAX31865 FAULT 0x");
    Serial.print(fault, HEX);
    Serial.print(": ");

    if (fault & MAX31865_FAULT_HIGHTHRESH)  Serial.print("RTD High Threshold | ");
    if (fault & MAX31865_FAULT_LOWTHRESH)   Serial.print("RTD Low Threshold | ");
    if (fault & MAX31865_FAULT_REFINLOW)    Serial.print("REFIN- > 0.85 x Bias | ");
    if (fault & MAX31865_FAULT_REFINHIGH)   Serial.print("REFIN- < 0.85 x Bias (FORCE- open) | ");
    if (fault & MAX31865_FAULT_RTDINLOW)    Serial.print("RTDIN- < 0.85 x Bias (FORCE- open) | ");
    if (fault & MAX31865_FAULT_OVUV)        Serial.print("Over/Under Voltage | ");

    Serial.println();
}
