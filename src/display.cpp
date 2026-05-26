// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//
//  Layout (portrait 320×480):
//    Header  40px  : nombre + barras de señal
//    Card 1 198px  : SHT35 → alterna temp 5s / hum 5s
//                    PT100 → temperatura fija
//    Card 2 198px  : segundo sensor (o N/A)
//    Footer  44px  : batería + uptime
//
//  Font números : FreeSans24pt7b  (regular — más delgada, más elegante)
//  Font labels  : FreeSans9pt7b
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

// Los fonts van DESPUÉS de Arduino_GFX (necesitan GFXfont)
#include "FreeSans24pt7b.h"
#include "FreeSans9pt7b.h"

// ═══════════════════════════════════════════════════════════════════════════
//  COLORES  RGB565
//  Paleta inspirada en la imagen de referencia
// ═══════════════════════════════════════════════════════════════════════════
#define C_BG      0x0000   // negro puro — fondo principal
#define C_HDR     0x0841   // azul muy oscuro — header / footer
#define C_DIVID   0x18C3   // línea divisora sutil
#define C_WHITE   0xFFFF   // números (igual que referencia)
#define C_LABEL   0x5ACF   // azul-gris para etiquetas T1 / T2
#define C_UNIT    0x8C51   // unidad °C / % (gris medio)
#define C_DIM     0x4208   // N/A / texto inactivo
#define C_GRID    0x0861   // líneas de guía de chart (muy oscuro)
#define C_LINE1   0x4EDF   // sparkline card 1 — azul claro (referencia)
#define C_LINE2   0x27DF   // sparkline card 2 — cian suave
#define C_BARON   0xFFFF   // barra señal activa (blanca — referencia)
#define C_BAROFF  0x1082   // barra señal inactiva
#define C_OK      0x07E0   // verde
#define C_WARN    0xFCC0
#define C_ERR     0xF800
#define C_WIFION  0x3D9F   // acento WiFi (azul suave, diferente a GSM)

// ═══════════════════════════════════════════════════════════════════════════
//  LAYOUT  (320 × 480)
// ═══════════════════════════════════════════════════════════════════════════
//  Nota: el divisor entre secciones es 1px
#define HDR_Y    0
#define HDR_H    40
#define C1_Y     41      // 0+40+1
#define C1_H     198
#define C2_Y     240     // 41+198+1
#define C2_H     198
#define FTR_Y    439     // 240+198+1
#define FTR_H    41      // 439+41 = 480 ✓

// Posiciones internas de cada card (relativas al top de la card)
//  FreeSans24pt7b: ascender ≈ 33px, total altura glifo ≈ 38px
#define LBL_DY    20     // baseline etiqueta "T1  TEMPERATURA"
#define NUM_DY    80     // baseline número grande
#define UNT_DY    58     // baseline unidad (arriba-derecha del número)
#define SUB_DY    99     // baseline subtítulo / animación
#define CHT_TOP   112    // y inicio chart (relativo a card top)
#define CHT_H     83     // altura chart → bottom en 195 (dentro de 198)

// ═══════════════════════════════════════════════════════════════════════════
//  HISTORIAL SPARKLINES
// ═══════════════════════════════════════════════════════════════════════════
#define HIST_N  60

static float   s_h1[HIST_N];
static float   s_h2[HIST_N];
static uint8_t s_hi   = 0;
static uint8_t s_hcnt = 0;

// ═══════════════════════════════════════════════════════════════════════════
//  ANIMACIÓN 5s / 5s para SHT35
// ═══════════════════════════════════════════════════════════════════════════
#define ALT_PERIOD_MS 5000

static uint32_t s_alt1_ms  = 0;   // última vez que alternó card 1
static bool     s_alt1_hum = false; // false=temp, true=hum

static uint32_t s_alt2_ms  = 0;
static bool     s_alt2_hum = false;

// ═══════════════════════════════════════════════════════════════════════════
//  HARDWARE
// ═══════════════════════════════════════════════════════════════════════════
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus = nullptr;
static Arduino_GFX     *s_gfx = nullptr;
static bool             s_ok          = false;
static bool             s_static_done = false;
static int8_t           s_last_cfg    = -1;

// ═══════════════════════════════════════════════════════════════════════════
//  HELPERS
// ═══════════════════════════════════════════════════════════════════════════

// Imprime texto centrado en [x0, x1]; retorna x de final del texto
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

// Barras de señal — 4 barras blancas ascendentes (estilo referencia)
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

// Ícono batería compacto + porcentaje
static void draw_battery_row(int16_t x, int16_t y, uint8_t pct) {
    uint16_t fc = (pct > 40) ? C_OK : (pct > 20) ? C_WARN : C_ERR;
    // Outline
    s_gfx->drawRect(x, y, 24, 12, C_UNIT);
    s_gfx->fillRect(x + 24, y + 3, 3, 6, C_UNIT);  // polo +
    // Relleno
    int16_t fw = (int16_t)(20 * pct / 100);
    if (fw > 0) s_gfx->fillRect(x + 2, y + 2, fw, 8, fc);
}

