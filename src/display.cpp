// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//  Layout idéntico a imagen de referencia:
//    Header  (40px): nombre dispositivo + barras señal
//    Card 1 (198px): sensor primario (PT100 si hay, sino SHT35 temp)
//    Card 2 (198px): sensor secundario (SHT35 si Card1=PT100, sino hum)
//    Footer  (44px): batería + uptime/sync
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include "FreeSansBold24pt7b.h"
#include "FreeSans9pt7b.h"

// ═══════════════════════════════════════════════════════════════════════════
//  COLORS  (RGB565)
//  Referencia: fondo negro, números blancos, labels azul-gris, líneas azul claro
// ═══════════════════════════════════════════════════════════════════════════
#define C_BG       0x0000   // negro puro — fondo
#define C_HDR      0x0841   // azul muy oscuro — header / footer
#define C_DIV      0x18C3   // línea divisora
#define C_WHITE    0xFFFF   // números
#define C_LABEL    0x4B6D   // azul-gris — etiquetas T1 / T2
#define C_SUB      0x2965   // gris oscuro — texto secundario
#define C_DIM      0x39E7   // N/A, mensajes inactivos
#define C_BAR_ON   0xFFFF   // barra señal activa — blanca (como referencia)
#define C_BAR_OFF  0x18C3   // barra señal inactiva
#define C_GRID     0x0861   // líneas de guía del chart
#define C_LINE1    0x5FBF   // T1 sparkline — azul claro (como referencia)
#define C_LINE2    0x3FFF   // T2 sparkline — cian pálido
#define C_OK       0x07E0
#define C_WARN     0xFCC0
#define C_ERR      0xF800
#define C_WIFI_ON  0x3D9F   // WiFi bars — azul suave

// ═══════════════════════════════════════════════════════════════════════════
//  LAYOUT  (portrait 320 × 480)
// ═══════════════════════════════════════════════════════════════════════════
#define HDR_Y    0
#define HDR_H    40
#define C1_Y     41      // Card 1 top  (40 + 1px div)
#define C1_H     198
#define C2_Y     240     // Card 2 top  (41+198+1)
#define C2_H     198
#define FTR_Y    439     // Footer top  (240+198+1)
#define FTR_H    41

// Offsets dentro de cada card (relativos a card_y)
#define C_LBL_DY  22    // baseline etiqueta "T1 TEMPERATURA"
#define C_NUM_DY  80    // baseline número grande (FreeSansBold24pt textSize=1 ≈ 35px alto)
#define C_UNT_DY  62    // baseline unidad pequeña (°C / %)  — encima del número
#define C_SUB_DY  99    // baseline subtítulo (ej. humedad si hay)
#define C_CHT_DY  114   // chart top
#define C_CHT_H   81    // chart height  → bottom = 195 (dentro de 198)

// ═══════════════════════════════════════════════════════════════════════════
//  HISTORIAL DE SPARKLINES
// ═══════════════════════════════════════════════════════════════════════════
#define HIST_N 60

static float   s_h1[HIST_N];  // Card 1 history
static float   s_h2[HIST_N];  // Card 2 history
static uint8_t s_hi   = 0;
static uint8_t s_hcnt = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE
// ═══════════════════════════════════════════════════════════════════════════
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;
static bool             s_ok          = false;
static bool             s_static_done = false;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Centra texto entre x0 y x1, devuelve x donde terminó el texto
static int16_t cprint(const char *str, int16_t x0, int16_t x1,
                       int16_t y_baseline, uint16_t color) {
    int16_t bx, by; uint16_t tw, th;
    s_gfx->getTextBounds(str, 0, y_baseline, &bx, &by, &tw, &th);
    int16_t cx = x0 + ((int16_t)(x1 - x0) - (int16_t)tw) / 2;
    s_gfx->setTextColor(color);
    s_gfx->setCursor(cx, y_baseline);
    s_gfx->print(str);
    return cx + (int16_t)tw;
}

// Barras de señal — estilo referencia (blancas, 4 barras ascendentes)
static void draw_bars(int16_t x, int16_t y, int8_t rssi,
                       bool connected, uint16_t color_on) {
    const uint8_t h[4] = {5, 9, 14, 19};
    uint8_t filled = 0;
    if (connected) {
        if      (rssi >= -65) filled = 4;
        else if (rssi >= -75) filled = 3;
        else if (rssi >= -85) filled = 2;
        else                  filled = 1;
    }
    for (uint8_t i = 0; i < 4; i++) {
        uint16_t c = (i < filled) ? color_on : C_BAR_OFF;
        s_gfx->fillRect(x + i * 7, y + (19 - h[i]), 5, h[i], c);
    }
}

