// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//
//  Cards dinámicas:
//    Card 1 = SHT35 (S1): alterna temp 5s / hum 5s + gráfica propia
//    Card 2 = PT100 (S2): temperatura fija | N/A si no conectado
//
//  Header: nombre + ícono WiFi fan (arcos) + barras GSM
//  Footer: batería blanca + uptime HH:MM:SS
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <time.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include "FreeSans24pt7b.h"
#include "FreeSans9pt7b.h"

// ── Colores ────────────────────────────────────────────────────────────────
#define C_BG      0x0000
#define C_DIVID   0x18C3
#define C_WHITE   0xFFFF
#define C_LABEL   0x4A69   // etiquetas S1/S2 (azul-gris)
#define C_UNIT    0x7BEF   // unidad gris
#define C_DIM     0x2965   // N/A
#define C_GRID    0x0861
#define C_LINE1   0xCE59   // sparkline S1: gris plateado
#define C_LINE2   0xB5B6   // sparkline S2: gris ligeramente diferente
#define C_BARON   0xFFFF   // barras GSM activas
#define C_BAROFF  0x1082   // inactivas
#define C_WIFION  0xFFFF
#define C_WIFIOFF 0x18C3   // arcos WiFi inactivos (más visible que 0x1082)

// ── Layout 320×480 ────────────────────────────────────────────────────────
#define HDR_Y   0
#define HDR_H   40
#define C1_Y    41
#define C1_H    198
#define C2_Y    240
#define C2_H    198
#define FTR_Y   439
#define FTR_H   41

// Offsets dentro de cada card (y relativo al cy del card)
// FreeSans9pt7b: setCursor y = baseline; ascender ~13px, descender ~3px
#define LBL_OFF  20    // baseline de la etiqueta (texto arranca ~7px desde cy)
#define LBL_H    26    // área reservada para la etiqueta
#define NUM_DY   82    // baseline número grande
#define UNT_DY   60    // baseline unidad
#define SUB_DY   100   // baseline "sensor no conectado"
#define CHT_TOP  112   // inicio chart
#define CHT_H    83    // alto chart

// ── Historial independiente ────────────────────────────────────────────────
#define HIST_N  60
static float   s_h_temp[HIST_N];   // SHT35 temperatura
static float   s_h_hum[HIST_N];    // SHT35 humedad
static float   s_h_pt[HIST_N];     // PT100 temperatura
static uint8_t s_hi   = 0;
static uint8_t s_hcnt = 0;

// ── Alt 5s ────────────────────────────────────────────────────────────────
#define ALT_MS  5000
static uint32_t s_alt_t   = 0;
static bool     s_alt_hum = false;

// ── Estado previo (anti-flicker) ──────────────────────────────────────────
static char     s_c1_lbl_prev[24] = "";
static char     s_c1_num_prev[16] = "";
static char     s_c2_lbl_prev[24] = "";
static char     s_c2_num_prev[16] = "";
static bool     s_c1_alt_prev     = false;  // para saber si cambió el modo
static int8_t   s_hdr_gsm_prev    = 127;
static bool     s_hdr_gc_prev     = false;
static bool     s_hdr_wc_prev     = false;
static bool     s_hdr_ap_prev     = false;
static int8_t   s_hdr_wrssi_prev  = 127;
static uint8_t  s_bat_prev        = 255;
static uint32_t s_ftr_last_s      = 0xFFFFFFFF;

// ── Hardware ──────────────────────────────────────────────────────────────
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus         = nullptr;
static Arduino_GFX     *s_gfx         = nullptr;
static bool             s_ok          = false;
static bool             s_static_done = false;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static void cprint_center(const char *s, int16_t y, uint16_t c) {
    int16_t bx, by; uint16_t tw, th;
    s_gfx->getTextBounds(s, 0, y, &bx, &by, &tw, &th);
    int16_t cx = ((int16_t)LCD_WIDTH - (int16_t)tw) / 2;
    s_gfx->setTextColor(c);
    s_gfx->setCursor(cx, y);
    s_gfx->print(s);
}

