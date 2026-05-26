// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//  Dashboard ST7796 320×480 · dark theme
//
//  Hardware (Waveshare ESP32-S3-Touch-LCD-3.5):
//    SPI : SCK=5 MOSI=1 MISO=2 DC=3
//    CS  : TCA9554 P2  (I2C 0x20, Wire1 GPIO8/7)
//    RST : TCA9554 P1
//    BL  : GPIO6 (PWM)
//
//  Lib: GFX Library for Arduino v1.5.5
//       (v1.5.6+ breaks SPI_MODE on ST7796)
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

// ── Palette (RGB565) ────────────────────────────────────────────────────────
static const uint16_t C_BG      = 0x0841;  // #0D1117  very dark navy
static const uint16_t C_PANEL   = 0x10C3;  // #181F2B  card background
static const uint16_t C_BORDER  = 0x2965;  // #2D3748  card border
static const uint16_t C_WHITE   = 0xFFFF;
static const uint16_t C_LGRAY   = 0xAD55;  // #A8B0BE  light label
static const uint16_t C_GRAY    = 0x5AEB;  // #5A6070  dim text
static const uint16_t C_TEMP    = 0x051F;  // #0090FF  blue
static const uint16_t C_HUM     = 0x07ED;  // #00FF68  teal green
static const uint16_t C_OK      = 0x07E0;  // #00FF00
static const uint16_t C_WARN    = 0xFCC0;  // #FF9900  amber
static const uint16_t C_ERR     = 0xF800;  // #FF0000
static const uint16_t C_ACCENT  = 0x2B7F;  // #2477FF  accent blue

// ── Module state ────────────────────────────────────────────────────────────
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus  = nullptr;
static Arduino_GFX     *s_gfx  = nullptr;
static bool             s_ok   = false;

// ── Internal helpers ────────────────────────────────────────────────────────

// Centered text: pass y for top of text, sz for setTextSize(), color
static void print_centered(const char *str, int16_t y, uint8_t sz,
                            uint16_t color) {
    int16_t x1, y1;
    uint16_t tw, th;
    s_gfx->setTextSize(sz);
    s_gfx->setTextColor(color);
    s_gfx->getTextBounds(str, 0, y, &x1, &y1, &tw, &th);
    s_gfx->setCursor((LCD_WIDTH - (int16_t)tw) / 2, y);
    s_gfx->print(str);
}

// Rounded progress bar: (x,y) = top-left, tw=total width, th=height
static void draw_bar(int16_t x, int16_t y, int16_t tw, int16_t th,
                     uint8_t pct, uint16_t fill_color) {
    pct = constrain(pct, 0, 100);
    s_gfx->drawRoundRect(x, y, tw, th, 3, C_BORDER);
    s_gfx->fillRoundRect(x + 2, y + 2, tw - 4, th - 4, 2, C_BG);
    int16_t fw = (int16_t)((tw - 4) * pct / 100);
    if (fw > 0)
        s_gfx->fillRoundRect(x + 2, y + 2, fw, th - 4, 2, fill_color);
}

// Simple signal strength bars (4 bars)
static void draw_signal(int16_t x, int16_t y, int8_t rssi) {
    uint8_t bars = 0;
    if      (rssi >= -65) bars = 4;
    else if (rssi >= -75) bars = 3;
    else if (rssi >= -85) bars = 2;
    else if (rssi >  -99) bars = 1;

    uint16_t color = (bars >= 3) ? C_OK : (bars >= 2) ? C_WARN : C_ERR;
    int16_t bw = 5, gap = 3;
    for (uint8_t i = 0; i < 4; i++) {
        int16_t bh = 4 + i * 4;
        int16_t bx = x + i * (bw + gap);
        int16_t by = y + (16 - bh);
        uint16_t bc = (i < bars) ? color : C_BORDER;
        s_gfx->fillRect(bx, by, bw, bh, bc);
    }
}

// Battery icon (simple, 28×14)
static void draw_battery(int16_t x, int16_t y, uint8_t pct) {
    uint16_t color = (pct > 40) ? C_OK : (pct > 20) ? C_WARN : C_ERR;
    s_gfx->drawRect(x, y, 24, 12, C_LGRAY);
    s_gfx->fillRect(x + 24, y + 3, 3, 6, C_LGRAY);
    int16_t fw = (int16_t)(20 * pct / 100);
    s_gfx->fillRect(x + 2, y + 2, 20, 8, C_BG);
    if (fw > 0)
        s_gfx->fillRect(x + 2, y + 2, fw, 8, color);
}

