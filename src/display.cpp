// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//
//  Cards dinámicas:
//    Card 1 = SHT35 (S1): alterna temp 5s / hum 5s
//    Card 2 = PT100 (S2): temperatura fija
//    Si el sensor no está conectado → card en negro, sin nada
//
//  Header: nombre + ícono WiFi (arcos) + barras GSM (blancas)
//  Footer: batería + uptime HH:MM:SS (sin colores)
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include "FreeSans24pt7b.h"
#include "FreeSans9pt7b.h"

// ── Colores ────────────────────────────────────────────────────────────────
#define C_BG      0x0000   // negro puro
#define C_DIVID   0x18C3   // divisor
#define C_WHITE   0xFFFF   // números
#define C_LABEL   0x4A69   // etiquetas S1 / S2 (azul-gris oscuro)
#define C_UNIT    0x7BEF   // unidad (gris)
#define C_DIM     0x2965   // texto inactivo
#define C_GRID    0x0861   // guías chart
#define C_LINE1   0xCE59   // sparkline S1 — gris clarito (no brillante)
#define C_LINE2   0xB5B6   // sparkline S2 — gris ligeramente diferente
#define C_BARON   0xFFFF   // barras GSM activas (blanco)
#define C_BAROFF  0x1082   // barras inactivas
#define C_WIFION  0xFFFF   // WiFi activo (blanco como referencia)
#define C_WIFIOFF 0x1082   // WiFi arco inactivo

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
#define LBL_DY   16    // baseline etiqueta (pequeña)
#define NUM_DY   78    // baseline número grande
#define UNT_DY   57    // baseline unidad
#define SUB_DY   96    // baseline subtítulo N/A
#define CHT_TOP  108   // inicio chart
#define CHT_H    87    // alto chart

// ── Historial sparkline ────────────────────────────────────────────────────
#define HIST_N  60
static float   s_h1[HIST_N];
static float   s_h2[HIST_N];
static uint8_t s_hi   = 0;
static uint8_t s_hcnt = 0;

// ── Alt 5s SHT35 ──────────────────────────────────────────────────────────
#define ALT_MS  5000
static uint32_t s_alt_t   = 0;
static bool     s_alt_hum = false;

// ── Estado previo (anti-flicker) ──────────────────────────────────────────
static char    s_c1_lbl_prev[24] = "";
static char    s_c1_num_prev[16] = "";
static char    s_c2_lbl_prev[24] = "";
static char    s_c2_num_prev[16] = "";
static bool    s_c1_blank_prev   = true;
static bool    s_c2_blank_prev   = true;
static int8_t  s_hdr_gsm_prev    = 127;
static bool    s_hdr_gc_prev     = false;
static bool    s_hdr_wc_prev     = false;
static bool    s_hdr_ap_prev     = false;
static int8_t  s_hdr_wrssi_prev  = 127;
static uint8_t s_bat_prev        = 255;
static uint32_t s_ftr_last_s     = 0xFFFFFFFF;

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

// Ícono WiFi — 3 arcos concéntricos + punto (logo WiFi real)
//   x,y = esquina superior-izquierda del área del ícono (~24×17px)
//   bars = 0..3 arcos activos
static void draw_wifi_icon(int16_t x, int16_t y, uint8_t bars) {
    // Centro del "dot" (punto inferior)
    const int16_t cx = x + 11;
    const int16_t cy = y + 15;

    s_gfx->fillRect(x, y, 23, 18, C_BG);

    // 3 arcos desde más interior al más exterior
    const uint8_t radii[3] = {5, 9, 13};
    for (uint8_t i = 0; i < 3; i++) {
        uint16_t col = (i < bars) ? C_WIFION : C_WIFIOFF;
        float r = (float)radii[i];
        // Arco de 210° a 330° — fan apuntando hacia arriba (en coords pantalla)
        // En pantalla: 270° = arriba, cos/sin estándar
        for (int16_t a = 210; a <= 330; a += 3) {
            float ra = (float)a * PI / 180.0f;
            int16_t px = cx + (int16_t)(r * cosf(ra));
            int16_t py = cy + (int16_t)(r * sinf(ra));
            s_gfx->drawPixel(px, py, col);
            s_gfx->drawPixel(px, py - 1, col); // grosor 2px
        }
    }

    // Punto central
    uint16_t dc = (bars > 0) ? C_WIFION : C_WIFIOFF;
    s_gfx->fillCircle(cx, cy, 2, dc);
}