// Barras GSM — 4 barras blancas ascendentes
static void draw_gsm_bars(int16_t x, int16_t y, int8_t rssi, bool conn) {
    const uint8_t H[4] = {5, 9, 14, 19};
    uint8_t filled = 0;
    if (conn) {
        if      (rssi >= -65) filled = 4;
        else if (rssi >= -75) filled = 3;
        else if (rssi >= -85) filled = 2;
        else                  filled = 1;
    }
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t c = (i < filled) ? C_BARON : C_BAROFF;
        s_gfx->fillRect(x + i * 7, y + (19 - H[i]), 5, H[i], c);
    }
}

// Ícono WiFi fan — 3 arcos concéntricos + punto
//   bars = 0 → todo dim; 1..3 → arcos activos de dentro a fuera
static void draw_wifi_icon(int16_t x, int16_t y, uint8_t bars) {
    const int16_t cx = x + 11;
    const int16_t cy = y + 15;

    s_gfx->fillRect(x, y, 23, 18, C_BG);

    const uint8_t radii[3] = {5, 9, 13};
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t col = (i < bars) ? C_WIFION : C_WIFIOFF;
        float r = (float)radii[i];
        for (int16_t a = 212; a <= 328; a += 3) {
            float ra = (float)a * PI / 180.0f;
            int16_t px = cx + (int16_t)(r * cosf(ra));
            int16_t py = cy + (int16_t)(r * sinf(ra));
            s_gfx->drawPixel(px, py, col);
            s_gfx->drawPixel(px, py - 1, col);
        }
    }
    uint16_t dc = (bars > 0) ? C_WIFION : C_WIFIOFF;
    s_gfx->fillCircle(cx, cy, 2, dc);
}

// Batería (relleno blanco, sin colores)
static void draw_battery(int16_t x, int16_t y, uint8_t pct) {
    s_gfx->drawRect(x, y, 24, 12, C_UNIT);
    s_gfx->fillRect(x + 24, y + 3, 3, 6, C_UNIT);
    int16_t fw = (int16_t)(20 * pct / 100);
    if (fw > 0) s_gfx->fillRect(x + 2, y + 2, fw, 8, C_WHITE);
}

// Sparkline + min/max
static void draw_sparkline(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                             float *data, uint8_t cnt, uint16_t lc) {
    s_gfx->fillRect(cx, cy, cw, ch, C_BG);
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

// ── Etiqueta del card (FreeSans9pt7b — proporcional, bonita) ──────────────
static void redraw_label(int16_t cy, const char *lbl) {
    // FreeSans9pt7b: cursor y = baseline; glifo ocupa baseline-13 a baseline+3
    s_gfx->fillRect(0, cy + 4, LCD_WIDTH, LBL_H, C_BG);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LABEL);
    s_gfx->setCursor(14, cy + LBL_OFF);
    s_gfx->print(lbl);
}