// ── Public: init ────────────────────────────────────────────────────────────

bool display_init() {
    Serial.println("[DISP] Initializing...");

    // Wire1 → board internal I2C (TCA9554, FT6336 touch)
    // Wire  → SHT35 (already initialized in sensors_init)
    Wire1.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire1.setClock(400000);

    if (!s_tca.begin()) {
        Serial.printf("[DISP] ERROR: TCA9554 not found at 0x%02X\n", TCA9554_ADDR);
        return false;
    }
    Serial.println("[DISP] TCA9554 OK");

    // P1 = LCD_RST,  P2 = LCD_CS
    s_tca.pinMode1(TCA_LCD_RST_PIN, OUTPUT);
    s_tca.pinMode1(TCA_LCD_CS_PIN,  OUTPUT);

    // Deassert CS first, then perform hardware reset
    s_tca.write1(TCA_LCD_CS_PIN,  HIGH);
    s_tca.write1(TCA_LCD_RST_PIN, LOW);
    delay(20);
    s_tca.write1(TCA_LCD_RST_PIN, HIGH);
    delay(150);

    // Assert CS permanently (ST7796 is the only device on this SPI bus)
    s_tca.write1(TCA_LCD_CS_PIN, LOW);

    // Backlight via PWM
    ledcSetup(LCD_BL_CHANNEL, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DUTY);

    // GFX bus (hardware SPI, CS = -1 = managed externally via TCA9554)
    s_bus = new Arduino_ESP32SPI(
        LCD_DC_PIN,           // DC
        GFX_NOT_DEFINED,      // CS  (held LOW by TCA9554)
        LCD_SCLK_PIN,         // SCK
        LCD_MOSI_PIN,         // MOSI
        LCD_MISO_PIN          // MISO
    );

    // ST7796, no RST pin (managed by TCA9554), portrait, IPS display
    s_gfx = new Arduino_ST7796(s_bus, GFX_NOT_DEFINED, 0, true);

    if (!s_gfx->begin(20000000UL)) {
        Serial.println("[DISP] ERROR: GFX begin failed");
        return false;
    }

    s_ok = true;
    s_gfx->fillScreen(C_BG);
    Serial.println("[DISP] Display OK");
    return true;
}

// ── Public: update ──────────────────────────────────────────────────────────

