// ─────────────────────────────────────────────
//  NEOLINK V1 — display.cpp
//  Layout: 2 sensor panels con sparkline charts
//  Anti-flicker: estático dibujado 1 sola vez
// ─────────────────────────────────────────────

#include "display.h"
#include "config.h"

#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>

// Incluir fonts DESPUÉS de Arduino_GFX (que define GFXglyph/GFXfont)
#include "FreeSansBold24pt7b.h"
#include "FreeSans9pt7b.h"

// ── Colores RGB565 ────────────────────────────
#define C_BG        0x0000   // negro puro
#define C_PANEL     0x0821   // #08102A  azul muy oscuro
#define C_DIVIDER   0x2945   // #294060  azul gris
#define C_WHITE     0xFFFF
#define C_GRAY      0x7BEF   // #787878  gris medio
#define C_LGRAY     0x9CF3   // #989898  gris claro (labels)
#define C_CYAN      0x07FF   // #00FFFF  sparkline T1
#define C_TEAL      0x07EF   // #00FFE8  sparkline T2
#define C_OK        0x07E0
#define C_WARN      0xFCC0
#define C_ERR       0xF800
#define C_ACCENT    0x435F   // #408BFF  azul acento

// ── Layout (portrait 320×480) ─────────────────
#define HDR_Y       0
#define HDR_H       36
#define DIV1_Y      36
#define T1_Y        38
#define T1_H        200      // y=38..237
#define DIV2_Y      238
#define T2_Y        240
#define T2_H        200      // y=240..439
#define DIV3_Y      440
#define FTR_Y       441
#define FTR_H       39       // y=441..479

// Dentro de cada sección (offset relativo al inicio de sección)
#define SEC_LABEL_DY    6    // "T1 INTERNA" label
#define SEC_NUM_DY      32   // número grande
#define SEC_CHART_DY    105  // sparkline chart
#define SEC_CHART_H     80

// Área precisa del número grande (para limpiar antes de redibujar)
#define NUM_CLEAR_X     0
#define NUM_CLEAR_W     320
#define NUM_CLEAR_H     68   // aprox altura número con font 24pt×2

// ── Histórico para sparklines ─────────────────
#define CHART_POINTS    60

static float   s_t1_hist[CHART_POINTS];   // temperatura
static float   s_t2_hist[CHART_POINTS];   // humedad
static uint8_t s_hist_idx   = 0;
static uint8_t s_hist_count = 0;

// ── Estado del módulo ─────────────────────────
static TCA9554          s_tca(TCA9554_ADDR, &Wire1);
static Arduino_DataBus *s_bus  = nullptr;
static Arduino_GFX     *s_gfx  = nullptr;
static bool             s_ok          = false;
static bool             s_static_done = false;

// ═════════════════════════════════════════════
//  HELPERS
// ═════════════════════════════════════════════

// Centra texto horizontalmente en la pantalla
static void print_hcenter(const char *str, int16_t y, uint16_t color) {
    int16_t x1, y1;
    uint16_t tw, th;
    s_gfx->getTextBounds(str, 0, y, &x1, &y1, &tw, &th);
    s_gfx->setTextColor(color);
    s_gfx->setCursor((int16_t)(LCD_WIDTH - tw) / 2, y);
    s_gfx->print(str);
}

// Dibuja sparkline chart en la región dada
static void draw_chart(int16_t cx, int16_t cy, int16_t cw, int16_t ch,
                       float *data, uint8_t count,
                       uint16_t line_color) {
    // Fondo
    s_gfx->fillRect(cx, cy, cw, ch, C_BG);
    // Líneas guía horizontales (3)
    uint16_t gc = 0x1082;   // gris muy oscuro
    for (uint8_t i = 1; i <= 3; i++)
        s_gfx->drawFastHLine(cx, cy + ch * i / 4, cw, gc);

    if (count < 2) return;

    // Calcula rango de datos (auto-scale)
    float vmin = data[0], vmax = data[0];
    for (uint8_t i = 1; i < count; i++) {
        if (data[i] < vmin) vmin = data[i];
        if (data[i] > vmax) vmax = data[i];
    }
    float range = vmax - vmin;
    if (range < 0.5f) {
        vmin -= 0.5f;
        vmax += 0.5f;
        range = 1.0f;
    }

    // Dibuja polilínea (un punto por pixel si hay suficientes datos)
    uint8_t pts = min((uint8_t)cw, count);
    float step = (float)cw / pts;

    int16_t prev_px = -1, prev_py = -1;
    for (uint8_t i = 0; i < pts; i++) {
        // Lee en orden cronológico desde el más antiguo
        uint8_t idx = (s_hist_idx + CHART_POINTS - pts + i) % CHART_POINTS;
        float val = data[idx];

        float norm = (val - vmin) / range;
        norm = constrain(norm, 0.0f, 1.0f);

        int16_t px = cx + (int16_t)(i * step);
        int16_t py = cy + ch - 1 - (int16_t)(norm * (ch - 2));

        if (prev_px >= 0) {
            s_gfx->drawLine(prev_px, prev_py, px, py, line_color);
            // Engorda la línea 1px abajo para mejor visibilidad
            s_gfx->drawLine(prev_px, prev_py + 1, px, py + 1, line_color);
        }
        prev_px = px;
        prev_py = py;
    }
}