// ── Zona de número + unidad ────────────────────────────────────────────────
static void redraw_number(int16_t cy, bool ok, float val, const char *unit) {
    s_gfx->fillRect(0, cy + LBL_H + LBL_OFF, LCD_WIDTH,
                    CHT_TOP - LBL_H - LBL_OFF, C_BG);

    if (ok) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", val);

        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        int16_t bx, by; uint16_t tw, th;
        s_gfx->getTextBounds(buf, 0, cy + NUM_DY, &bx, &by, &tw, &th);

        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        int16_t ux, uy; uint16_t utw, uth;
        s_gfx->getTextBounds(unit, 0, cy + UNT_DY, &ux, &uy, &utw, &uth);

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
        cprint_center("N/A", cy + NUM_DY, C_DIM);
        s_gfx->setFont(nullptr);
        s_gfx->setTextSize(1);
        cprint_center("sensor no conectado", cy + SUB_DY, C_DIVID);
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
        Serial.println("[DISP] GFX begin falló"); return false;
    }

    memset(s_h_temp, 0, sizeof(s_h_temp));
    memset(s_h_hum,  0, sizeof(s_h_hum));
    memset(s_h_pt,   0, sizeof(s_h_pt));
    s_ok = true;
    s_gfx->fillScreen(C_BG);
    Serial.println("[DISP] OK");
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  ESTÁTICO
// ═══════════════════════════════════════════════════════════════════════════
static void draw_static() {
    s_gfx->fillScreen(C_BG);
    s_gfx->drawFastHLine(0, C1_Y - 1,  LCD_WIDTH, C_DIVID);
    s_gfx->drawFastHLine(0, C2_Y - 1,  LCD_WIDTH, C_DIVID);
    s_gfx->drawFastHLine(0, FTR_Y - 1, LCD_WIDTH, C_DIVID);
    s_gfx->setFont(nullptr);
    s_static_done = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE — anti-flicker: redibuja solo lo que cambió
// ═══════════════════════════════════════════════════════════════════════════
void display_update(const DisplayData &d) {
    if (!s_ok) return;
    if (!s_static_done) draw_static();

    bool has_sht = d.sht35_ok && !isnan(d.temp_sht35) && !isnan(d.humidity);
    bool has_pt  = d.pt100_ok && !isnan(d.temp_pt100);

    // ── Alt 5s ─────────────────────────────────────────────────────────
    if (millis() - s_alt_t >= ALT_MS) {
        s_alt_hum = !s_alt_hum;
        s_alt_t   = millis();
    }

    // ── Historial — cada canal independiente ───────────────────────────
    s_h_temp[s_hi] = has_sht ? d.temp_sht35 : 0.0f;
    s_h_hum[s_hi]  = has_sht ? d.humidity   : 0.0f;
    s_h_pt[s_hi]   = has_pt  ? d.temp_pt100 : 0.0f;
    s_hi = (s_hi + 1) % HIST_N;
    if (s_hcnt < HIST_N) s_hcnt++;

    // ── Card 1: SHT35 ─────────────────────────────────────────────────
    float       c1_val  = has_sht ? (s_alt_hum ? d.humidity : d.temp_sht35) : 0;
    const char *c1_unit = has_sht ? (s_alt_hum ? " %" : " C") : "";
    const char *c1_lbl  = has_sht ? (s_alt_hum ? "S1  HUMEDAD" : "S1  TEMPERATURA") : "S1  SHT35";
    float      *c1_hist = s_alt_hum ? s_h_hum : s_h_temp;

    // ── Card 2: PT100 ─────────────────────────────────────────────────
    float       c2_val  = has_pt ? d.temp_pt100 : 0;
    const char *c2_unit = " C";
    const char *c2_lbl  = "S2  PT100";
    float      *c2_hist = s_h_pt;

    // ── HEADER (solo si cambió) ────────────────────────────────────────
    bool hdr_changed = (d.gsm_rssi       != s_hdr_gsm_prev)  ||
                       (d.gsm_connected  != s_hdr_gc_prev)   ||
                       (d.wifi_connected != s_hdr_wc_prev)   ||
                       (d.wifi_ap_mode   != s_hdr_ap_prev)   ||
                       (d.wifi_rssi      != s_hdr_wrssi_prev);

    if (hdr_changed) {
        s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_BG);

        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(12, HDR_Y + 25);
        s_gfx->print(DEVICE_ID);

        // GSM (derecha)
        draw_gsm_bars(LCD_WIDTH - 36, HDR_Y + 10, d.gsm_rssi, d.gsm_connected);

        // WiFi — arcos solo si conectado STA con rssi negativo válido
        // AP mode y sin conexión → todo dim (wbars = 0)
        uint8_t wbars = 0;
        if (d.wifi_connected && !d.wifi_ap_mode && d.wifi_rssi < 0) {
            if      (d.wifi_rssi >= -50) wbars = 3;
            else if (d.wifi_rssi >= -65) wbars = 2;
            else                         wbars = 1;
        }
        draw_wifi_icon(LCD_WIDTH - 36 - 30, HDR_Y + 11, wbars);

        s_hdr_gsm_prev   = d.gsm_rssi;
        s_hdr_gc_prev    = d.gsm_connected;
        s_hdr_wc_prev    = d.wifi_connected;
        s_hdr_ap_prev    = d.wifi_ap_mode;
        s_hdr_wrssi_prev = d.wifi_rssi;
    }

    // ── CARD 1 ────────────────────────────────────────────────────────
    {
        // Etiqueta: cambia si el label cambia (temp↔hum o sensor conecta/desconecta)
        if (strcmp(c1_lbl, s_c1_lbl_prev) != 0) {
            redraw_label(C1_Y, c1_lbl);
            strlcpy(s_c1_lbl_prev, c1_lbl, sizeof(s_c1_lbl_prev));
        }

        // Número: cambia si el valor formateado cambió O si el modo alt cambió
        char c1_str[16];
        if (has_sht) snprintf(c1_str, sizeof(c1_str), "%.1f%s", c1_val, c1_unit);
        else         strlcpy(c1_str, "N/A", sizeof(c1_str));

        if (strcmp(c1_str, s_c1_num_prev) != 0 || s_alt_hum != s_c1_alt_prev) {
            redraw_number(C1_Y, has_sht, c1_val, c1_unit);
            strlcpy(s_c1_num_prev, c1_str, sizeof(s_c1_num_prev));
            s_c1_alt_prev = s_alt_hum;
        }

        // Chart: siempre redibuja con el historial correcto (temp o hum)
        draw_sparkline(10, C1_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H,
                       c1_hist, s_hcnt, C_LINE1);
    }

    // ── CARD 2 ────────────────────────────────────────────────────────
    {
        if (strcmp(c2_lbl, s_c2_lbl_prev) != 0) {
            redraw_label(C2_Y, c2_lbl);
            strlcpy(s_c2_lbl_prev, c2_lbl, sizeof(s_c2_lbl_prev));
        }

        char c2_str[16];
        if (has_pt) snprintf(c2_str, sizeof(c2_str), "%.1f%s", c2_val, c2_unit);
        else        strlcpy(c2_str, "N/A", sizeof(c2_str));

        if (strcmp(c2_str, s_c2_num_prev) != 0) {
            redraw_number(C2_Y, has_pt, c2_val, c2_unit);
            strlcpy(s_c2_num_prev, c2_str, sizeof(s_c2_num_prev));
        }

        // Chart: solo tiene sentido si PT100 está conectado
        if (has_pt) {
            draw_sparkline(10, C2_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H,
                           c2_hist, s_hcnt, C_LINE2);
        } else {
            s_gfx->fillRect(10, C2_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H, C_BG);
        }
    }

    // ── FOOTER ────────────────────────────────────────────────────────
    if (d.battery_pct != s_bat_prev) {
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

    // Footer timestamp: fecha/hora real (NTP) o uptime si no sincronizado
    uint32_t cur_s = millis() / 1000;
    // Redibuja cada minuto si hay NTP, o cada segundo si solo hay uptime
    bool time_synced = (d.current_time > 1000000000UL);
    uint32_t ftr_tick = time_synced ? (cur_s / 60) : cur_s;
    if (ftr_tick != s_ftr_last_s) {
        s_gfx->fillRect(100, FTR_Y, LCD_WIDTH - 100, FTR_H, C_BG);
        char buf[24];
        if (time_synced) {
            // Formato: "26 may  3:27 AM"
            static const char *MES[] = {
                "ene","feb","mar","abr","may","jun",
                "jul","ago","sep","oct","nov","dic"
            };
            struct tm *ti = localtime(&d.current_time);
            int h = ti->tm_hour % 12;
            if (h == 0) h = 12;
            snprintf(buf, sizeof(buf), "%d %s  %d:%02d %s",
                     ti->tm_mday, MES[ti->tm_mon],
                     h, ti->tm_min,
                     ti->tm_hour < 12 ? "AM" : "PM");
        } else {
            // Uptime hasta que se sincronice
            snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                     (unsigned long)(cur_s / 3600),
                     (unsigned long)((cur_s % 3600) / 60),
                     (unsigned long)(cur_s % 60));
        }
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_UNIT);
        // Alinea a la derecha del footer
        int16_t bx, by; uint16_t tw, th;
        s_gfx->getTextBounds(buf, 0, FTR_Y + 27, &bx, &by, &tw, &th);
        s_gfx->setCursor(LCD_WIDTH - (int16_t)tw - 12, FTR_Y + 27);
        s_gfx->print(buf);
        s_ftr_last_s = ftr_tick;
    }

    s_gfx->setFont(nullptr);
}