void display_update(const DisplayData &d) {
    if (!s_ok) return;

    char buf[40];

    // ── Full background ────────────────────────────────
    s_gfx->fillScreen(C_BG);

    // ════════════════════════════════════════════════════
    //  HEADER BAR  (y 0–40)
    // ════════════════════════════════════════════════════
    s_gfx->fillRect(0, 0, LCD_WIDTH, 40, C_PANEL);
    s_gfx->drawFastHLine(0, 40, LCD_WIDTH, C_BORDER);

    // Title
    s_gfx->setTextColor(C_ACCENT);
    s_gfx->setTextSize(2);
    s_gfx->setCursor(10, 11);
    s_gfx->print("NEOLINK V1");

    // Battery icon + percentage
    draw_battery(230, 14, d.battery_pct);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(258, 18);
    snprintf(buf, sizeof(buf), "%d%%", d.battery_pct);
    s_gfx->print(buf);

    // Signal bars
    draw_signal(287, 12, d.rssi);

    // ════════════════════════════════════════════════════
    //  TEMPERATURA  (y 52–182)
    // ════════════════════════════════════════════════════
    s_gfx->fillRoundRect(8, 52, LCD_WIDTH - 16, 130, 8, C_PANEL);
    s_gfx->drawRoundRect(8, 52, LCD_WIDTH - 16, 130, 8, C_BORDER);

    // Label
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setTextSize(1);
    s_gfx->setCursor(20, 64);
    s_gfx->print("TEMPERATURA");

    // Big value
    if (d.sht35_ok && !isnan(d.temp_sht35)) {
        snprintf(buf, sizeof(buf), "%.1f C", d.temp_sht35);
        print_centered(buf, 82, 5, C_TEMP);
    } else {
        print_centered("-- C", 82, 5, C_GRAY);
    }

    // Progress bar: -10 °C → 60 °C
    if (d.sht35_ok && !isnan(d.temp_sht35)) {
        uint8_t pct = (uint8_t)constrain(
            map((long)(d.temp_sht35 * 10), -100, 600, 0, 100), 0, 100);
        uint16_t bc = (d.temp_sht35 > TEMP_ALARM_HIGH ||
                       d.temp_sht35 < TEMP_ALARM_LOW) ? C_ERR : C_TEMP;
        draw_bar(20, 158, LCD_WIDTH - 40, 14, pct, bc);
    }

    // ════════════════════════════════════════════════════
    //  HUMEDAD  (y 194–324)
    // ════════════════════════════════════════════════════
    s_gfx->fillRoundRect(8, 194, LCD_WIDTH - 16, 130, 8, C_PANEL);
    s_gfx->drawRoundRect(8, 194, LCD_WIDTH - 16, 130, 8, C_BORDER);

    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setTextSize(1);
    s_gfx->setCursor(20, 206);
    s_gfx->print("HUMEDAD RELATIVA");

    if (d.sht35_ok && !isnan(d.humidity)) {
        snprintf(buf, sizeof(buf), "%.1f%%", d.humidity);
        print_centered(buf, 224, 5, C_HUM);
    } else {
        print_centered("--.-%% ", 224, 5, C_GRAY);
    }

    if (d.sht35_ok && !isnan(d.humidity)) {
        uint8_t pct = (uint8_t)constrain((int)d.humidity, 0, 100);
        draw_bar(20, 300, LCD_WIDTH - 40, 14, pct, C_HUM);
    }

    // ════════════════════════════════════════════════════
    //  STATUS  (y 336–454)
    // ════════════════════════════════════════════════════
    s_gfx->fillRoundRect(8, 336, LCD_WIDTH - 16, 118, 8, C_PANEL);
    s_gfx->drawRoundRect(8, 336, LCD_WIDTH - 16, 118, 8, C_BORDER);

    // Connection row
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(20, 350);
    s_gfx->print("RED  ");
    if (d.modem_connected) {
        s_gfx->setTextColor(C_OK);
        s_gfx->print("GPRS CONECTADO");
        if (d.rssi != 0) {
            s_gfx->setTextColor(C_LGRAY);
            snprintf(buf, sizeof(buf), "  %ddBm", d.rssi);
            s_gfx->print(buf);
        }
    } else {
        s_gfx->setTextColor(C_ERR);
        s_gfx->print("DESCONECTADO");
    }

    // Last sync row
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(20, 370);
    s_gfx->print("SYNC ");
    if (d.last_post_ms > 0) {
        uint32_t ago_s = (millis() - d.last_post_ms) / 1000;
        if (ago_s < 60)
            snprintf(buf, sizeof(buf), "hace %lus", (unsigned long)ago_s);
        else
            snprintf(buf, sizeof(buf), "hace %lum", (unsigned long)(ago_s / 60));
        s_gfx->setTextColor(C_WHITE);
        s_gfx->print(buf);
    } else {
        s_gfx->setTextColor(C_GRAY);
        s_gfx->print("---");
    }

    // Uptime row
    {
        uint32_t up = millis() / 1000;
        snprintf(buf, sizeof(buf), "UP   %02lu:%02lu:%02lu",
                 (unsigned long)(up / 3600),
                 (unsigned long)((up % 3600) / 60),
                 (unsigned long)(up % 60));
        s_gfx->setTextColor(C_GRAY);
        s_gfx->setCursor(20, 390);
        s_gfx->print(buf);
    }

    // Divider
    s_gfx->drawFastHLine(20, 408, LCD_WIDTH - 40, C_BORDER);

    // Battery bar + label
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(20, 420);
    s_gfx->print("BAT");
    {
        uint16_t bc = (d.battery_pct > 40) ? C_OK : C_WARN;
        draw_bar(52, 418, 200, 12, d.battery_pct, bc);
        snprintf(buf, sizeof(buf), "%d%%", d.battery_pct);
        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(256, 420);
        s_gfx->print(buf);
    }

    // Alarm indicator (bottom of status card)
    bool alarm = d.sht35_ok &&
                 (d.temp_sht35 > TEMP_ALARM_HIGH || d.temp_sht35 < TEMP_ALARM_LOW);
    if (alarm) {
        s_gfx->setTextColor(C_ERR);
        s_gfx->setCursor(20, 436);
        s_gfx->print("! ALARMA DE TEMPERATURA");
    }

    // ════════════════════════════════════════════════════
    //  FOOTER  (y 461–479)
    // ════════════════════════════════════════════════════
    s_gfx->setTextColor(C_GRAY);
    s_gfx->setTextSize(1);
    s_gfx->setCursor(8, 468);
    s_gfx->print(DEVICE_ID);
}