// Ícono batería (relleno blanco, sin colores)
static void draw_battery(int16_t x, int16_t y, uint8_t pct) {
    s_gfx->drawRect(x, y, 24, 12, C_UNIT);
    s_gfx->fillRect(x + 24, y + 3, 3, 6, C_UNIT);
    int16_t fw = (int16_t)(20 * pct / 100);
    if (fw > 0) s_gfx->fillRect(x + 2, y + 2, fw, 8, C_WHITE); // blanco puro
}

// Sparkline — con valores min/max en esquina derecha
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

    // Min/max en esquina derecha
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

// Dibuja solo la etiqueta (font pequeño)
static void redraw_label(int16_t cy, const char *lbl) {
    s_gfx->fillRect(0, cy, LCD_WIDTH, LBL_DY + 4, C_BG);
    s_gfx->setFont(nullptr);   // default 5×8 — más pequeño
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LABEL);
    s_gfx->setCursor(12, cy + LBL_DY);
    s_gfx->print(lbl);
}

// Dibuja solo la zona del número
static void redraw_number(int16_t cy, bool ok, float val, const char *unit) {
    s_gfx->fillRect(0, cy + LBL_DY + 4, LCD_WIDTH,
                    CHT_TOP - LBL_DY - 4, C_BG);

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
    }
    // Si !ok → la card estará en blanco (se maneja en display_update)
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

    memset(s_h1, 0, sizeof(s_h1));
    memset(s_h2, 0, sizeof(s_h2));
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
//  UPDATE — anti-flicker: solo redibuja lo que cambió
// ═══════════════════════════════════════════════════════════════════════════
void display_update(const DisplayData &d) {
    if (!s_ok) return;
    if (!s_static_done) draw_static();

    bool has_sht = d.sht35_ok && !isnan(d.temp_sht35) && !isnan(d.humidity);
    bool has_pt  = d.pt100_ok && !isnan(d.temp_pt100);

    // ── Alt 5s para SHT35 ──────────────────────────────────────────────
    if (millis() - s_alt_t >= ALT_MS) {
        s_alt_hum = !s_alt_hum;
        s_alt_t   = millis();
    }

    // ── Determinar contenido de cada card ─────────────────────────────
    // Card 1 = SHT35 (S1)
    bool        c1_blank = !has_sht;
    float       c1_val   = 0;
    const char *c1_unit  = "";
    const char *c1_lbl   = "";
    if (has_sht) {
        if (!s_alt_hum) { c1_val = d.temp_sht35; c1_unit = " C";  c1_lbl = "S1  TEMPERATURA"; }
        else             { c1_val = d.humidity;   c1_unit = " %";  c1_lbl = "S1  HUMEDAD";     }
    }

    // Card 2 = PT100 (S2)
    bool        c2_blank = !has_pt;
    float       c2_val   = 0;
    const char *c2_unit  = "";
    const char *c2_lbl   = "";
    if (has_pt) {
        c2_val  = d.temp_pt100;
        c2_unit = " C";
        c2_lbl  = "S2  PT100";
    }

    // ── Historial (acumula siempre) ────────────────────────────────────
    s_h1[s_hi] = has_sht ? d.temp_sht35 : 0;
    s_h2[s_hi] = has_pt  ? d.temp_pt100 : 0;
    s_hi = (s_hi + 1) % HIST_N;
    if (s_hcnt < HIST_N) s_hcnt++;

    // ── HEADER ────────────────────────────────────────────────────────
    bool hdr_changed = (d.gsm_rssi      != s_hdr_gsm_prev)  ||
                       (d.gsm_connected != s_hdr_gc_prev)   ||
                       (d.wifi_connected != s_hdr_wc_prev)  ||
                       (d.wifi_ap_mode   != s_hdr_ap_prev)  ||
                       (d.wifi_rssi      != s_hdr_wrssi_prev);

    if (hdr_changed) {
        s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_BG);

        // Nombre dispositivo
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(12, HDR_Y + 25);
        s_gfx->print(DEVICE_ID);

        // GSM bars (derecha)
        draw_gsm_bars(LCD_WIDTH - 36, HDR_Y + 10, d.gsm_rssi, d.gsm_connected);

        // WiFi ícono (a la izquierda de GSM bars)
        uint8_t wbars = 0;
        if (d.wifi_connected && !d.wifi_ap_mode) {
            if      (d.wifi_rssi >= -50) wbars = 3;
            else if (d.wifi_rssi >= -65) wbars = 2;
            else                         wbars = 1;
        } else if (d.wifi_ap_mode) {
            wbars = 2; // AP: 2 arcos para indicar "activo"
        }
        draw_wifi_icon(LCD_WIDTH - 36 - 30, HDR_Y + 11, wbars);

        s_hdr_gsm_prev   = d.gsm_rssi;
        s_hdr_gc_prev    = d.gsm_connected;
        s_hdr_wc_prev    = d.wifi_connected;
        s_hdr_ap_prev    = d.wifi_ap_mode;
        s_hdr_wrssi_prev = d.wifi_rssi;
    }

    // ── CARD 1 ────────────────────────────────────────────────────────
    if (c1_blank && !s_c1_blank_prev) {
        // Sensor desconectado → borrar card
        s_gfx->fillRect(0, C1_Y, LCD_WIDTH, C1_H, C_BG);
        s_c1_lbl_prev[0] = '\0';
        s_c1_num_prev[0] = '\0';
        s_c1_blank_prev  = true;
    } else if (!c1_blank) {
        s_c1_blank_prev = false;

        if (strcmp(c1_lbl, s_c1_lbl_prev) != 0) {
            redraw_label(C1_Y, c1_lbl);
            strlcpy(s_c1_lbl_prev, c1_lbl, sizeof(s_c1_lbl_prev));
        }

        char c1_str[16];
        snprintf(c1_str, sizeof(c1_str), "%.1f%s", c1_val, c1_unit);
        if (strcmp(c1_str, s_c1_num_prev) != 0) {
            redraw_number(C1_Y, true, c1_val, c1_unit);
            strlcpy(s_c1_num_prev, c1_str, sizeof(s_c1_num_prev));
        }

        draw_sparkline(10, C1_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H,
                       s_h1, s_hcnt, C_LINE1);
    }

    // ── CARD 2 ────────────────────────────────────────────────────────
    if (c2_blank && !s_c2_blank_prev) {
        s_gfx->fillRect(0, C2_Y, LCD_WIDTH, C2_H, C_BG);
        s_c2_lbl_prev[0] = '\0';
        s_c2_num_prev[0] = '\0';
        s_c2_blank_prev  = true;
    } else if (!c2_blank) {
        s_c2_blank_prev = false;

        if (strcmp(c2_lbl, s_c2_lbl_prev) != 0) {
            redraw_label(C2_Y, c2_lbl);
            strlcpy(s_c2_lbl_prev, c2_lbl, sizeof(s_c2_lbl_prev));
        }

        char c2_str[16];
        snprintf(c2_str, sizeof(c2_str), "%.1f%s", c2_val, c2_unit);
        if (strcmp(c2_str, s_c2_num_prev) != 0) {
            redraw_number(C2_Y, true, c2_val, c2_unit);
            strlcpy(s_c2_num_prev, c2_str, sizeof(s_c2_num_prev));
        }

        draw_sparkline(10, C2_Y + CHT_TOP, LCD_WIDTH - 20, CHT_H,
                       s_h2, s_hcnt, C_LINE2);
    }

    // ── FOOTER: batería (si cambió) + tiempo (cada segundo) ───────────
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

    uint32_t cur_s = millis() / 1000;
    if (cur_s != s_ftr_last_s) {
        s_gfx->fillRect(130, FTR_Y, LCD_WIDTH - 130, FTR_H, C_BG);
        // Uptime HH:MM:SS — sin "sync", sin colores
        char buf[12];
        snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                 (unsigned long)(cur_s / 3600),
                 (unsigned long)((cur_s % 3600) / 60),
                 (unsigned long)(cur_s % 60));
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_UNIT);
        s_gfx->setCursor(190, FTR_Y + 27);
        s_gfx->print(buf);
        s_ftr_last_s = cur_s;
    }

    s_gfx->setFont(nullptr);
}