// Icono de batería
static void draw_battery_icon(int16_t x, int16_t y, uint8_t pct) {
    uint16_t c = (pct > 40) ? C_OK : (pct > 20) ? C_WARN : C_ERR;
    s_gfx->fillRect(x, y, 25, 10, C_BG);
    s_gfx->drawRect(x, y, 22, 10, C_GRAY);
    s_gfx->fillRect(x + 22, y + 2, 2, 6, C_GRAY);
    int16_t fw = (int16_t)(18 * pct / 100);
    if (fw > 0) s_gfx->fillRect(x + 2, y + 2, fw, 6, c);
}

// ═════════════════════════════════════════════
//  INIT
// ═════════════════════════════════════════════

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
    s_tca.write1(TCA_LCD_CS_PIN,  HIGH);  // deassert CS

    // Reset hardware
    s_tca.write1(TCA_LCD_RST_PIN, LOW);
    delay(20);
    s_tca.write1(TCA_LCD_RST_PIN, HIGH);
    delay(150);

    // CS permanentemente activo (único dispositivo en este bus SPI)
    s_tca.write1(TCA_LCD_CS_PIN, LOW);

    // Backlight PWM
    ledcSetup(LCD_BL_CHANNEL, 5000, 8);
    ledcAttachPin(LCD_BL_PIN, LCD_BL_CHANNEL);
    ledcWrite(LCD_BL_CHANNEL, LCD_BL_DUTY);

    // GFX: hardware SPI, CS manejado externamente
    s_bus = new Arduino_ESP32SPI(
        LCD_DC_PIN, GFX_NOT_DEFINED, LCD_SCLK_PIN, LCD_MOSI_PIN, LCD_MISO_PIN);
    s_gfx = new Arduino_ST7796(s_bus, GFX_NOT_DEFINED, 0 /*portrait*/, true /*IPS*/);

    if (!s_gfx->begin(20000000UL)) {
        Serial.println("[DISP] ERROR: GFX begin failed");
        return false;
    }

    // Historial vacío
    memset(s_t1_hist, 0, sizeof(s_t1_hist));
    memset(s_t2_hist, 0, sizeof(s_t2_hist));

    s_ok = true;
    s_gfx->fillScreen(C_BG);
    Serial.println("[DISP] OK");
    return true;
}

// ═════════════════════════════════════════════
//  ESTÁTICO — dibujado 1 sola vez
// ═════════════════════════════════════════════
static void draw_static() {
    s_gfx->fillScreen(C_BG);

    // ── Header ──────────────────────────────────
    s_gfx->fillRect(0, HDR_Y, LCD_WIDTH, HDR_H, C_PANEL);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_WHITE);
    s_gfx->setCursor(10, HDR_Y + 22);
    s_gfx->print(DEVICE_ID);

    // ── Líneas divisorias ────────────────────────
    s_gfx->drawFastHLine(0, DIV1_Y, LCD_WIDTH, C_DIVIDER);
    s_gfx->drawFastHLine(0, DIV2_Y, LCD_WIDTH, C_DIVIDER);
    s_gfx->drawFastHLine(0, DIV3_Y, LCD_WIDTH, C_DIVIDER);

    // ── Labels de secciones ──────────────────────
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);

    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(12, T1_Y + SEC_LABEL_DY + 14);
    s_gfx->print("T1  TEMPERATURA");

    s_gfx->setCursor(12, T2_Y + SEC_LABEL_DY + 14);
    s_gfx->print("T2  HUMEDAD RELATIVA");

    s_gfx->setFont(nullptr);  // vuelve al font por defecto

    s_static_done = true;
}

// ═════════════════════════════════════════════
//  UPDATE — solo partes dinámicas
// ═════════════════════════════════════════════

