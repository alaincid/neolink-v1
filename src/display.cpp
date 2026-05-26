// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//
//  Anti-flicker: solo se borra/redibuja la región que cambió:
//    - Header : cuando cambia señal / modo AP
//    - Label  : cuando cambia el sensor o el modo alt (temp↔hum)
//    - Número : cuando cambia el string formateado
//    - Chart  : siempre (maneja su propio fondo)
//    - Footer : solo la zona del tiempo (cambia cada segundo)
//
//  Cards:
//    Card 1 = SHT35  →  alterna temperatura 5s / humedad 5s
//    Card 2 = PT100  →  temperatura fija  (N/A si no conectado)
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include "FreeSans24pt7b.h"
#include "FreeSans9pt7b.h"

// ── Colores RGB565 ─────────────────────────────────────────────────────────
#define C_BG      0x0000   // negro puro (todo el fondo, header, footer)
#define C_DIVID   0x18C3   // divisor sutil
#define C_WHITE   0xFFFF   // números
#define C_LABEL   0x5ACF   // etiquetas T1/T2 (azul-gris)
#define C_UNIT    0x7BEF   // unidad °C / % (gris)
#define C_DIM     0x4208   // N/A
#define C_GRID    0x0861   // guías de chart
#define C_LINE1   0x4EDF   // sparkline card 1 (azul claro — referencia)
#define C_LINE2   0x27DF   // sparkline card 2
#define C_BARON   0xFFFF   // barras señal activas (blanco)
#define C_BAROFF  0x1082   // barras inactivas
#define C_WIFION  0x3D9F   // WiFi bars (azul suave)

// ── Layout 320×480 ────────────────────────────────────────────────────────
#define HDR_Y   0
#define HDR_H   40
#define C1_Y    41
#define C1_H    198
#define C2_Y    240
#define C2_H    198
#define FTR_Y   439
#define FTR_H   41

// Offsets dentro de cada card
#define LBL_DY   22    // baseline etiqueta
#define NUM_DY   80    // baseline número grande
#define UNT_DY   58    // baseline unidad (sobre el número)
#define SUB_DY   99    // baseline subtítulo (N/A msg)
#define CHT_TOP  114   // inicio chart
#define CHT_H    81    // alto chart

// ── Historial sparkline ────────────────────────────────────────────────────
#define HIST_N  60
static float   s_h1[HIST_N];
static float   s_h2[HIST_N];
static uint8_t s_hi   = 0;
static uint8_t s_hcnt = 0;

// ── Alt 5s para SHT35 ─────────────────────────────────────────────────────
#define ALT_MS  5000
static uint32_t s_alt_t   = 0;
static bool     s_alt_hum = false;   // false=temp, true=hum

// ── Estado previo para anti-flicker ──────────────────────────────────────
static char    s_c1_lbl_prev[24]  = "";
static char    s_c1_num_prev[16]  = "";
static char    s_c2_lbl_prev[24]  = "";
static char    s_c2_num_prev[16]  = "";
static int8_t  s_hdr_gsm_prev     = 127;   // sentinel
static bool    s_hdr_gc_prev      = false;
static bool    s_hdr_wc_prev      = false;
static bool    s_hdr_ap_prev      = false;
static uint8_t s_bat_prev         = 255;   // sentinel
static uint32_t s_ftr_last_s      = 0xFFFFFFFF;

// ── Hardware ──────────────────────────────────────────────────────────────
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;
static bool             s_ok          = false;
static bool             s_static_done = false;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static int16_t cprint(const char *s, int16_t x0, int16_t x1,
                       int16_t y, uint16_t c) {
    int16_t bx, by; uint16_t tw, th;
    s_gfx->getTextBounds(s, 0, y, &bx, &by, &tw, &th);
    int16_t cx = x0 + ((int16_t)(x1 - x0) - (int16_t)tw) / 2;
    s_gfx->setTextColor(c);
    s_gfx->setCursor(cx, y);
    s_gfx->print(s);
    return cx + (int16_t)tw;
}

// Barras de señal — 4 barras blancas ascendentes (igual que referencia)
static void draw_bars(int16_t x, int16_t y,
                       int8_t rssi, bool conn, uint16_t col_on) {
    const uint8_t H[4] = {5, 9, 14, 19};
    uint8_t filled = 0;
    if (conn) {
        if      (rssi >= -65) filled = 4;
        else if (rssi >= -75) filled = 3;
        else if (rssi >= -85) filled = 2;
        else                  filled = 1;
    }
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t c = (i < filled) ? col_on : C_BAROFF;
        s_gfx->fillRect(x + i * 7, y + (19 - H[i]), 5, H[i], c);
    }
}

