// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//  Layout:
//    Header  (36px) : device | GSM bars | WiFi bars
//    Card 1 (200px) : T1 temp | T1 hum  (split) + dual chart
//    Card 2 (200px) : PT100 temp (N/A si no conectado) + chart
//    Footer  (44px) : batería + uptime
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

#include "FreeSansBold24pt7b.h"
#include "FreeSans9pt7b.h"

// ── Colores ────────────────────────────────────────────────────────────────
#define C_BG      0x0000   // negro
#define C_PANEL   0x0821   // azul muy oscuro
#define C_DIV     0x2104   // divisor gris oscuro
#define C_GRID    0x18A3   // guías del chart
#define C_WHITE   0xFFFF
#define C_GRAY    0x7BEF
#define C_LGRAY   0xAD55
#define C_CYAN    0x07FF   // T1 temperatura
#define C_TEAL    0x07EF   // T1 humedad
#define C_ORANGE  0xFCA0   // PT100
#define C_OK      0x07E0
#define C_WARN    0xFCC0
#define C_ERR     0xF800
#define C_BLUE    0x435F   // acento WiFi

// ── Layout ─────────────────────────────────────────────────────────────────
//  Portrait 320×480
#define HDR_Y      0
#define HDR_H      36
#define C1_Y       36
#define C1_H       200     //  36 → 235
#define C1_MID_X   160     // división izquierda/derecha
#define C1_VAL_H   100     // zona del número grande
#define C1_CHART_Y (C1_Y + C1_VAL_H + 18)   // 154
#define C1_CHART_H 62
#define DIV_Y      236
#define C2_Y       237
#define C2_H       200     // 237 → 436
#define C2_CHART_Y (C2_Y + 100 + 18)         // 355
#define C2_CHART_H 70
#define FTR_Y      437
#define FTR_H      43      // 437 → 479

// ── Historia sparklines ────────────────────────────────────────────────────
#define HIST_N     60

static float   s_t1[HIST_N];   // temperatura SHT35
static float   s_t2[HIST_N];   // humedad SHT35
static float   s_t3[HIST_N];   // temperatura PT100
static uint8_t s_hi    = 0;
static uint8_t s_hcnt  = 0;

// ── Hardware ───────────────────────────────────────────────────────────────
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus  = nullptr;
static Arduino_GFX     *s_gfx  = nullptr;
static bool             s_ok           = false;
static bool             s_static_done  = false;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Centra un string en el rango horizontal [x0, x1)
static void print_centered_range(const char *str, int16_t x0, int16_t x1,
                                  int16_t y, uint16_t color) {
    int16_t bx, by; uint16_t tw, th;
    s_gfx->getTextBounds(str, 0, y, &bx, &by, &tw, &th);
    int16_t cx = x0 + ((x1 - x0) - (int16_t)tw) / 2;
    s_gfx->setTextColor(color);
    s_gfx->setCursor(cx, y);
    s_gfx->print(str);
}

// Dibuja sparkline de UNA serie; normalización independiente
static void draw_series(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                         float *data, uint8_t cnt, uint16_t lc) {
    if (cnt < 2) return;

    float vmin = data[0], vmax = data[0];
    for (uint8_t i = 1; i < cnt; i++) {
        if (data[i] < vmin) vmin = data[i];
        if (data[i] > vmax) vmax = data[i];
    }
    float range = vmax - vmin;
    if (range < 0.1f) { vmin -= 0.5f; vmax += 0.5f; range = 1.0f; }

    uint8_t pts = (cnt < (uint8_t)cw) ? cnt : (uint8_t)cw;
    int16_t prev_px = -1, prev_py = -1;
    for (uint8_t i = 0; i < pts; i++) {
        uint8_t idx = (s_hi + HIST_N - pts + i) % HIST_N;
        float v = data[idx];
        float n = constrain((v - vmin) / range, 0.0f, 1.0f);
        int16_t px = cx + (int16_t)((float)i / (pts - 1) * (cw - 1));
        int16_t py = cy + ch - 1 - (int16_t)(n * (ch - 2));
        if (prev_px >= 0) {
            s_gfx->drawLine(prev_px, prev_py,     px, py,     lc);
            s_gfx->drawLine(prev_px, prev_py + 1, px, py + 1, lc);
        }
        prev_px = px; prev_py = py;
    }
}