void display_update(const DisplayData &d) {
    if (!s_ok) return;

    // Dibuja estático solo la primera vez
    if (!s_static_done) draw_static();

    // Acumula historial
    s_t1_hist[s_hist_idx] = d.sht35_ok ? d.temp_sht35 : (s_hist_count > 0 ? s_t1_hist[(s_hist_idx + CHART_POINTS - 1) % CHART_POINTS] : 0.0f);
    s_t2_hist[s_hist_idx] = d.sht35_ok ? d.humidity   : (s_hist_count > 0 ? s_t2_hist[(s_hist_idx + CHART_POINTS - 1) % CHART_POINTS] : 0.0f);
    s_hist_idx = (s_hist_idx + 1) % CHART_POINTS;
    if (s_hist_count < CHART_POINTS) s_hist_count++;

    char buf[32];

    // ── HEADER: señal GSM + batería ─────────────
    // Limpiar zona dinámica del header (derecha)
    s_gfx->fillRect(150, HDR_Y, LCD_WIDTH - 150, HDR_H, C_PANEL);

    // Barras de señal (4 barras verticales)
    {
        uint8_t bars = 0;
        if      (d.rssi >= -65) bars = 4;
        else if (d.rssi >= -75) bars = 3;
        else if (d.rssi >= -85) bars = 2;
        else if (d.rssi >  -99) bars = 1;

        uint16_t sc = (bars >= 3) ? C_OK : (bars >= 2) ? C_WARN : C_ERR;
        for (uint8_t i = 0; i < 4; i++) {
            int16_t bh = 5 + i * 5;
            int16_t bx = 200 + i * 8;
            int16_t by = HDR_Y + HDR_H - bh - 4;
            s_gfx->fillRect(bx, by, 5, bh, (i < bars) ? sc : C_DIVIDER);
        }
    }

    // Batería
    draw_battery_icon(240, HDR_Y + 13, d.battery_pct);
    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);
    s_gfx->setTextColor(C_LGRAY);
    s_gfx->setCursor(268, HDR_Y + 23);
    snprintf(buf, sizeof(buf), "%d%%", d.battery_pct);
    s_gfx->print(buf);

    // ── T1: TEMPERATURA ─────────────────────────
    // Limpiar zona del número
    s_gfx->fillRect(NUM_CLEAR_X, T1_Y + SEC_NUM_DY, NUM_CLEAR_W, NUM_CLEAR_H, C_BG);

    s_gfx->setFont(&FreeSansBold24pt7b);
    s_gfx->setTextSize(1);
    if (d.sht35_ok && !isnan(d.temp_sht35)) {
        snprintf(buf, sizeof(buf), "%.1f C", d.temp_sht35);
        print_hcenter(buf, T1_Y + SEC_NUM_DY + 46, C_WHITE);
    } else {
        print_hcenter("-- C", T1_Y + SEC_NUM_DY + 46, C_GRAY);
    }

    // Sparkline T1
    draw_chart(
        10, T1_Y + SEC_CHART_DY,
        LCD_WIDTH - 20, SEC_CHART_H,
        s_t1_hist, s_hist_count, C_CYAN);

    // ── T2: HUMEDAD ──────────────────────────────
    s_gfx->fillRect(NUM_CLEAR_X, T2_Y + SEC_NUM_DY, NUM_CLEAR_W, NUM_CLEAR_H, C_BG);

    s_gfx->setFont(&FreeSansBold24pt7b);
    s_gfx->setTextSize(1);
    if (d.sht35_ok && !isnan(d.humidity)) {
        snprintf(buf, sizeof(buf), "%.1f%%", d.humidity);
        print_hcenter(buf, T2_Y + SEC_NUM_DY + 46, C_WHITE);
    } else {
        print_hcenter("--.-%% ", T2_Y + SEC_NUM_DY + 46, C_GRAY);
    }

    // Sparkline T2
    draw_chart(
        10, T2_Y + SEC_CHART_DY,
        LCD_WIDTH - 20, SEC_CHART_H,
        s_t2_hist, s_hist_count, C_TEAL);

    // ── FOOTER ───────────────────────────────────
    s_gfx->fillRect(0, FTR_Y, LCD_WIDTH, FTR_H, C_PANEL);

    s_gfx->setFont(&FreeSans9pt7b);
    s_gfx->setTextSize(1);

    // Estado conexión
    s_gfx->setCursor(10, FTR_Y + 22);
    if (d.modem_connected) {
        s_gfx->setTextColor(C_OK);
        s_gfx->print("GPRS");
        if (d.rssi != 0) {
            s_gfx->setTextColor(C_LGRAY);
            snprintf(buf, sizeof(buf), "  %ddBm", d.rssi);
            s_gfx->print(buf);
        }
    } else {
        s_gfx->setTextColor(C_ERR);
        s_gfx->print("SIN RED");
    }

    // Uptime / sync
    {
        uint32_t up = millis() / 1000;
        if (d.last_post_ms > 0) {
            uint32_t ago = (millis() - d.last_post_ms) / 1000;
            if (ago < 60)
                snprintf(buf, sizeof(buf), "sync %lus", (unsigned long)ago);
            else
                snprintf(buf, sizeof(buf), "sync %lum", (unsigned long)(ago / 60));
        } else {
            snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu",
                     (unsigned long)(up / 3600),
                     (unsigned long)((up % 3600) / 60),
                     (unsigned long)(up % 60));
        }
        s_gfx->setTextColor(C_LGRAY);
        s_gfx->setCursor(170, FTR_Y + 22);
        s_gfx->print(buf);
    }

    // Restaura font por defecto al terminar
    s_gfx->setFont(nullptr);
}