// Ícono batería — relleno BLANCO (sin colores, igual que referencia)
static void draw_battery(int16_t x, int16_t y, uint8_t pct) {
    s_gfx->drawRect(x, y, 24, 12, C_UNIT);        // outline gris
    s_gfx->fillRect(x + 24, y + 3, 3, 6, C_UNIT); // polo +
    int16_t fw = (int16_t)(20 * pct / 100);
    if (fw > 0) s_gfx->fillRect(x + 2, y + 2, fw, 8, C_WHITE); // relleno blanco
}

// Sparkline + valores min/max en esquinas del chart
static void draw_sparkline(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                             float *data, uint8_t cnt, uint16_t lc) {
    s_gfx->fillRect(cx, cy, cw, ch, C_BG);

    // Grid lines (3 horizontales)
    for (uint8_t i = 1; i <= 3; i++)
        s_gfx->drawFastHLine(cx, cy + ch * i / 4, cw, C_GRID);

    if (cnt < 2) return;

    float vmin = data[0], vmax = data[0];
    for (uint8_t i = 1; i < cnt; i++) {
        if (data[i] < vmin) vmin = data[i];
        if (data[i] > vmax) vmax = data[i];
    }
    float range = vmax - vmin;
    if (range < 0.5f) { vmin -= 0.5f; vmax += 0.5f; range = 1.0f; }

    uint8_t pts = (cnt < (uint8_t)(cw / 2 + 1)) ? cnt : (uint8_t)(cw / 2);
    int16_t px0 = -1, py0 = -1;
    for (uint8_t i = 0; i < pts; i++) {
        uint8_t idx = (s_hi + HIST_N - pts + i) % HIST_N;
        float   n   = constrain((data[idx] - vmin) / range, 0.f, 1.f);
        int16_t px  = cx + (int16_t)((float)i / (float)(pts - 1) * (float)(cw - 1));
        int16_t py  = cy + ch - 3 - (int16_t)(n * (float)(ch - 6));
        if (px0 >= 0) {
            s_gfx->drawLine(px0, py0,     px, py,     lc);
            s_gfx->drawLine(px0, py0 + 1, px, py + 1, lc);
        }
        px0 = px; py0 = py;
    }

    // Valores min/max (derecha del chart, font default 5×8)
    char lb[10];
    s_gfx->setFont(nullptr);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LABEL);
    snprintf(lb, sizeof(lb), "%.1f", vmax);
    s_gfx->setCursor(cx + cw - (int16_t)(strlen(lb) * 6) - 2, cy + 2);
    s_gfx->print(lb);
    snprintf(lb, sizeof(lb), "%.1f", vmin);
    s_gfx->setCursor(cx + cw - (int16_t)(strlen(lb) * 6) - 2, cy + ch - 10);
    s_gfx->print(lb);
}

// ── Dibuja solo la etiqueta de una card ───────────────────────────────────
static void redraw_label(int16_t cy, const char *lbl) {
    s_gfx->fillRect(0, cy, LCD_WIDTH, LBL_DY + 4, C_BG);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LABEL);
    s_gfx->setCursor(12, cy + LBL_DY);
    s_gfx->print(lbl);
}

// ── Dibuja solo la zona del número de una card ────────────────────────────
static void redraw_number(int16_t cy, bool ok, float val, const char *unit) {
    // Limpia solo la zona número+unidad+subtítulo (entre label y chart)
    s_gfx->fillRect(0, cy + LBL_DY + 4, LCD_WIDTH,
                    CHT_TOP - LBL_DY - 4, C_BG);

    if (ok) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", val);

        // Mide número en FreeSans24pt
        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        int16_t bx, by; uint16_t tw, th;
        s_gfx->getTextBounds(buf, 0, cy + NUM_DY, &bx, &by, &tw, &th);

        // Mide unidad en FreeSans9pt
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        int16_t ux, uy; uint16_t utw, uth;
        s_gfx->getTextBounds(unit, 0, cy + UNT_DY, &ux, &uy, &utw, &uth);

        // Centra el bloque [número + unidad]
        int16_t block_w = (int16_t)tw + 4 + (int16_t)utw;
        int16_t sx = (LCD_WIDTH - block_w) / 2;

        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(sx, cy + NUM_DY);
        s_gfx->print(buf);

        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_UNIT);
        s_gfx->setCursor(sx + (int16_t)tw + 4, cy + UNT_DY);
        s_gfx->print(unit);
    } else {
        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        cprint("N/A", 0, LCD_WIDTH, cy + NUM_DY, C_DIM);
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        cprint("sensor no conectado", 0, LCD_WIDTH, cy + SUB_DY, C_DIVID);
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
        Serial.printf("[DISP] TCA9554 0x%02X no encontrado\n", TCA9554_ADDR);
        return false;
    }

    s_tca.pinMode1(TCA_LCD_RST_PIN, OUTPUT);
    s_tca.pinMode1(TCA_LCD_CS_PIN,  OUTPUT);
    s_tca.write1(TCA_LCD_CS_PIN,  HIGH);
    s_tca.write1(TCA_LCD_RST_PIN, LOW);  delay(20);
    s_tca.write1(TCA_LCD_RST_PIN, HIGH); delay(150);
    s_tca.write1(TCA_LCD_CS_PIN,  LOW);

    ledcSetup(LCD_BL_CHANNEL, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DUTY);

    s_bus = new Arduino_ESP32SPI(
        LCD_DC_PIN, GFX_NOT_DEFINED, LCD_SCLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN);
    s_gfx = new Arduino_ST7796(s_bus, GFX_NOT_DEFINED, 0, true);

    if (!s_gfx->begin(20000000UL)) {
        Serial.println("[DISP] GFX begin falló");
        return false;
    }

    memset(s_h1, 0, sizeof(s_h1));
    memset(s_h2, 0, sizeof(s_h2));

    s_ok = true;
    s_gfx->fillScreen(C_BG);
    Serial.println("[DISP] OK");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ESTÁTICO  (una sola vez — solo divisores)