// Chart con una o dos series
static void draw_chart(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                        float *d1, float *d2, uint8_t cnt,
                        uint16_t c1, uint16_t c2) {
    s_gfx->fillRect(cx, cy, cw, ch, C_BG);
    // Guías horizontales
    for (uint8_t i = 1; i <= 3; i++)
        s_gfx->drawFastHLine(cx, cy + ch * i / 4, cw, C_GRID);

    draw_series(cx, cy, cw, ch, d1, cnt, c1);
    if (d2) draw_series(cx, cy, cw, ch, d2, cnt, c2);
}

// Barras de señal (4 barras verticales, ancho=4 alto variable)
static void draw_signal_bars(int16_t x, int16_t y, int8_t rssi,
                              bool connected, uint16_t color) {
    uint8_t bars = 0;
    if (connected) {
        if      (rssi >= -65) bars = 4;
        else if (rssi >= -75) bars = 3;
        else if (rssi >= -85) bars = 2;
        else                  bars = 1;
    }
    for (uint8_t i = 0; i < 4; i++) {
        int16_t bh = 4 + i * 5;
        int16_t bx = x + i * 7;
        int16_t by = y + (20 - bh);
        s_gfx->fillRect(bx, by, 5, bh, (i < bars) ? color : C_DIV);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════════════════════
bool display_init() {
    Serial.println("[DISP] Init...");

    Wire1.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire1.setClock(400000);

    if (!s_tca.begin()) {
        Serial.printf("[DISP] ERROR: TCA9554 no encontrado (0x%02X)\n", TCA9554_ADDR);
        return false;
    }

    s_tca.pinMode1(TCA_LCD_RST_PIN, OUTPUT);
    s_tca.pinMode1(TCA_LCD_CS_PIN,  OUTPUT);
    s_tca.write1(TCA_LCD_CS_PIN,  HIGH);
    s_tca.write1(TCA_LCD_RST_PIN, LOW);
    delay(20);
    s_tca.write1(TCA_LCD_RST_PIN, HIGH);
    delay(150);
    s_tca.write1(TCA_LCD_CS_PIN, LOW);  // CS activo permanente

    ledcSetup(LCD_BL_CHANNEL, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DUTY);

    s_bus = new Arduino_ESP32SPI(
        LCD_DC_PIN, GFX_NOT_DEFINED, LCD_SCLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN);
    s_gfx = new Arduino_ST7796(s_bus, GFX_NOT_DEFINED, 0, true);

    if (!s_gfx->begin(20000000UL)) {
        Serial.println("[DISP] ERROR: GFX begin failed");
        return false;
    }

    memset(s_t1, 0, sizeof(s_t1));
    memset(s_t2, 0, sizeof(s_t2));
    memset(s_t3, 0, sizeof(s_t3));

    s_ok = true;
    s_gfx->fillScreen(C_BG);
    Serial.println("[DISP] OK");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ESTÁTICO — solo se dibuja una vez
// ═══════════════════════════════════════════════════════════════════════════
static void draw_static() {
    s_gfx->fillScreen(C_BG);

    // Header fondo
    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_PANEL);

    // Divisores
    s_gfx->drawFastHLine(0, HDR_H,   LCD_WIDTH, C_DIV);
    s_gfx->drawFastHLine(0, DIV_Y,   LCD_WIDTH, C_DIV);
    s_gfx->drawFastHLine(0, FTR_Y,   LCD_WIDTH, C_DIV);
    // Divisor vertical en Card 1
    s_gfx->drawFastVLine(C1_MID_X, C1_Y, C1_VAL_H + 16, C_DIV);

    // Labels de sección (FreeSans9pt)
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);

    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(10, C1_Y + 18);
    s_gfx->print("T1  TEMPERATURA");

    s_gfx->setCursor(C1_MID_X + 10, C1_Y + 18);
    s_gfx->print("T1  HUMEDAD");

    s_gfx->setCursor(10, C2_Y + 18);
    s_gfx->print("T2  PT100");

    // Footer fondo
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_PANEL);

    s_gfx->setFont(nullptr);
    s_static_done = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════════════════
void display_update(const DisplayData &d) {
    if (!s_ok) return;
    if (!s_static_done) draw_static();

    // Acumula historial
    s_t1[s_hi] = d.sht35_ok ? d.temp_sht35 : (s_hcnt > 0 ? s_t1[(s_hi + HIST_N - 1) % HIST_N] : 20.0f);
    s_t2[s_hi] = d.sht35_ok ? d.humidity   : (s_hcnt > 0 ? s_t2[(s_hi + HIST_N - 1) % HIST_N] : 50.0f);
    s_t3[s_hi] = d.pt100_ok  ? d.temp_pt100 : (s_hcnt > 0 ? s_t3[(s_hi + HIST_N - 1) % HIST_N] : 0.0f);
    s_hi = (s_hi + 1) % HIST_N;
    if (s_hcnt < HIST_N) s_hcnt++;

    char buf[32];

    // ── HEADER ──────────────────────────────────────────────────────────
    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_PANEL);

    // Device name (left)
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_WHITE);
    s_gfx->setCursor(8, HDR_Y + 22);
    s_gfx->print(DEVICE_ID);

    // GSM signal bars (right, near center)
    draw_signal_bars(196, HDR_Y + 8, d.gsm_rssi, d.gsm_connected, C_OK);

    // WiFi signal bars
    if (d.wifi_ap_mode) {
        // En modo AP: ícono wifi diferente (azul, parpadeante no, solo color)
        draw_signal_bars(228, HDR_Y + 8, -50, true, C_BLUE);
    } else {
        draw_signal_bars(228, HDR_Y + 8, d.wifi_rssi, d.wifi_connected, C_BLUE);
    }

    // Batería (esquina derecha)
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(d.battery_pct > 20 ? C_OK : C_WARN);
    s_gfx->setCursor(264, HDR_Y + 22);
    snprintf(buf, sizeof(buf), "%d%%", d.battery_pct);
    s_gfx->print(buf);

    // ── CARD 1: SHT35 ───────────────────────────────────────────────────
    // Zona izquierda: temperatura
    s_gfx->fillRect(0, C1_Y + 22, C1_MID_X, C1_VAL_H - 8, C_BG);
    s_gfx->setFont(&FreeSansBold24pt7b);
    s_gfx->setTextSize(1);
    if (d.sht35_ok && !isnan(d.temp_sht35)) {
        snprintf(buf, sizeof(buf), "%.1f", d.temp_sht35);
        print_centered_range(buf, 4, C1_MID_X - 4,
                             C1_Y + 22 + 44, C_WHITE);
    } else {
        print_centered_range("N/A", 4, C1_MID_X - 4,
                             C1_Y + 22 + 44, C_GRAY);
    }
    // Unidad °C (pequeño, debajo del número)
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(C1_MID_X / 2 - 10, C1_Y + 22 + 44 + 18);
    s_gfx->print("grados C");

    // Zona derecha: humedad
    s_gfx->fillRect(C1_MID_X + 1, C1_Y + 22, C1_MID_X - 1, C1_VAL_H - 8, C_BG);
    s_gfx->setFont(&FreeSansBold24pt7b);
    s_gfx->setTextSize(1);
    if (d.sht35_ok && !isnan(d.humidity)) {
        snprintf(buf, sizeof(buf), "%.1f%%", d.humidity);
        print_centered_range(buf, C1_MID_X + 4, LCD_WIDTH - 4,
                             C1_Y + 22 + 44, C_WHITE);
    } else {
        print_centered_range("N/A", C1_MID_X + 4, LCD_WIDTH - 4,
                             C1_Y + 22 + 44, C_GRAY);
    }
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(C1_MID_X + C1_MID_X / 2 - 10, C1_Y + 22 + 44 + 18);
    s_gfx->print("% HR");

    // Chart Card 1 (dos líneas: cyan=temp, teal=hum)
    draw_chart(10, C1_CHART_Y, LCD_WIDTH - 20, C1_CHART_H,
               s_t1, s_t2, s_hcnt, C_CYAN, C_TEAL);

    // ── CARD 2: PT100 ───────────────────────────────────────────────────
    s_gfx->fillRect(0, C2_Y + 22, LCD_WIDTH, 95, C_BG);
    s_gfx->setFont(&FreeSansBold24pt7b);
    s_gfx->setTextSize(1);

    if (d.pt100_ok && !isnan(d.temp_pt100)) {
        snprintf(buf, sizeof(buf), "%.1f C", d.temp_pt100);
        print_centered_range(buf, 4, LCD_WIDTH - 4,
                             C2_Y + 22 + 50, C_WHITE);
    } else {
        // No conectado — mostrar N/A en gris
        print_centered_range("N/A", 4, LCD_WIDTH - 4,
                             C2_Y + 22 + 50, C_GRAY);
        // Mensaje pequeño
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_DIV);
        s_gfx->setCursor(80, C2_Y + 22 + 50 + 22);
        s_gfx->print("sonda no conectada");
    }

    // Chart Card 2 (solo PT100 si disponible)
    if (d.pt100_ok) {
        draw_chart(10, C2_CHART_Y, LCD_WIDTH - 20, C2_CHART_H,
                   s_t3, nullptr, s_hcnt, C_ORANGE, 0);
    } else {
        // Grid vacío
        s_gfx->fillRect(10, C2_CHART_Y, LCD_WIDTH - 20, C2_CHART_H, C_BG);
        for (uint8_t i = 1; i <= 3; i++)
            s_gfx->drawFastHLine(10, C2_CHART_Y + C2_CHART_H * i / 4,
                                  LCD_WIDTH - 20, C_GRID);
    }

    // ── FOOTER ──────────────────────────────────────────────────────────
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_PANEL);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);

    // Estado red
    s_gfx->setCursor(8, FTR_Y + 26);
    if (d.wifi_ap_mode) {
        s_gfx->setTextColor(C_BLUE);
        s_gfx->print("AP: NEOLINK-SETUP");
    } else if (d.wifi_connected) {
        s_gfx->setTextColor(C_BLUE);
        s_gfx->print("WiFi");
        if (d.gsm_connected) {
            s_gfx->setTextColor(C_DIV);
            s_gfx->print(" + ");
            s_gfx->setTextColor(C_OK);
            s_gfx->print("GSM");
        }
    } else if (d.gsm_connected) {
        s_gfx->setTextColor(C_OK);
        s_gfx->print("GSM");
        snprintf(buf, sizeof(buf), " %ddBm", d.gsm_rssi);
        s_gfx->setTextColor(C_LGRAY);
        s_gfx->print(buf);
    } else {
        s_gfx->setTextColor(C_ERR);
        s_gfx->print("SIN RED");
    }

    // Tiempo / sync (derecha)
    {
        uint32_t up = millis() / 1000;
        if (d.last_post_ms > 0) {
            uint32_t ago = (millis() - d.last_post_ms) / 1000;
            if (ago < 60)
                snprintf(buf, sizeof(buf), "sync %lus", (unsigned long)ago);
            else
                snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                         (unsigned long)(up / 3600),
                         (unsigned long)((up % 3600) / 60),
                         (unsigned long)(up % 60));
        } else {
            snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                     (unsigned long)(up / 3600),
                     (unsigned long)((up % 3600) / 60),
                     (unsigned long)(up % 60));
        }
        s_gfx->setTextColor(C_LGRAY);
        s_gfx->setCursor(190, FTR_Y + 26);
        s_gfx->print(buf);
    }

    s_gfx->setFont(nullptr);
}