// Ícono de batería pequeño
static void draw_battery(int16_t x, int16_t y, uint8_t pct) {
    uint16_t c = (pct > 40) ? C_OK : (pct > 20) ? C_WARN : C_ERR;
    s_gfx->drawRect(x, y, 22, 11, C_DIV);
    s_gfx->fillRect(x + 22, y + 3, 3, 5, C_DIV);
    int16_t fw = (int16_t)(18 * pct / 100);
    if (fw > 0) s_gfx->fillRect(x + 2, y + 2, fw, 7, c);
}

// Dibuja un sparkline en el área indicada (fondo + grid + línea)
static void draw_sparkline(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                             float *data, uint8_t cnt, uint16_t lc) {
    s_gfx->fillRect(cx, cy, cw, ch, C_BG);

    // Grid (4 líneas horizontales)
    for (uint8_t i = 1; i <= 4; i++)
        s_gfx->drawFastHLine(cx, cy + ch * i / 5, cw, C_GRID);

    if (cnt < 2) return;

    // Auto-escala
    float vmin = data[0], vmax = data[0];
    for (uint8_t i = 1; i < cnt; i++) {
        if (data[i] < vmin) vmin = data[i];
        if (data[i] > vmax) vmax = data[i];
    }
    float range = vmax - vmin;
    if (range < 0.5f) { vmin -= 0.5f; vmax += 0.5f; range = 1.0f; }

    // Línea (2px de grosor para visibilidad)
    uint8_t pts = (cnt < (uint8_t)(cw / 2 + 1)) ? cnt : (uint8_t)(cw / 2);
    int16_t px0 = -1, py0 = -1;
    for (uint8_t i = 0; i < pts; i++) {
        uint8_t idx = (s_hi + HIST_N - pts + i) % HIST_N;
        float   n   = constrain((data[idx] - vmin) / range, 0.f, 1.f);
        int16_t px  = cx + (int16_t)((float)i / (float)(pts - 1) * (float)(cw - 1));
        int16_t py  = cy + ch - 2 - (int16_t)(n * (float)(ch - 4));
        if (px0 >= 0) {
            s_gfx->drawLine(px0, py0,     px, py,     lc);
            s_gfx->drawLine(px0, py0 + 1, px, py + 1, lc);
        }
        px0 = px; py0 = py;
    }

    // Valores mín / máx en esquinas (font default tamaño 1)
    char lb[10];
    s_gfx->setFont(nullptr);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_SUB);
    snprintf(lb, sizeof(lb), "%.1f", vmax);
    s_gfx->setCursor(cx + 4, cy + 2);
    s_gfx->print(lb);
    snprintf(lb, sizeof(lb), "%.1f", vmin);
    s_gfx->setCursor(cx + 4, cy + ch - 10);
    s_gfx->print(lb);
}

// Dibuja el contenido dinámico de una card (número + unidad + subtítulo + chart)
//   cy           : y superior de la card
//   has_val      : hay lectura válida
//   val          : valor principal
//   unit         : cadena de unidad (ej. " C" o "%")
//   has_sub      : tiene subtítulo (ej. humedad cuando sht35 es card 1)
//   sub_val / sub_unit: valor y unidad del subtítulo
//   hist / hcnt  : buffer histórico y cantidad de puntos
//   lc           : color de la línea del chart
static void draw_card_body(int16_t cy,
                            bool has_val, float val,
                            const char *unit,
                            bool has_sub, float sub_val, const char *sub_unit,
                            float *hist, uint8_t hcnt, uint16_t lc) {
    char buf[28];

    // Limpia zona número + subtítulo
    s_gfx->fillRect(0, cy + C_LBL_DY + 4, LCD_WIDTH,
                    C_CHT_DY - C_LBL_DY - 4, C_BG);

    if (has_val) {
        // ── Número grande centrado ─────────────────────
        s_gfx->setFont(&FreeSansBold24pt7b);
        s_gfx->setTextSize(1);

        snprintf(buf, sizeof(buf), "%.1f", val);

        // Reserva espacio para la unidad (aprox 22px en FreeSans9pt)
        int16_t unit_w = 24;
        int16_t bx, by; uint16_t tw, th;
        s_gfx->getTextBounds(buf, 0, cy + C_NUM_DY, &bx, &by, &tw, &th);

        // Centra número + unidad juntos
        int16_t total_w = (int16_t)tw + unit_w + 4;
        int16_t start_x = (LCD_WIDTH - total_w) / 2;

        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(start_x, cy + C_NUM_DY);
        s_gfx->print(buf);

        // Unidad en FreeSans9pt, pegada arriba a la derecha del número
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_LABEL);
        s_gfx->setCursor(start_x + (int16_t)tw + 4, cy + C_UNT_DY);
        s_gfx->print(unit);

        // ── Subtítulo (ej. humedad cuando card1=SHT35) ─
        if (has_sub) {
            snprintf(buf, sizeof(buf), "%.1f %s", sub_val, sub_unit);
            s_gfx->setFont(&FreeSans9pt7b);
            s_gfx->setTextSize(1);
            cprint(buf, 0, LCD_WIDTH, cy + C_SUB_DY, C_SUB);
        }
    } else {
        // N/A
        s_gfx->setFont(&FreeSansBold24pt7b);
        s_gfx->setTextSize(1);
        cprint("N/A", 0, LCD_WIDTH, cy + C_NUM_DY, C_DIM);

        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        cprint("sensor no conectado", 0, LCD_WIDTH, cy + C_SUB_DY, C_DIV);
    }

    // Chart
    draw_sparkline(10, cy + C_CHT_DY, LCD_WIDTH - 20, C_CHT_H,
                   hist, hcnt, lc);
}