// Sparkline — sin etiquetas de ejes para un look más limpio
static void draw_sparkline(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                             float *data, uint8_t cnt, uint16_t lc) {
    s_gfx->fillRect(cx, cy, cw, ch, C_BG);

    // Líneas de guía horizontales (3 líneas, muy sutiles)
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

    uint8_t pts = (cnt < (uint8_t)((cw / 2) + 1)) ? cnt : (uint8_t)(cw / 2);
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
}

// Dibuja una card completa (etiqueta + número + unidad + chart)
//   cy         : y top de la card
//   label      : "T1  TEMPERATURA" etc.
//   has_val    : hay lectura válida
//   val        : valor a mostrar
//   unit       : " C" o " %" (espacio intencional)
//   hist/hcnt  : historial para sparkline
//   lc         : color de la línea de chart
static void draw_card(int16_t cy,
                       const char *label,
                       bool has_val, float val, const char *unit,
                       float *hist, uint8_t hcnt, uint16_t lc) {
    // Limpia toda la card
    s_gfx->fillRect(0, cy, LCD_WIDTH, C1_H, C_BG);

    // ── Etiqueta ───────────────────────────────────────────────────
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LABEL);
    s_gfx->setCursor(12, cy + LBL_DY);
    s_gfx->print(label);

    // ── Número + unidad ────────────────────────────────────────────
    if (has_val) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f", val);

        // Mide el número para centrar el bloque número+unidad juntos
        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        int16_t bx, by; uint16_t tw, th;
        s_gfx->getTextBounds(buf, 0, cy + NUM_DY, &bx, &by, &tw, &th);

        // Mide la unidad en FreeSans9pt
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        int16_t ux, uy; uint16_t utw, uth;
        s_gfx->getTextBounds(unit, 0, cy + UNT_DY, &ux, &uy, &utw, &uth);

        // Gap de 4px entre número y unidad
        int16_t block_w = (int16_t)tw + 4 + (int16_t)utw;
        int16_t start_x = (LCD_WIDTH - block_w) / 2;

        // Dibuja número (FreeSans24pt — regular, delgada)
        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WHITE);
        s_gfx->setCursor(start_x, cy + NUM_DY);
        s_gfx->print(buf);

        // Dibuja unidad en FreeSans9pt, alineada arriba
        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_UNIT);
        s_gfx->setCursor(start_x + (int16_t)tw + 4, cy + UNT_DY);
        s_gfx->print(unit);

    } else {
        // Sin valor — N/A centrado
        s_gfx->setFont(&FreeSans24pt7b);
        s_gfx->setTextSize(1);
        cprint("N/A", 0, LCD_WIDTH, cy + NUM_DY, C_DIM);

        s_gfx->setFont(&FreeSans9pt7b);
        s_gfx->setTextSize(1);
        cprint("sensor no conectado", 0, LCD_WIDTH, cy + SUB_DY, C_DIVID);
    }

    // ── Sparkline ──────────────────────────────────────────────────
    draw_sparkline(10, cy + CHT_TOP, LCD_WIDTH - 20, CHT_H, hist, hcnt, lc);
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
    s_tca.write1(TCA_LCD_CS_PIN,  LOW);  // CS activo permanente

    ledcSetup(LCD_BL_CHANNEL, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DUTY);

    s_bus = new Arduino_ESP32SPI(
        LCD_DC_PIN, GFX_NOT_DEFINED, LCD_SCLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN);
    s_gfx = new Arduino_ST7796(s_bus, GFX_NOT_DEFINED, 0 /*portrait*/, true /*IPS*/);

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
//  ESTÁTICO (una sola vez)
// ═══════════════════════════════════════════════════════════════════════════
static void draw_static() {
    s_gfx->fillScreen(C_BG);

    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_HDR);
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_HDR);

    // Líneas divisoras (1px)
    s_gfx->drawFastHLine(0, C1_Y - 1,  LCD_WIDTH, C_DIVID);
    s_gfx->drawFastHLine(0, C2_Y - 1,  LCD_WIDTH, C_DIVID);
    s_gfx->drawFastHLine(0, FTR_Y - 1, LCD_WIDTH, C_DIVID);

    s_gfx->setFont(nullptr);
    s_static_done = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE  (llamado periódicamente desde main)
