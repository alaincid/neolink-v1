#include "power_manager.h"
#include "config.h"
#include <Wire.h>

// ─────────────────────────────────────────────
//  AXP2101 — configuración exacta para
//  Waveshare ESP32-S3-Touch-LCD-3.5
//  Basada en la implementación de referencia
//  del proyecto xiaozhi-esp32 (Waveshare board)
// ─────────────────────────────────────────────

#define AXP_ADDR  0x34

// Registros clave
#define AXP_CHIP_ID       0x03
#define AXP_STATUS2       0x01   // bit3 = batería conectada

#define AXP_IRQ_OFF_CFG   0x22   // fuentes de apagado
#define AXP_PWRKEY_CFG    0x27   // tiempo de apagado por botón

#define AXP_DC_EN         0x80   // enable DC-DC (bit0=DC1…)
#define AXP_DC1_VOL       0x82   // DC1 voltage: (mV-1500)/100

#define AXP_LDO0_EN       0x90   // ALDO1..4 + BLDO1..2 enable
                                 // bit0=ALDO1 bit1=ALDO2 bit2=ALDO3 bit3=ALDO4
                                 // bit4=BLDO1 bit5=BLDO2 bit6=CPUSLDO bit7=DLDO1
#define AXP_LDO1_EN       0x91   // bit0=DLDO2

#define AXP_ALDO1_VOL     0x92   // (mV-500)/100
#define AXP_ALDO2_VOL     0x93
#define AXP_ALDO3_VOL     0x94
#define AXP_ALDO4_VOL     0x95
#define AXP_BLDO1_VOL     0x96   // (mV-500)/100
#define AXP_BLDO2_VOL     0x97

#define AXP_CHG_PRECURR   0x61   // precharge current
#define AXP_CHG_CURR      0x62   // charge constant current
#define AXP_CHG_TERM      0x63   // termination current
#define AXP_CHG_VOL       0x64   // CV voltage

#define AXP_GAUGE_CTRL    0xB8   // bit1 = fuel gauge enable
#define AXP_GAUGE_PCT     0xB9   // bits6:0 = battery % (0-100)

static bool s_ok = false;

// ── I2C helpers ────────────────────────────────────────────────────────────
static uint8_t axp_read(uint8_t reg) {
    Wire1.beginTransmission(AXP_ADDR);
    Wire1.write(reg);
    if (Wire1.endTransmission(false) != 0) return 0xFF;
    Wire1.requestFrom((uint8_t)AXP_ADDR, (uint8_t)1);
    return Wire1.available() ? Wire1.read() : 0xFF;
}

static void axp_write(uint8_t reg, uint8_t val) {
    Wire1.beginTransmission(AXP_ADDR);
    Wire1.write(reg);
    Wire1.write(val);
    Wire1.endTransmission();
}

// ══════════════════════════════════════════════════════════════════════════
bool power_init() {
    // Wire1 ya se usa para TCA9554 — mismos pines, reinit es seguro
    Wire1.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire1.setClock(400000);

    // Verificar presencia del AXP2101
    Wire1.beginTransmission(AXP_ADDR);
    if (Wire1.endTransmission() != 0) {
        Serial.println("[PWR] AXP2101 no encontrado en 0x34");
        return false;
    }
    uint8_t chip_id = axp_read(AXP_CHIP_ID);
    Serial.printf("[PWR] AXP2101 ChipID=0x%02X\n", chip_id);

    // ── Botón PWR ──────────────────────────────────────────────────────────
    axp_write(AXP_IRQ_OFF_CFG, 0x06);  // PWRON > OFFLEVEL como fuente de apagado
    axp_write(AXP_PWRKEY_CFG,  0x10);  // 4 s de pulsación para apagar

    // ── DC-DC: NO se toca — los rails DC pueden estar alimentando el backlight
    // Solo aseguramos ALDO1+BLDO1+BLDO2 para los rails LCD

    // ── Voltajes de los LDOs de la pantalla ───────────────────────────────
    // ALDO1 = 3.3V → alimenta la pantalla y periféricos
    axp_write(AXP_ALDO1_VOL, (3300 - 500) / 100);  // = 28

    // BLDO1 = 1.5V → VDDI (digital I/O del LCD)
    axp_write(AXP_BLDO1_VOL, (1500 - 500) / 100);  // = 10

    // BLDO2 = 2.8V → AVDD (analog del LCD)
    axp_write(AXP_BLDO2_VOL, (2800 - 500) / 100);  // = 23

    // ── Encender ALDO1 + BLDO1 + BLDO2 sin tocar otros LDOs ──────────────
    // Leer estado actual y solo activar los bits necesarios
    uint8_t ldo_state = axp_read(AXP_LDO0_EN);
    // bit0=ALDO1, bit4=BLDO1, bit5=BLDO2
    axp_write(AXP_LDO0_EN, ldo_state | 0x31);

    // ── Carga de batería ───────────────────────────────────────────────────
    axp_write(AXP_CHG_VOL,     0x02);  // CV = 4.1V
    axp_write(AXP_CHG_PRECURR, 0x02);  // Precarga = 50mA
    axp_write(AXP_CHG_CURR,    0x08);  // Carga constante = 400mA
    axp_write(AXP_CHG_TERM,    0x01);  // Terminación = 25mA

    // ── Fuel gauge ─────────────────────────────────────────────────────────
    uint8_t fg = axp_read(AXP_GAUGE_CTRL);
    axp_write(AXP_GAUGE_CTRL, fg | 0x02);  // habilitar fuel gauge (bit 1)

    s_ok = true;
    delay(20);  // pequeña espera para que los LDOs estabilicen

    uint8_t pct = power_batt_pct();
    Serial.printf("[PWR] OK — batería: %u%%\n", pct);
    return true;
}

// ══════════════════════════════════════════════════════════════════════════
uint8_t power_batt_pct() {
    if (!s_ok) return 100;
    // Verificar que la batería está conectada (STATUS2 bit3)
    if (!(axp_read(AXP_STATUS2) & 0x08)) return 100;  // sin batería → 100 (USB)
    uint8_t pct = axp_read(AXP_GAUGE_PCT) & 0x7F;
    return (pct > 100) ? 100 : pct;
}