// Dibuja etiqueta de card (se redibuja solo si cambia el estado de sensores)
static void draw_card_label(int16_t cy, const char *label) {
    s_gfx->fillRect(0, cy, LCD_WIDTH, C_LBL_DY + 4, C_BG);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LABEL);
    s_gfx->setCursor(12, cy + C_LBL_DY);
    s_gfx->print(label);
}

// ═══════════════════════════════════════════════════════════════════════════
//  INIT
// ═══════════════════════════════════════════════════════════════════════════
bool display_init() {
    Serial.println("[DISP] Init...");

    Wire1.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
    Wire1.setClock(400000);

    if (!s_tca.begin()) {
        Serial.printf("[DISP] ERROR: TCA9554 0x%02X no encontrado\n", TCA9554_ADDR);
        return false;
    }

    s_tca.pinMode1(TCA_LCD_RST_PIN, OUTPUT);
    s_tca.pinMode1(TCA_LCD_CS_PIN,  OUTPUT);
    s_tca.write1(TCA_LCD_CS_PIN,  HIGH);
    s_tca.write1(TCA_LCD_RST_PIN, LOW);
    delay(20);
    s_tca.write1(TCA_LCD_RST_PIN, HIGH);
    delay(150);
    s_tca.write1(TCA_LCD_CS_PIN, LOW);  // CS permanente bajo

    ledcSetup(LCD_BL_CHANNEL, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DUTY);

    s_bus = new Arduino_ESP32SPI(
        LCD_DC_PIN, GFX_NOT_DEFINED, LCD_SCLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN);
    s_gfx = new Arduino_ST7796(s_bus, GFX_NOT_DEFINED, 0 /*portrait*/, true /*IPS*/);

    if (!s_gfx->begin(20000000UL)) {
        Serial.println("[DISP] ERROR: GFX begin falló");
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
//  ESTÁTICO (solo una vez)
// ═══════════════════════════════════════════════════════════════════════════
static void draw_static() {
    s_gfx->fillScreen(C_BG);

    // Header y footer backgrounds
    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_HDR);
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_HDR);

    // Líneas divisoras (muy sutiles, como en la referencia)
    s_gfx->drawFastHLine(0, C1_Y - 1,  LCD_WIDTH, C_DIV);
    s_gfx->drawFastHLine(0, C2_Y - 1,  LCD_WIDTH, C_DIV);
    s_gfx->drawFastHLine(0, FTR_Y - 1, LCD_WIDTH, C_DIV);

    s_gfx->setFont(nullptr);
    s_static_done = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE
// ═══════════════════════════════════════════════════════════════════════════

// Track de estado anterior de sensores para redibujar labels solo cuando cambia
static int8_t s_last_cfg = -1;  // -1=never, 0=neither, 1=sht35only, 2=pt100only, 3=both

void display_update(const DisplayData &d) {
    if (!s_ok) return;
    if (!s_static_done) draw_static();

    // ── Determinar qué muestra cada card ──────────────────────────────
    bool has_pt100 = d.pt100_ok  && !isnan(d.temp_pt100);
    bool has_sht35 = d.sht35_ok  && !isnan(d.temp_sht35);
    bool has_hum   = d.sht35_ok  && !isnan(d.humidity);

    // Card 1: PT100 si hay, sino SHT35 temperatura
    // Card 2: SHT35 temperatura (si Card1=PT100) o SHT35 humedad (si Card1=SHT35)
    bool   c1_ok,  c2_ok;
    float  c1_val, c2_val;
    const char *c1_unit, *c2_unit;
    const char *c1_lbl,  *c2_lbl;
    bool   c1_sub_ok  = false;
    float  c1_sub_val = 0;
    const char *c1_sub_unit = "";

    if (has_pt100) {
        c1_ok  = true;  c1_val = d.temp_pt100; c1_unit = " C";  c1_lbl = "T1  PT100";
        c2_ok  = has_sht35; c2_val = has_sht35 ? d.temp_sht35 : 0;
        c2_unit = " C";     c2_lbl = has_sht35 ? "T2  REFERENCIA" : "T2  SHT35";
    } else {
        c1_ok  = has_sht35; c1_val = has_sht35 ? d.temp_sht35 : 0;
        c1_unit = " C";     c1_lbl = "T1  TEMPERATURA";
        c2_ok  = has_hum;   c2_val = has_hum   ? d.humidity    : 0;
        c2_unit = " %";     c2_lbl = "T2  HUMEDAD";
    }

    // ── Historial ─────────────────────────────────────────────────────
    s_h1[s_hi] = c1_ok ? c1_val : (s_hcnt > 0 ? s_h1[(s_hi + HIST_N - 1) % HIST_N] : 0.f);
    s_h2[s_hi] = c2_ok ? c2_val : (s_hcnt > 0 ? s_h2[(s_hi + HIST_N - 1) % HIST_N] : 0.f);
    s_hi = (s_hi + 1) % HIST_N;
    if (s_hcnt < HIST_N) s_hcnt++;

    // ── HEADER ────────────────────────────────────────────────────────
    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_HDR);

    // Nombre del dispositivo (izquierda)
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_WHITE);
    s_gfx->setCursor(12, HDR_Y + 25);
    s_gfx->print(DEVICE_ID);

    // Barras GSM (derecha) — blancas como en la referencia
    draw_bars(250, HDR_Y + 10, d.gsm_rssi, d.gsm_connected, C_BAR_ON);

    // Barras WiFi (a la izquierda de GSM) — azul si conectado, apagadas si AP
    if (d.wifi_ap_mode) {
        // "AP" en azul muy pequeño
        s_gfx->setFont(nullptr);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WIFI_ON);
        s_gfx->setCursor(224, HDR_Y + 18);
        s_gfx->print("AP");
    } else {
        draw_bars(220, HDR_Y + 10, d.wifi_rssi, d.wifi_connected, C_WIFI_ON);
    }

    // ── CARD LABELS (solo si cambia config de sensores) ──────────────
    int8_t cfg = (int8_t)((has_pt100 ? 2 : 0) | (has_sht35 ? 1 : 0));
    if (cfg != s_last_cfg) {
        draw_card_label(C1_Y, c1_lbl);
        draw_card_label(C2_Y, c2_lbl);
        s_last_cfg = cfg;
    }

    // ── CARD 1 BODY ───────────────────────────────────────────────────
    draw_card_body(C1_Y,
                   c1_ok, c1_val, c1_unit,
                   c1_sub_ok, c1_sub_val, c1_sub_unit,
                   s_h1, s_hcnt, C_LINE1);

    // ── CARD 2 BODY ───────────────────────────────────────────────────
    draw_card_body(C2_Y,
                   c2_ok, c2_val, c2_unit,
                   false, 0, "",
                   s_h2, s_hcnt, C_LINE2);

    // ── FOOTER ────────────────────────────────────────────────────────
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_HDR);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);

    // Batería izquierda
    draw_battery(10, FTR_Y + 15, d.battery_pct);
    char buf[32];
    snprintf(buf, sizeof(buf), " %d%%", d.battery_pct);
    s_gfx->setTextColor(C_LABEL);
    s_gfx->setCursor(36, FTR_Y + 26);
    s_gfx->print(buf);

    // Red activa (centro-izq)
    s_gfx->setCursor(90, FTR_Y + 26);
    if (d.wifi_connected && !d.wifi_ap_mode) {
        s_gfx->setTextColor(C_WIFI_ON);
        s_gfx->print("WiFi");
    } else if (d.gsm_connected) {
        s_gfx->setTextColor(C_OK);
        s_gfx->print("GSM");
    } else {
        s_gfx->setTextColor(C_ERR);
        s_gfx->print("--");
    }

    // Tiempo / sync (derecha)
    uint32_t up = millis() / 1000;
    if (d.last_post_ms > 0) {
        uint32_t ago = (millis() - d.last_post_ms) / 1000;
        if (ago < 60)
            snprintf(buf, sizeof(buf), "sync %lus",  (unsigned long)ago);
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
    s_gfx->setTextColor(C_SUB);
    s_gfx->setCursor(190, FTR_Y + 26);
    s_gfx->print(buf);

    s_gfx->setFont(nullptr);
}