// ═══════════════════════════════════════════════════════════════════════════
static void draw_static() {
    s_gfx->fillScreen(C_BG);
    // Divisores 1px entre secciones
    s_gfx->drawFastHLine(0, C1_Y - 1,  LCD_WIDTH, C_DIVID);
    s_gfx->drawFastHLine(0, C2_Y - 1,  LCD_WIDTH, C_DIVID);
    s_gfx->drawFastHLine(0, FTR_Y - 1, LCD_WIDTH, C_DIVID);
    s_gfx->setFont(nullptr);
    s_static_done = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE  — anti-flicker real
// ═══════════════════════════════════════════════════════════════════════════
void display_update(const DisplayData &d) {
    if (!s_ok) return;
    if (!s_static_done) draw_static();

    bool has_sht = d.sht35_ok && !isnan(d.temp_sht35) && !isnan(d.humidity);
    bool has_pt  = d.pt100_ok && !isnan(d.temp_pt100);

    // ── Alt 5s SHT35 ──────────────────────────────────────────────────────
    if (millis() - s_alt_t >= ALT_MS) {
        s_alt_hum = !s_alt_hum;
        s_alt_t   = millis();
    }

    // ── Card 1: SHT35 (alterna) ───────────────────────────────────────────
    bool        c1_ok;
    float       c1_val;
    const char *c1_unit, *c1_lbl;

    if (has_sht) {
        c1_ok  = true;
        if (!s_alt_hum) { c1_val = d.temp_sht35; c1_unit = " C";  c1_lbl = "T1  TEMPERATURA"; }
        else             { c1_val = d.humidity;   c1_unit = " %";  c1_lbl = "T1  HUMEDAD";     }
    } else {
        c1_ok = false; c1_val = 0; c1_unit = ""; c1_lbl = "T1  SHT35";
    }

    // ── Card 2: PT100 (fija o N/A) ────────────────────────────────────────
    bool        c2_ok;
    float       c2_val;
    const char *c2_unit, *c2_lbl;

    if (has_pt) {
        c2_ok = true; c2_val = d.temp_pt100; c2_unit = " C"; c2_lbl = "T2  PT100";
    } else {
        c2_ok = false; c2_val = 0; c2_unit = ""; c2_lbl = "T2  PT100";
    }

    // ── Historial (siempre acumula) ───────────────────────────────────────
    float raw1 = has_sht ? d.temp_sht35 : (has_pt  ? d.temp_pt100 : 0);
    float raw2 = has_pt  ? d.temp_pt100 : (has_sht ? d.humidity   : 0);
    s_h1[s_hi] = raw1;
    s_h2[s_hi] = raw2;
    s_hi = (s_hi + 1) % HIST_N;
    if (s_hcnt < HIST_N) s_hcnt++;

    // ── HEADER (solo si cambió algo) ──────────────────────────────────────
    bool hdr_changed = (d.gsm_rssi    != s_hdr_gsm_prev) ||
                       (d.gsm_connected != s_hdr_gc_prev) ||
                       (d.wifi_connected != s_hdr_wc_prev) ||
                       (d.wifi_ap_mode   != s_hdr_ap_prev);

    if (hdr_changed || !s_static_done) {
        s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_BG);

        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(12, HDR_Y + 25);
        s_gfx->print(DEVICE_ID);

        // GSM (derecha)
        draw_bars(LCD_WIDTH - 36, HDR_Y + 11, d.gsm_rssi, d.gsm_connected, C_BARON);

        // WiFi (a la izquierda de GSM)
        if (d.wifi_connected && !d.wifi_ap_mode) {
            draw_bars(LCD_WIDTH - 72, HDR_Y + 11, d.wifi_rssi, true, C_WIFION);
        } else if (d.wifi_ap_mode) {
            s_gfx->setFont(nullptr);
            s_gfx->setTextSize(1);
            s_gfx->setTextColor(C_WIFION);
            s_gfx->setCursor(LCD_WIDTH - 60, HDR_Y + 22);
            s_gfx->print("AP");
        }

        s_hdr_gsm_prev = d.gsm_rssi;
        s_hdr_gc_prev  = d.gsm_connected;
        s_hdr_wc_prev  = d.wifi_connected;
        s_hdr_ap_prev  = d.wifi_ap_mode;
    }

    // ── CARD 1 LABEL (solo si cambió la etiqueta) ─────────────────────────
    if (strcmp(c1_lbl, s_c1_lbl_prev) != 0) {
        redraw_label(C1_Y, c1_lbl);
        strlcpy(s_c1_lbl_prev, c1_lbl, sizeof(s_c1_lbl_prev));
    }

    // ── CARD 1 NÚMERO (solo si cambió el valor formateado) ────────────────
    char c1_numstr[16];
    if (c1_ok) snprintf(c1_numstr, sizeof(c1_numstr), "%.1f%s", c1_val, c1_unit);
    else       strlcpy(c1_numstr, "N/A", sizeof(c1_numstr));

    if (strcmp(c1_numstr, s_c1_num_prev) != 0) {
        redraw_number(C1_Y, c1_ok, c1_val, c1_unit);
        strlcpy(s_c1_num_prev, c1_numstr, sizeof(s_c1_num_prev));
    }

    // ── CARD 1 CHART (siempre — maneja su fondo) ──────────────────────────
    draw_sparkline(10, C1_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H,
                   s_h1, s_hcnt, C_LINE1);

    // ── CARD 2 LABEL ──────────────────────────────────────────────────────
    if (strcmp(c2_lbl, s_c2_lbl_prev) != 0) {
        redraw_label(C2_Y, c2_lbl);
        strlcpy(s_c2_lbl_prev, c2_lbl, sizeof(s_c2_lbl_prev));
    }

    // ── CARD 2 NÚMERO ─────────────────────────────────────────────────────
    char c2_numstr[16];
    if (c2_ok) snprintf(c2_numstr, sizeof(c2_numstr), "%.1f%s", c2_val, c2_unit);
    else       strlcpy(c2_numstr, "N/A", sizeof(c2_numstr));

    if (strcmp(c2_numstr, s_c2_num_prev) != 0) {
        redraw_number(C2_Y, c2_ok, c2_val, c2_unit);
        strlcpy(s_c2_num_prev, c2_numstr, sizeof(s_c2_num_prev));
    }

    // ── CARD 2 CHART ──────────────────────────────────────────────────────
    draw_sparkline(10, C2_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H,
                   s_h2, s_hcnt, C_LINE2);

    // ── FOOTER: batería (solo si cambió) + tiempo (cada segundo) ─────────
    uint32_t cur_s = millis() / 1000;

    if (d.battery_pct != s_bat_prev) {
        // Redibuja zona batería
        s_gfx->fillRect(0, FTR_Y, 90, FTR_H, C_BG);
        draw_battery(10, FTR_Y + 15, d.battery_pct);
        char bb[8];
        snprintf(bb, sizeof(bb), " %d%%", d.battery_pct);
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_UNIT);
        s_gfx->setCursor(37, FTR_Y + 27);
        s_gfx->print(bb);
        s_bat_prev = d.battery_pct;
    }

    if (cur_s != s_ftr_last_s) {
        // Redibuja solo la zona del tiempo (derecha del footer)
        s_gfx->fillRect(130, FTR_Y, LCD_WIDTH - 130, FTR_H, C_BG);
        char buf[20];
        if (d.last_post_ms > 0) {
            uint32_t ago = (millis() - d.last_post_ms) / 1000;
            if (ago < 60)
                snprintf(buf, sizeof(buf), "sync %lus", (unsigned long)ago);
            else
                snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                         (unsigned long)(cur_s / 3600),
                         (unsigned long)((cur_s % 3600) / 60),
                         (unsigned long)(cur_s % 60));
        } else {
            snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                     (unsigned long)(cur_s / 3600),
                     (unsigned long)((cur_s % 3600) / 60),
                     (unsigned long)(cur_s % 60));
        }
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_UNIT);
        s_gfx->setCursor(185, FTR_Y + 27);
        s_gfx->print(buf);
        s_ftr_last_s = cur_s;
    }

    s_gfx->setFont(nullptr);
}