// ═══════════════════════════════════════════════════════════════════════════
void display_update(const DisplayData &d) {
    if (!s_ok) return;
    if (!s_static_done) draw_static();

    bool has_sht = d.sht35_ok && !isnan(d.temp_sht35) && !isnan(d.humidity);
    bool has_pt  = d.pt100_ok && !isnan(d.temp_pt100);

    // ── Determinar qué muestra cada card ─────────────────────────────────
    //   Card 1: SHT35 si conectado (alterna temp/hum), PT100 si no hay SHT35
    //   Card 2: PT100 si conectado, sino N/A
    //   (si ambos: Card1=SHT35 alternando, Card2=PT100 temperatura)

    // Card 1 — SHT35 si hay, sino PT100
    float  c1_val;
    const char *c1_label, *c1_unit;
    bool   c1_ok;

    if (has_sht) {
        // Alterna cada 5 segundos
        if (millis() - s_alt1_ms >= ALT_PERIOD_MS) {
            s_alt1_hum = !s_alt1_hum;
            s_alt1_ms  = millis();
        }
        if (!s_alt1_hum) {
            c1_val   = d.temp_sht35;
            c1_unit  = " C";
            c1_label = "T1  TEMPERATURA";
        } else {
            c1_val   = d.humidity;
            c1_unit  = " %";
            c1_label = "T1  HUMEDAD";
        }
        c1_ok = true;
    } else if (has_pt) {
        c1_val   = d.temp_pt100;
        c1_unit  = " C";
        c1_label = "T1  PT100";
        c1_ok    = true;
    } else {
        c1_val   = 0;
        c1_unit  = "";
        c1_label = "T1";
        c1_ok    = false;
    }

    // Card 2 — SOLO PT100 (módulo físico independiente)
    //   Si PT100 no está conectado → N/A, punto.
    float  c2_val;
    const char *c2_label, *c2_unit;
    bool   c2_ok;

    if (has_pt) {
        c2_val   = d.temp_pt100;
        c2_unit  = " C";
        c2_label = "T2  PT100";
        c2_ok    = true;
    } else {
        c2_val   = 0;
        c2_unit  = "";
        c2_label = "T2  PT100";
        c2_ok    = false;
    }

    // ── Historial ─────────────────────────────────────────────────────────
    //   Guarda el valor REAL del sensor (no el alternado)
    float raw1 = has_sht  ? d.temp_sht35 : (has_pt ? d.temp_pt100 : 0);
    float raw2 = has_pt   ? d.temp_pt100 : (has_sht ? d.humidity   : 0);
    s_h1[s_hi] = raw1;
    s_h2[s_hi] = raw2;
    s_hi = (s_hi + 1) % HIST_N;
    if (s_hcnt < HIST_N) s_hcnt++;

    // ── HEADER ────────────────────────────────────────────────────────────
    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_HDR);

    // Nombre del dispositivo (izquierda)
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_WHITE);
    s_gfx->setCursor(12, HDR_Y + 25);
    s_gfx->print(DEVICE_ID);

    // Barras de señal (derecha)
    // Muestra GSM en blanco Y WiFi en azul (como el header de referencia con una sola señal)
    // Si hay WiFi: una sola barra WiFi más a la derecha
    // Si no hay WiFi: solo GSM
    int16_t bars_x = LCD_WIDTH - 36;  // GSM bars

    if (d.wifi_connected && !d.wifi_ap_mode) {
        // WiFi + GSM: dos grupos de barras
        draw_bars(bars_x - 35, HDR_Y + 11, d.wifi_rssi, true, C_WIFION);
        draw_bars(bars_x,      HDR_Y + 11, d.gsm_rssi,  d.gsm_connected, C_BARON);
    } else if (d.wifi_ap_mode) {
        // AP mode: barras GSM + "AP" en azul
        draw_bars(bars_x, HDR_Y + 11, d.gsm_rssi, d.gsm_connected, C_BARON);
        s_gfx->setFont(nullptr);
        s_gfx->setTextSize(1);
        s_gfx->setTextColor(C_WIFION);
        s_gfx->setCursor(bars_x - 25, HDR_Y + 22);
        s_gfx->print("AP");
    } else {
        // Solo GSM
        draw_bars(bars_x, HDR_Y + 11, d.gsm_rssi, d.gsm_connected, C_BARON);
    }

    // ── CARDS ─────────────────────────────────────────────────────────────
    draw_card(C1_Y, c1_label, c1_ok, c1_val, c1_unit, s_h1, s_hcnt, C_LINE1);
    draw_card(C2_Y, c2_label, c2_ok, c2_val, c2_unit, s_h2, s_hcnt, C_LINE2);

    // ── FOOTER ────────────────────────────────────────────────────────────
    // Igual que la referencia: solo batería + porcentaje (izq) y tiempo (der)
    // Sin colores de red, sin indicadores GSM/WiFi
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_HDR);

    // Batería izquierda
    draw_battery_row(10, FTR_Y + 15, d.battery_pct);
    char buf[32];
    snprintf(buf, sizeof(buf), " %d%%", d.battery_pct);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_UNIT);
    s_gfx->setCursor(37, FTR_Y + 27);
    s_gfx->print(buf);

    // Uptime / sync derecha — en blanco/gris, sin colores
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
    s_gfx->setTextColor(C_UNIT);
    s_gfx->setCursor(190, FTR_Y + 27);
    s_gfx->print(buf);

    s_gfx->setFont(nullptr);
}
