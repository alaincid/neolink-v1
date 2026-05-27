#!/usr/bin/env python3
"""
Neolink V1 — KiCad 7 project file generator.
Generates:
  neolink-v1.kicad_pro  — project metadata
  neolink-v1.kicad_sch  — schematic (all connectors + global labels)
  neolink-v1.kicad_pcb  — PCB layout (footprints placed, no routes)

Run from pcb/ directory:  python3 gen.py
"""

import uuid, json, math

# ─── helpers ──────────────────────────────────────────────────────────────────

def u(): return str(uuid.uuid4())

def fv(v):
    """Format float cleanly (strip trailing zeros)."""
    s = f"{float(v):.4f}"
    p = s.split('.')
    dec = p[1].rstrip('0')
    return p[0] + ('.' + dec if dec else '')

EFF   = '(effects (font (size 1.27 1.27)))'
EFF_H = '(effects (font (size 1.27 1.27)) hide)'

# ─────────────────────────────────────────────────────────────────────────────
# 1. PROJECT FILE
# ─────────────────────────────────────────────────────────────────────────────

PRO = {
    "board": {
        "design_settings": {
            "defaults": {
                "board_outline_line_width": 0.05,
                "copper_line_width": 0.25,
                "copper_text_size_h": 1.5,
                "copper_text_size_v": 1.5,
                "copper_text_thickness": 0.3,
                "other_line_width": 0.1,
                "silk_line_width": 0.1,
                "pads": {"drill": 1.0, "height": 1.8, "width": 1.8}
            },
            "rules": {
                "min_clearance": 0.2,
                "min_track_width": 0.25,
                "min_via_diameter": 0.8,
                "min_through_hole_diameter": 0.8
            },
            "track_widths": [0.25, 0.5, 1.0],
            "via_dimensions": [{"diameter": 1.6, "drill": 0.8}]
        }
    },
    "meta": {"filename": "neolink-v1.kicad_pro", "version": 1},
    "schematic": {
        "drawing": {
            "default_wire_thickness": 6,
            "default_pin_length": 100,
            "default_text_size": 50,
            "default_junction_size": 40,
            "default_line_thickness": 6,
            "default_bus_thickness": 12
        }
    },
    "sheets": [],
    "text_variables": {}
}

with open("neolink-v1.kicad_pro", "w") as fh:
    json.dump(PRO, fh, indent=2)
print("✓ neolink-v1.kicad_pro")

# ─────────────────────────────────────────────────────────────────────────────
# 2. SCHEMATIC
# ─────────────────────────────────────────────────────────────────────────────

# ── symbol definitions (lib_symbols) ─────────────────────────────────────────

def sym_1xN(name, n, pin_names):
    """Single-row N-pin connector symbol, pins on left side."""
    bot = -(n * 2.54) + 1.27
    top = 1.27
    out = [f'  (symbol "{name}"']
    out += ['    (pin_names (offset 1.016) hide)', '    (pin_numbers hide)']
    out += [f'    (property "Reference" "J" (at 1.905 {fv(top+0.5)} 0) {EFF})']
    out += [f'    (property "Value" "{name}" (at 1.905 {fv(bot-0.5)} 0) {EFF})']
    out += [f'    (property "Footprint" "" (at 0 0 0) {EFF_H})']
    out += [f'    (symbol "{name}_0_1"']
    out += [f'      (rectangle (start -2.54 {fv(bot)}) (end 0 {fv(top)})']
    out += ['        (stroke (width 0.1524) (type default)) (fill (type background)))']
    out += ['    )']
    out += [f'    (symbol "{name}_1_1"']
    for i in range(n):
        y = fv(-i * 2.54)
        pn = pin_names[i]
        out += [f'      (pin passive line (at -5.08 {y} 0) (length 2.54)']
        out += [f'        (name "{pn}" {EFF}) (number "{i+1}" {EFF}))']
    out += ['    )', '  )']
    return '\n'.join(out) + '\n'

def sym_2xN(name, n, pin_names):
    """Dual-row N-pair connector: odd pins left, even pins right."""
    bot = -(n * 2.54) + 1.27
    top = 1.27
    out = [f'  (symbol "{name}"']
    out += ['    (pin_names (offset 1.016) hide)', '    (pin_numbers hide)']
    out += [f'    (property "Reference" "J" (at 2.54 {fv(top+0.5)} 0) {EFF})']
    out += [f'    (property "Value" "{name}" (at 2.54 {fv(bot-0.5)} 0) {EFF})']
    out += [f'    (property "Footprint" "" (at 0 0 0) {EFF_H})']
    out += [f'    (symbol "{name}_0_1"']
    out += [f'      (rectangle (start -2.54 {fv(bot)}) (end 5.08 {fv(top)})']
    out += ['        (stroke (width 0.1524) (type default)) (fill (type background)))']
    out += ['    )']
    out += [f'    (symbol "{name}_1_1"']
    for row in range(n):
        y = fv(-row * 2.54)
        lo = pin_names[2*row];   ln = pin_names[2*row+1]
        out += [f'      (pin passive line (at -5.08 {y} 0) (length 2.54)']
        out += [f'        (name "{lo}" {EFF}) (number "{2*row+1}" {EFF}))']
        out += [f'      (pin passive line (at 7.62 {y} 180) (length 2.54)']
        out += [f'        (name "{ln}" {EFF}) (number "{2*row+2}" {EFF}))']
    out += ['    )', '  )']
    return '\n'.join(out) + '\n'

def sym_pwrflag():
    return f'''  (symbol "PWR_FLAG"
    (power)
    (property "Reference" "#PWR" (at 0 -3.81 0) {EFF_H})
    (property "Value" "PWR_FLAG" (at 0 2.54 0) {EFF})
    (property "Footprint" "" (at 0 0 0) {EFF_H})
    (symbol "PWR_FLAG_0_0"
      (pin power_out line (at 0 0 0) (length 0)
        (name "PWR" {EFF})
        (number "1" {EFF}))
    )
  )
'''

def sym_cap():
    return f'''  (symbol "C_TH"
    (pin_names (offset 0.508))
    (pin_numbers hide)
    (property "Reference" "C" (at 1.905 0 0) {EFF})
    (property "Value" "C_TH" (at 1.905 -2.54 0) {EFF})
    (property "Footprint" "" (at 0 0 0) {EFF_H})
    (symbol "C_TH_0_1"
      (polyline (pts (xy -2.032 -0.762) (xy 2.032 -0.762))
        (stroke (width 0.508) (type default)) (fill (type none)))
      (polyline (pts (xy -2.032 0.762) (xy 2.032 0.762))
        (stroke (width 0.508) (type default)) (fill (type none)))
    )
    (symbol "C_TH_1_1"
      (pin passive line (at 0 3.81 270) (length 3.048)
        (name "+" {EFF}) (number "1" {EFF}))
      (pin passive line (at 0 -3.81 90) (length 3.048)
        (name "-" {EFF}) (number "2" {EFF}))
    )
  )
'''

# ── pin name lists ────────────────────────────────────────────────────────────

# ESP32-S3 Waveshare Touch LCD 3.5 — exact firmware-verified pinout
# Left column = odd pins (1,3,5…31), right column = even pins (2,4,6…32)
ESP32_PINS = [
    "BAT",           "~{VCC_5V}",    # row 0   pins 1,2   BAT / 5V
    "GND",           "GND",          # row 1   pins 3,4   GND / GND
    "TXD_SIM",       "NC",           # row 2   pins 5,6   GPIO21→SIM_RX / NC
    "CS_MAX1",       "NC",           # row 3   pins 7,8   GPIO38→CS1    / NC
    "CLK_SPI",       "NC",           # row 4   pins 9,10  GPIO39→CLK    / NC
    "MOSI_SPI",      "NC",           # row 5   pins 11,12 GPIO40→MOSI   / NC
    "MISO_SPI",      "NC",           # row 6   pins 13,14 GPIO41→MISO   / NC
    "I2C_SCL",       "NC",           # row 7   pins 15,16 GPIO42→SCL    / NC
    "RXD_SIM",       "NC",           # row 8   pins 17,18 GPIO45→SIM_TX / NC
    "CS_MAX2",       "NC",           # row 9   pins 19,20 GPIO46→CS2    / NC
    "I2C_SDA",       "NC",           # row 10  pins 21,22 GPIO47→SDA    / NC
    "NC",            "NC",           # row 11  pins 23,24 GPIO48 / PWR
    "NC",            "NC",           # row 12  pins 25,26 RXD44  / SCL7
    "NC",            "NC",           # row 13  pins 27,28 TXD43  / SDA8
    "GND",           "GND",          # row 14  pins 29,30 GND    / GND
    "~{VCC_3V3}",    "~{VCC_3V3}",   # row 15  pins 31,32 3V3    / 3V3
]

# Official GPIO labels for J1 silkscreen (matches Waveshare pinout diagram)
J1_SILK_LABELS = [
    "BAT",           "5V",           # row 0
    "GND",           "GND",          # row 1
    "GPIO21",        "DN(IO19)",     # row 2  GPIO21=SIM_RX
    "GPIO38",        "DP(IO20)",     # row 3  GPIO38=CS1
    "GPIO39",        "GPIO11",       # row 4  GPIO39=CLK
    "GPIO40",        "GPIO10",       # row 5  GPIO40=MOSI
    "GPIO41",        "GPIO9",        # row 6  GPIO41=MISO
    "GPIO42",        "GPIO17",       # row 7  GPIO42=SCL
    "GPIO45",        "GPIO18",       # row 8  GPIO45=SIM_TX
    "GPIO46",        "BOOT",         # row 9  GPIO46=CS2
    "GPIO47",        "RST",          # row 10 GPIO47=SDA
    "GPIO48",        "PWR",          # row 11
    "RXD(IO44)",     "SCL(IO7)",     # row 12
    "TXD(IO43)",     "SDA(IO8)",     # row 13
    "GND",           "GND",          # row 14
    "3V3",           "3V3",          # row 15
]

SIM800L_PINS    = ["~{VCC_5V}", "GND", "TXD_SIM", "RXD_SIM", "RST_SIM", "PWR_SIM"]
MAX31865_1_PINS = ["~{VCC_3V3}", "GND", "CLK_SPI", "MISO_SPI",
                   "MOSI_SPI", "CS_MAX1", "RTD1_P", "RTD1_N"]
MAX31865_2_PINS = ["~{VCC_3V3}", "GND", "CLK_SPI", "MISO_SPI",
                   "MOSI_SPI", "CS_MAX2", "RTD2_P", "RTD2_N"]
SHT35_1_PINS    = ["~{VCC_3V3}", "GND", "I2C_SDA", "I2C_SCL"]
SHT35_2_PINS    = ["~{VCC_3V3}", "GND", "I2C_SDA", "I2C_SCL"]
PT100_1_PINS    = ["RTD1_P", "RTD1_N"]
PT100_2_PINS    = ["RTD2_P", "RTD2_N"]
LIPO_PINS       = ["BAT", "GND"]
SMA_PINS        = ["ANT_GSM", "GND"]

LIB_SYMS  = "(lib_symbols\n"
LIB_SYMS += sym_pwrflag()
LIB_SYMS += sym_2xN("Conn_02x16",    16, ESP32_PINS)
LIB_SYMS += sym_2xN("Conn_02x04_M1",  4, MAX31865_1_PINS)
LIB_SYMS += sym_2xN("Conn_02x04_M2",  4, MAX31865_2_PINS)
LIB_SYMS += sym_1xN("Conn_01x06_SIM", 6, SIM800L_PINS)
LIB_SYMS += sym_1xN("Conn_01x04_S1",  4, SHT35_1_PINS)
LIB_SYMS += sym_1xN("Conn_01x04_S2",  4, SHT35_2_PINS)
LIB_SYMS += sym_1xN("Conn_01x02_P1",  2, PT100_1_PINS)
LIB_SYMS += sym_1xN("Conn_01x02_P2",  2, PT100_2_PINS)
LIB_SYMS += sym_1xN("Conn_01x02_LP",  2, LIPO_PINS)
LIB_SYMS += sym_1xN("Conn_01x02_SM",  2, SMA_PINS)
LIB_SYMS += sym_cap()
LIB_SYMS += ")\n"

# ── component instance helper ─────────────────────────────────────────────────

def mk_sym(lib_id, ref, value, fp, ax, ay, npins):
    out = [f'(symbol (lib_id "{lib_id}") (at {fv(ax)} {fv(ay)} 0) (unit 1)']
    out += ['  (in_bom yes) (on_board yes) (dnp no)']
    out += [f'  (uuid "{u()}")']
    out += [f'  (property "Reference" "{ref}" (at {fv(ax+3)} {fv(ay-2)} 0) {EFF})']
    out += [f'  (property "Value" "{value}" (at {fv(ax+3)} {fv(ay+0.5)} 0) {EFF})']
    out += [f'  (property "Footprint" "{fp}" (at 0 0 0) {EFF_H})']
    for i in range(1, npins+1):
        out += [f'  (pin "{i}" (uuid "{u()}"))']
    out += [')']
    return '\n'.join(out) + '\n'

# ── global label helpers ──────────────────────────────────────────────────────

LABELS     = []
WIRES      = []
NOCONNECTS = []

def no_connect(x, y):
    NOCONNECTS.append(
        f'(no_connect (at {fv(x)} {fv(y)}) (uuid "{u()}"))\n'
    )

def glabel(net, x, y, angle=0):
    if not net or net in ("NC", "~"):
        return
    LABELS.append(
        f'(global_label "{net}" (shape bidirectional) (at {fv(x)} {fv(y)} {angle})\n'
        f'  {EFF}\n'
        f'  (uuid "{u()}")\n'
        f')\n'
    )

def wire(x1, y1, x2, y2):
    WIRES.append(
        f'(wire (pts (xy {fv(x1)} {fv(y1)}) (xy {fv(x2)} {fv(y2)}))\n'
        f'  (stroke (width 0) (type default)) (uuid "{u()}")\n'
        f')\n'
    )

def attach_1x(pin_names, ax, ay):
    """Attach global labels to a 1xN connector placed at (ax, ay).

    KiCad applies a Y-flip to symbol coordinates when placing them in the
    schematic: absolute_y = sym_y - pin_y_relative.  Pins are defined at
    y = -i*2.54, so absolute_y = sym_y - (-i*2.54) = sym_y + i*2.54.
    """
    GAP = 5.08   # mm between label and pin
    for i, pname in enumerate(pin_names):
        py   = ay + i * 2.54      # y-flip: actual pin row goes DOWNWARD
        px_p = ax - 5.08          # pin connection point (x unchanged)
        if pname == "NC":
            no_connect(px_p, py)
            continue
        if not pname or pname == "~":
            continue
        px_l = px_p - GAP         # label placed to the left
        glabel(pname, px_l, py, 0)
        wire(px_l, py, px_p, py)

def attach_2x(pin_names, ax, ay):
    """Attach global labels to a 2xN connector placed at (ax, ay).

    Same y-flip rule: actual row y = ay + row*2.54.
    """
    GAP = 5.08
    n = len(pin_names) // 2
    for row in range(n):
        y     = ay + row * 2.54   # y-flip: rows go downward
        # left (odd) pin
        lo    = pin_names[2*row]
        if lo and lo not in ("NC", "~"):
            px_lp = ax - 5.08
            px_ll = px_lp - GAP
            glabel(lo, px_ll, y, 0)
            wire(px_ll, y, px_lp, y)
        # right (even) pin
        le    = pin_names[2*row+1]
        if le and le not in ("NC", "~"):
            px_rp = ax + 7.62
            px_rl = px_rp + GAP
            glabel(le, px_rl, y, 180)
            wire(px_rp, y, px_rl, y)
        elif le == "NC":
            no_connect(ax + 7.62, y)

# ── schematic layout ──────────────────────────────────────────────────────────
# Coordinates in mm.  Origin (0,0) is top-left of schematic area.
#
# J1  ESP32 2x16       @ (55, 90)    rows span y=90..y=90-38.1
# J2  SIM800L 1x6      @ (140, 20)
# C1  Capacitor        @ (140, 55)
# J3  MAX31865 #1 2x4  @ (105, 25)
# J4  MAX31865 #2 2x4  @ (105, 65)
# J5  SHT35 #1 1x4     @ (185, 25)
# J6  SHT35 #2 1x4     @ (185, 65)
# J7  PT100 #1 1x2     @ (220, 25)
# J8  PT100 #2 1x2     @ (220, 45)
# J9  LiPo 1x2         @ (220, 60)
# J10 SMA antenna 1x2  @ (220, 75)

SYMS = ""

J1 = (55, 90);   SYMS += mk_sym("Conn_02x16",    "J1",  "ESP32_Connector_2x16",
    "Connector_PinHeader_2.54mm:PinHeader_2x16_P2.54mm_Vertical", *J1, 32)
attach_2x(ESP32_PINS, *J1)

J2 = (140, 20);  SYMS += mk_sym("Conn_01x06_SIM", "J2",  "SIM800L_HDR_1x6",
    "Connector_PinHeader_2.54mm:PinHeader_1x06_P2.54mm_Vertical", *J2, 6)
attach_1x(SIM800L_PINS, *J2)

J3 = (105, 25);  SYMS += mk_sym("Conn_02x04_M1", "J3",  "MAX31865_1_HDR_2x4",
    "Connector_PinHeader_2.54mm:PinHeader_2x04_P2.54mm_Vertical", *J3, 8)
attach_2x(MAX31865_1_PINS, *J3)

J4 = (105, 65);  SYMS += mk_sym("Conn_02x04_M2", "J4",  "MAX31865_2_HDR_2x4",
    "Connector_PinHeader_2.54mm:PinHeader_2x04_P2.54mm_Vertical", *J4, 8)
attach_2x(MAX31865_2_PINS, *J4)

J5 = (185, 25);  SYMS += mk_sym("Conn_01x04_S1", "J5",  "SHT35_1_JST_XH_4p",
    "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical", *J5, 4)
attach_1x(SHT35_1_PINS, *J5)

J6 = (185, 65);  SYMS += mk_sym("Conn_01x04_S2", "J6",  "SHT35_2_JST_XH_4p",
    "Connector_JST:JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical", *J6, 4)
attach_1x(SHT35_2_PINS, *J6)

J7 = (220, 25);  SYMS += mk_sym("Conn_01x02_P1", "J7",  "PT100_1_JST_XH_2p",
    "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical", *J7, 2)
attach_1x(PT100_1_PINS, *J7)

J8 = (220, 40);  SYMS += mk_sym("Conn_01x02_P2", "J8",  "PT100_2_JST_XH_2p",
    "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical", *J8, 2)
attach_1x(PT100_2_PINS, *J8)

J9 = (220, 55);  SYMS += mk_sym("Conn_01x02_LP", "J9",  "LiPo_JST_XH_2p",
    "Connector_JST:JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical", *J9, 2)
attach_1x(LIPO_PINS, *J9)

J10= (220, 70);  SYMS += mk_sym("Conn_01x02_SM", "J10", "SMA_Antenna",
    "Connector_Coaxial:SMA_Molex_73251-1153_Vertical", *J10, 2)
attach_1x(SMA_PINS, *J10)

# ── PWR_FLAG symbols ─────────────────────────────────────────────────────────
# Placed in a clear area (x≈30, y=10..40) away from all connectors.
# Each PWR_FLAG pin is at its placement origin; a global label is attached
# via a 5.08mm wire to the left.

def mk_pwrflag(ref_num, net, ax, ay):
    """Place a PWR_FLAG at (ax,ay) and attach a net label via short wire."""
    out  = f'(symbol (lib_id "PWR_FLAG") (at {fv(ax)} {fv(ay)} 0) (unit 1)\n'
    out += '  (in_bom yes) (on_board yes) (dnp no)\n'
    out += f'  (uuid "{u()}")\n'
    out += f'  (property "Reference" "#PWR{ref_num:02d}" (at 0 -3.81 0) {EFF_H})\n'
    out += f'  (property "Value" "PWR_FLAG" (at 0 -2.54 0) {EFF})\n'
    out += f'  (property "Footprint" "" (at 0 0 0) {EFF_H})\n'
    out += f'  (pin "1" (uuid "{u()}"))\n'
    out += ')\n'
    glabel(net, ax - 5.08, ay, 0)      # label to the left, extends right
    wire(ax - 5.08, ay, ax, ay)         # wire label → PWR_FLAG pin
    return out

SYMS += mk_pwrflag(1, "GND",         30, 10)
SYMS += mk_pwrflag(2, "~{VCC_3V3}", 30, 20)
SYMS += mk_pwrflag(3, "~{VCC_5V}",  30, 30)
SYMS += mk_pwrflag(4, "BAT",         30, 40)

# Capacitor — vertical pins (pin1 + at top, pin2 - at bottom in schematic)
C1 = (140, 55)
SYMS += mk_sym("C_TH", "C1", "1000uF/10V_C1",
    "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", *C1, 2)
# Cap pin 1 (+) defined at y_sym=+3.81 → after y-flip: abs_y = C1[1] - 3.81
# Cap pin 2 (-) defined at y_sym=-3.81 → after y-flip: abs_y = C1[1] + 3.81
c1_top = (C1[0], C1[1] - 3.81)   # + pin (above centre on screen)
c1_bot = (C1[0], C1[1] + 3.81)   # - pin (below centre on screen)
# VCC_5V label goes UP from + pin (smaller y = upward on screen)
glabel("~{VCC_5V}", c1_top[0], c1_top[1] - 5.08, 90)
wire(c1_top[0], c1_top[1], c1_top[0], c1_top[1] - 5.08)
# GND label goes DOWN from - pin (larger y = downward on screen)
glabel("GND",       c1_bot[0], c1_bot[1] + 5.08, 270)
wire(c1_bot[0], c1_bot[1], c1_bot[0], c1_bot[1] + 5.08)

# ── write schematic ───────────────────────────────────────────────────────────

SCH = (
    f'(kicad_sch\n'
    f'  (version 20231120)\n'
    f'  (generator "eeschema")\n'
    f'  (generator_version "8.0")\n'
    f'  (uuid "{u()}")\n'
    f'  (paper "A1")\n\n'
    + LIB_SYMS + '\n'
    + SYMS + '\n'
    + ''.join(LABELS) + '\n'
    + ''.join(WIRES) + '\n'
    + ''.join(NOCONNECTS) + '\n'
    + '  (sheet_instances\n'
    + '    (path "/" (page "1"))\n'
    + '  )\n'
    + ')\n'
)

with open("neolink-v1.kicad_sch", "w") as fh:
    fh.write(SCH)
print("✓ neolink-v1.kicad_sch")

# ─────────────────────────────────────────────────────────────────────────────
# 3. PCB  —  full layout with net assignments, routing and GND zone
# ─────────────────────────────────────────────────────────────────────────────
# Board: 110 × 80 mm
# Footprints placed in logical zones:
#   Left strip  (x≈10–20):  J1 ESP32 header
#   Middle      (x≈30–70):  J3/J4 MAX31865, J5/J6 SHT35, J7/J8 PT100
#   Right strip (x≈80–115): J2 SIM800L, C1, J10 SMA, J9 LiPo

# ── net dictionary ────────────────────────────────────────────────────────────
# GND=1, power=2-4, signal=5-20, unconnected GPIO=21+

NETS = {
    "GND":        1, "~{VCC_3V3}": 2, "~{VCC_5V}": 3, "BAT":     4,
    "CLK_SPI":    5, "MOSI_SPI":   6, "MISO_SPI":  7,
    "CS_MAX1":    8, "CS_MAX2":    9,
    "I2C_SDA":   10, "I2C_SCL":   11,
    "TXD_SIM":   12, "RXD_SIM":   13,
    "RST_SIM":   14, "PWR_SIM":   15,
    "RTD1_P":    16, "RTD1_N":    17,
    "RTD2_P":    18, "RTD2_N":    19,
    "ANT_GSM":   20,
    # NC pads use net 0 (no net) — not listed here
}

def net_decls():
    out = '  (net 0 "")\n'
    for name, nid in NETS.items():
        out += f'  (net {nid} "{name}")\n'
    return out

# ── component positions  (pad-1 origin, mm from board top-left) ───────────────
#
#  Board 110 × 80 mm
#
#   y≈5-15   ┌J3──┐ ┌J5─┐ ┌J7─┐          ┌J2──┐ C1
#   y≈22-32  └J4──┘ └J6─┘ └J8─┘
#   y≈18-59           J1 (ESP32 2×16)
#   y≈67     J9                       J10(SMA)
#
BW, BH = 50, 80   # 50 mm wide × 80 mm tall (long side runs along J1's 16 rows)

# Layout 50×80 mm — portrait orientation
# J1 ESP32 2×16 at left edge (x=2.54), rows y=21..59.1 (centred vertically)
# The board extends ONLY to the RIGHT of J1 (x = 5..50 mm)
#
# Upper area (y < 21 = above J1):
#   J5/J6 SHT35 at (18,5) and (27,5) — different x so routes can pass between them
#   J7/J8 PT100 at (9,5) and (9,14)  — left side of upper area
#
# J1 zone (y=21..59.1): J3/J4 MAX31865 just right of J1
# Middle-right: J2 SIM800L, C1
# Lower area (y > 59.1): J9 LiPo, J10 SMA
#
# Key routing strategy: escape from J1 left column via x=3.81 (between J1 columns,
# 1.27mm from both — just clear of 0.65+0.25+0.20=1.10mm required clearance).
# Routes travel upward at x=3.81, then jog above J1 to reach J5/J6/J7/J8.

POS = {
    'J1':  ( 2.54, 21.00),  # ESP32 2×16        — left edge, rows y=21..59.1
    'J2':  (30.00, 30.00),  # SIM800L 1×6       — centre-right (y 30..42.7)
    'J3':  ( 9.00, 27.50),  # MAX31865 #1 2×4   — rows y=27.5..35.12 (pads x=9..11.54)
    'J4':  (22.00, 38.00),  # MAX31865 #2 2×4   — rows y=38..45.62  (pads x=22..24.54; ≠ J3 x!)
    'J5':  (18.00,  5.00),  # SHT35 #1 1×4 JST  — top area, x=18   (y 5..12.5)
    'J6':  (27.00,  5.00),  # SHT35 #2 1×4 JST  — top area, x=27   (y 5..12.5) ← diff x!
    'J7':  (30.00, 20.00),  # PT100 #1 1×2 JST  — mid-right area  (y 20..22.5) — J3 RTD1
    'J8':  (18.00, 62.00),  # PT100 #2 1×2 JST  — lower area, x=18 (y 62..64.5) — J4 RTD2
    'J9':  ( 9.00, 72.00),  # LiPo 1×2 JST      — bottom-left      (y 72..74.5)
    'J10': (38.00, 70.00),  # SMA coaxial        — bottom-right     (holes to y=73.05, x=41.05)
    'C1':  (38.00, 36.00),  # Capacitor 1000 µF  — next to J2       (pad2 at x+5=43 < 50)
}

# ── pad helpers with net assignment ─────────────────────────────────────────

NET_PADS = {}   # net_name → [(abs_x, abs_y), …]

def mk_pad_n(num, x, y, shape='circle', size=1.8, drill=1.0, nid=0, nname=''):
    net = f'(net {nid} "{nname}") ' if nid else ''
    return (f'  (pad "{num}" thru_hole {shape} (at {fv(x)} {fv(y)}) '
            f'(size {size} {size}) (drill {drill}) (layers "*.Cu" "*.Mask") '
            f'{net}(uuid "{u()}"))\n')

PAD_LIST = []   # (abs_x, abs_y, net_name, half_size) for collision detection

def _reg(net, abs_x, abs_y, half=0.9):
    """Register a pad for routing and collision detection.
    NC/~ pads are registered in PAD_LIST under '_NC_' so tracks respect clearance."""
    if net and net not in ('NC', '~'):
        NET_PADS.setdefault(net, []).append((abs_x, abs_y))
        PAD_LIST.append((abs_x, abs_y, net, half))
    elif net in ('NC', '~'):
        PAD_LIST.append((abs_x, abs_y, '_NC_', half))

# ── silkscreen pin-label helpers ─────────────────────────────────────────────

_NET_LABEL = {
    '~{VCC_3V3}': '3V3',   '~{VCC_5V}': '5V',
    'GND': 'GND',           'BAT': 'BAT',
    'CS_MAX1': 'CS1',       'CS_MAX2': 'CS2',
    'CLK_SPI': 'CLK',       'MOSI_SPI': 'MOSI',   'MISO_SPI': 'MISO',
    'I2C_SDA': 'SDA',       'I2C_SCL': 'SCL',
    'TXD_SIM': 'TXD',       'RXD_SIM': 'RXD',
    'RST_SIM': 'RST',       'PWR_SIM': 'NET',
    'RTD1_P': 'RTD+',       'RTD1_N': 'RTD-',
    'RTD2_P': 'RTD+',       'RTD2_N': 'RTD-',
    'ANT_GSM': 'ANT',       'NC': 'NC',
}

def _net_disp(net):
    """Human-readable label for a net name."""
    if not net or net in ('~', ''):
        return ''
    return _NET_LABEL.get(net, net)

SILK_TEXTS = []   # accumulated gr_text elements at absolute coords

_SIL_SZ = 0.8   # silkscreen text size — matches JLCPCB minimum readable size
_SIL_TH = 0.15  # silkscreen stroke thickness — JLCPCB minimum

def _sil_abs(ax, ay, text, sz=_SIL_SZ):
    """Append a gr_text silk label at absolute PCB coordinates (left-justified)."""
    SILK_TEXTS.append(
        f'  (gr_text "{text}" (at {fv(ax)} {fv(ay)}) (layer "F.SilkS")\n'
        f'    (effects (font (size {sz} {sz}) (thickness {_SIL_TH}))) (uuid "{u()}"))\n'
    )

def _sil(ref_ax, ref_ay, rx, ry, text, sz=_SIL_SZ):
    """Emit a silk label at footprint-relative (rx,ry) → absolute (ref_ax+rx, ref_ay+ry)."""
    _sil_abs(ref_ax + rx, ref_ay + ry, text, sz)

def _hdr(ref, val, ax, ay, fp, pad_rows, sil_dx=1.27):
    out  = f'(footprint "{fp}" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += (f'  (property "Reference" "{ref}" (at {fv(sil_dx)} -2 0) '
            f'(layer "F.SilkS") (effects (font (size 1 1))))\n')
    out += (f'  (property "Value" "{val}" (at {fv(sil_dx)} 1 0) '
            f'(layer "F.Fab") (effects (font (size 1 1))))\n')
    for r in pad_rows: out += r
    out += ')\n'
    return out

def fp_2xN(ref, val, ax, ay, n, pins, labels=None):
    fp   = f'Connector_PinHeader_2.54mm:PinHeader_2x{n:02d}_P2.54mm_Vertical'
    rows = []
    for row in range(n):
        y  = row * 2.54
        p1, p2   = 2*row+1, 2*row+2
        n1, n2   = pins[2*row], pins[2*row+1]
        id1, id2 = NETS.get(n1, 0), NETS.get(n2, 0)
        sh = 'rect' if row == 0 else 'circle'
        rows.append(mk_pad_n(p1, 0,    y, sh,       1.3, 0.8, id1, n1))
        rows.append(mk_pad_n(p2, 2.54, y, 'circle', 1.3, 0.8, id2, n2))
        _reg(n1, ax,      ay + y, half=0.65)
        _reg(n2, ax+2.54, ay + y, half=0.65)
        # Silk labels: left-col pin slightly above row centre, right-col below
        l1 = labels[2*row]   if labels else _net_disp(n1)
        l2 = labels[2*row+1] if labels else _net_disp(n2)
        if l1: _sil(ax, ay, 3.8, y - 0.7, l1)
        if l2: _sil(ax, ay, 3.8, y + 0.7, l2)
    return _hdr(ref, val, ax, ay, fp, rows, 1.27)

def fp_1xN(ref, val, ax, ay, n, pins, pitch=2.54, psz=1.3, dr=0.8, labels=None):
    fp   = f'Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical'
    rows = []
    for i, net in enumerate(pins):
        y  = i * pitch
        sh = 'rect' if i == 0 else 'circle'
        nid = NETS.get(net, 0)
        rows.append(mk_pad_n(i+1, 0, y, sh, psz, dr, nid, net))
        _reg(net, ax, ay + y, half=psz/2)
        lbl = labels[i] if labels else _net_disp(net)
        if lbl: _sil(ax, ay, 1.2, y, lbl)
    return _hdr(ref, val, ax, ay, fp, rows, 0)

def fp_jst(ref, val, ax, ay, n, pins, labels=None):
    fp   = f'Connector_JST:JST_XH_B{n}B-XH-A_1x{n:02d}_P2.50mm_Vertical'
    rows = []
    for i, net in enumerate(pins):
        y  = i * 2.50
        sh = 'rect' if i == 0 else 'circle'
        nid = NETS.get(net, 0)
        rows.append(mk_pad_n(i+1, 0, y, sh, 1.6, 0.9, nid, net))
        _reg(net, ax, ay + y, half=0.8)   # JST pads are 1.6 mm → half=0.8
        lbl = labels[i] if labels else _net_disp(net)
        if lbl: _sil(ax, ay, 1.3, y, lbl)
    return _hdr(ref, val, ax, ay, fp, rows, 0)

def fp_cap(ref, val, ax, ay):
    n1, n2   = '~{VCC_5V}', 'GND'
    id1, id2 = NETS.get(n1, 0), NETS.get(n2, 1)
    rows = [
        mk_pad_n(1, 0,   0, 'rect',   2.0, 1.0, id1, n1),
        mk_pad_n(2, 5.0, 0, 'circle', 2.0, 1.0, id2, n2),
    ]
    _reg(n1, ax,     ay)
    _reg(n2, ax+5.0, ay)
    fp = 'Capacitor_THT:CP_Radial_D10.0mm_P5.00mm'
    out  = f'(footprint "{fp}" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 0 -2.5 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{val}" (at 0 8 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    for r in rows: out += r
    out += ')\n'
    return out

def fp_sma(ref, val, ax, ay):
    sn, gn   = 'ANT_GSM', 'GND'
    sid, gid = NETS.get(sn, 0), 1
    fp = 'Connector_Coaxial:SMA_Molex_73251-1153_Vertical'
    out  = f'(footprint "{fp}" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 0 -6 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{val}" (at 0 5 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    out += mk_pad_n(1, 0, 0, 'circle', 2.0, 1.3, sid, sn)
    _sil(ax, ay, -0.5, -2.3, 'ANT')
    _reg(sn, ax, ay)
    for i, (gx, gy) in enumerate([(-3.05,-3.05),(3.05,-3.05),(-3.05,3.05),(3.05,3.05)]):
        out += mk_pad_n(i+2, gx, gy, 'circle', 2.0, 1.3, gid, gn)
        _reg(gn, ax+gx, ay+gy)
    _sil(ax, ay, 4.2, 0, 'GND')
    out += ')\n'
    return out

# ── routing ───────────────────────────────────────────────────────────────────
#
# Strategy:
#  • GND  → handled entirely by B.Cu copper pour (no F.Cu GND traces)
#  • Power (VCC_3V3, VCC_5V, BAT) → 1 mm traces on F.Cu
#  • Signals → 0.5 mm traces on F.Cu
#
# Each net is routed as a nearest-neighbour chain.  Before generating each
# L-shaped segment the router checks for foreign-net pad collisions and
# falls back to: vertical-first, jog-above, jog-below, or a wider bypass.
#
SEGS     = []
VIAS     = []   # KiCad via strings
CROSSTAB = {'F.Cu': [], 'B.Cu': []}   # committed segments per layer (for crossing check)

def seg(x1, y1, x2, y2, nid, w=0.5, lyr='F.Cu', net_name=''):
    if abs(x1-x2) + abs(y1-y2) < 0.001:
        return
    CROSSTAB[lyr].append((x1, y1, x2, y2, net_name))
    SEGS.append(
        f'  (segment (start {fv(x1)} {fv(y1)}) (end {fv(x2)} {fv(y2)})\n'
        f'    (width {w}) (layer "{lyr}") (net {nid}) (uuid "{u()}"))\n'
    )


_EPS = 0.005   # endpoint-tolerance for crossing / overlap checks

def _no_cross(x1, y1, x2, y2, lyr, net_name=''):
    """True if the axis-aligned segment is compatible with all committed segments on lyr.
    Detects: H×V crossings (including T-junctions at endpoints), and same-axis overlaps.
    Same-net segments are skipped — they may legitimately share endpoints."""
    is_h = abs(y1 - y2) < _EPS
    is_v = abs(x1 - x2) < _EPS
    if not (is_h or is_v):
        return True
    xlo, xhi = min(x1, x2), max(x1, x2)
    ylo, yhi = min(y1, y2), max(y1, y2)
    for ex1, ey1, ex2, ey2, en in CROSSTAB[lyr]:
        if net_name and en and en == net_name:
            continue                          # same net — legitimate overlap/touch
        eH = abs(ey1 - ey2) < _EPS
        eV = abs(ex1 - ex2) < _EPS
        exlo, exhi = min(ex1, ex2), max(ex1, ex2)
        eylo, eyhi = min(ey1, ey2), max(ey1, ey2)
        if is_h and eV:                       # my H  ×  existing V (incl. endpoint T-junctions)
            xe = ex1
            if xlo <= xe <= xhi and eylo <= y1 <= eyhi:
                return False
        elif is_v and eH:                     # my V  ×  existing H (incl. endpoint T-junctions)
            ye = ey1
            if ylo <= ye <= yhi and exlo <= x1 <= exhi:
                return False
        elif is_v and eV:                     # same-x V×V overlap
            if abs(x1 - ex1) < _EPS and ylo < eyhi - _EPS and eylo < yhi - _EPS:
                return False
        elif is_h and eH:                     # same-y H×H overlap
            if abs(y1 - ey1) < _EPS and xlo < exhi - _EPS and exlo < xhi - _EPS:
                return False
    return True

_GAP = 0.20   # minimum DRC clearance

def _clear_h(xa, ya, xb, net_name, tw=0.25):
    """True if horizontal segment at y=ya from x=xa to xb clears all foreign pads."""
    xlo, xhi = min(xa, xb), max(xa, xb)
    for px, py, pn, ph in PAD_LIST:
        if pn == net_name:
            continue
        cx = max(xlo, min(px, xhi))
        dist = math.sqrt((px - cx) ** 2 + (py - ya) ** 2)
        if dist < ph + tw + _GAP:
            return False
    return True

def _clear_v(xa, ya, yb, net_name, tw=0.25):
    """True if vertical segment at x=xa from y=ya to yb clears all foreign pads."""
    ylo, yhi = min(ya, yb), max(ya, yb)
    for px, py, pn, ph in PAD_LIST:
        if pn == net_name:
            continue
        cy = max(ylo, min(py, yhi))
        dist = math.sqrt((px - xa) ** 2 + (py - cy) ** 2)
        if dist < ph + tw + _GAP:
            return False
    return True

def _find_route(x1, y1, x2, y2, net_name, w, lyr):
    """Try pad-clear + crossing-free route on `lyr`.
    Returns list of (ax,ay,bx,by) tuples, or None if impossible."""
    tw = w / 2

    _min_parallel = 2 * tw + _GAP   # min centre-to-centre for parallel tracks

    def ch(xa, ya, xb):
        if not (_clear_h(xa, ya, xb, net_name, tw) and
                _no_cross(xa, ya, xb, ya, lyr, net_name)):
            return False
        # Parallel H-to-H spacing: reject if any committed H on lyr is
        # within _min_parallel (vertically) and overlaps this segment's x range.
        xlo_, xhi_ = min(xa, xb), max(xa, xb)
        for ex1, ey1, ex2, ey2, en in CROSSTAB[lyr]:
            if en == net_name:
                continue
            if abs(ey1 - ey2) < _EPS:   # existing H at y=ey1
                if abs(ya - ey1) < _min_parallel:
                    exlo_, exhi_ = min(ex1, ex2), max(ex1, ex2)
                    if xlo_ < exhi_ and exlo_ < xhi_:
                        return False
        return True

    def cv(xa, ya, yb):
        if not (_clear_v(xa, ya, yb, net_name, tw) and
                _no_cross(xa, ya, xa, yb, lyr, net_name)):
            return False
        # Parallel V-to-V spacing: reject if any committed V on lyr is
        # within _min_parallel (laterally) and overlaps this segment's y range.
        ylo_, yhi_ = min(ya, yb), max(ya, yb)
        for ex1, ey1, ex2, ey2, en in CROSSTAB[lyr]:
            if en == net_name:
                continue
            if abs(ex1 - ex2) < _EPS:   # existing V at x=ex1
                if abs(xa - ex1) < _min_parallel:
                    eylo_, eyhi_ = min(ey1, ey2), max(ey1, ey2)
                    if ylo_ < eyhi_ and eylo_ < yhi_:
                        return False
        return True

    # A: H → V
    if ch(x1, y1, x2) and cv(x2, y1, y2):
        return [(x1, y1, x2, y1), (x2, y1, x2, y2)]

    # B: V → H
    if cv(x1, y1, y2) and ch(x1, y2, x2):
        return [(x1, y1, x1, y2), (x1, y2, x2, y2)]

    # C: jog ABOVE
    for dy in [1.25, 2.54, 5.08, 7.62, 10.16, 15.0]:
        yj = min(y1, y2) - dy
        if yj < 1.0:
            continue
        if cv(x1, y1, yj) and ch(x1, yj, x2) and cv(x2, yj, y2):
            return [(x1, y1, x1, yj), (x1, yj, x2, yj), (x2, yj, x2, y2)]

    # D: jog BELOW
    for dy in [1.25, 2.54, 5.08, 7.62, 10.16, 15.0]:
        yj = max(y1, y2) + dy
        if yj > BH - 1.0:
            continue
        if cv(x1, y1, yj) and ch(x1, yj, x2) and cv(x2, yj, y2):
            return [(x1, y1, x1, yj), (x1, yj, x2, yj), (x2, yj, x2, y2)]

    # E+F: horizontal escape to xj
    _xj_cands = set()
    for base in [x1, x2, (x1+x2)/2]:
        for sign in (-1, +1):
            for dx in (1.25, 1.27, 1.35, 2.54, 3.81, 5.08, 7.62, 10.16, 12.7, 15.0, 20.0, 25.0):
                _xj_cands.add(round(base + sign * dx, 3))
    # Always add board-edge corridors (essential for J1 left-col leftward escapes)
    _xj_cands.add(1.0)
    _xj_cands.add(round(BW - 1.0, 3))
    for xj in sorted(_xj_cands, key=lambda v: abs(v - x1) + abs(v - x2)):
        if xj < 1.0 or xj > BW - 1.0:
            continue
        if ch(x1, y1, xj) and cv(xj, y1, y2) and ch(xj, y2, x2):
            return [(x1, y1, xj, y1), (xj, y1, xj, y2), (xj, y2, x2, y2)]

    # G: 4-segment S-bend  H→V→H→V
    _DX = [1.25, 1.27, 1.35, 2.54, 3.81, 5.08, 7.62, 10.16, 12.7, 15.0, 20.0, 25.0, 30.0]
    _DY = [1.25, 1.27, 1.35, 2.54, 3.81, 5.08, 7.62, 10.16, 12.7, 15.0, 20.0]
    _xj_set, _yj_set = set(), set()
    # Always include board-edge corridors for J1 left-col leftward escapes
    _xj_set.add(1.0)
    _xj_set.add(round(BW - 1.0, 3))
    for _b in [x1, x2, (x1 + x2) / 2]:
        for _s in (-1, +1):
            for _d in _DX:
                _v = round(_b + _s * _d, 3)
                if 1.0 <= _v <= BW - 1.0:
                    _xj_set.add(_v)
    for _b in [y1, y2, (y1 + y2) / 2]:
        for _s in (-1, +1):
            for _d in _DY:
                _v = round(_b + _s * _d, 3)
                if 1.0 <= _v <= BH - 1.0:
                    _yj_set.add(_v)
    # Sort xj: prefer values ≤ x1 first (escape left avoids J1-column congestion),
    # then use wire-length key to break further ties.
    def _xj_key(v): return (abs(v - x1) + abs(v - x2), 1 if v > x1 else 0)
    for _xj in sorted(_xj_set, key=_xj_key):
        for _yj in sorted(_yj_set, key=lambda v: abs(v - y1) + abs(v - y2)):
            if (ch(x1, y1, _xj) and cv(_xj, y1, _yj) and
                    ch(_xj, _yj, x2) and cv(x2, _yj, y2)):
                return [(x1, y1, _xj, y1), (_xj, y1, _xj, _yj),
                        (_xj, _yj, x2, _yj), (x2, _yj, x2, y2)]

    # G2: V→H→V→H  (escape vertically first — needed when horizontal escape
    #     from x1,y1 is blocked by adjacent connector pads on the same row)
    def _yj_key(v): return (abs(v - y1) + abs(v - y2), 1 if v > y1 else 0)
    for _yj in sorted(_yj_set, key=_yj_key):
        for _xj in sorted(_xj_set, key=_xj_key):
            if (cv(x1, y1, _yj) and ch(x1, _yj, _xj) and
                    cv(_xj, _yj, y2) and ch(_xj, y2, x2)):
                return [(x1, y1, x1, _yj), (x1, _yj, _xj, _yj),
                        (_xj, _yj, _xj, y2), (_xj, y2, x2, y2)]

    return None   # no valid route found on this layer


_FAR_LEFT = 10.0   # not actively used by safe_route (kept for reference)

_VIA_DRILL = 0.4   # via drill radius (0.8mm hole)
_VIA_SIZE  = 0.9   # via annular ring outer radius → pad size 1.8mm

def _via_clear(xv, yv, net_name):
    """True if a via at (xv,yv) clears all foreign-net pads AND routed tracks."""
    min_d = _VIA_SIZE + _GAP        # pad clearance: via annular ring edge + gap
    track_d = _VIA_SIZE + 0.25 + _GAP   # track clearance: via edge + half track + gap
    # Pad check
    for px, py, pn, ph in PAD_LIST:
        if pn == net_name:
            continue
        dist = math.sqrt((px - xv) ** 2 + (py - yv) ** 2)
        if dist < ph + min_d:
            return False
    # Existing routed track check (both layers)
    for lyr in ('F.Cu', 'B.Cu'):
        for ex1, ey1, ex2, ey2, en in CROSSTAB[lyr]:
            if en == net_name:
                continue
            eH = abs(ey1 - ey2) < _EPS
            eV = abs(ex1 - ex2) < _EPS
            if eH:
                cx = max(min(ex1, ex2), min(xv, max(ex1, ex2)))
                dist = math.sqrt((xv - cx) ** 2 + (yv - ey1) ** 2)
            elif eV:
                cy = max(min(ey1, ey2), min(yv, max(ey1, ey2)))
                dist = math.sqrt((xv - ex1) ** 2 + (yv - cy) ** 2)
            else:
                continue
            if dist < track_d:
                return False
    return True

def _commit_via(xv, yv, nid, net_name=''):
    """Write a via to VIAS, register tiny sentinel in CROSSTAB, and register in PAD_LIST
    so future routes respect the via's DRC clearance zone."""
    drill_d = fv(_VIA_DRILL * 2)
    size_d  = fv(_VIA_SIZE  * 2)
    VIAS.append(
        f'  (via (at {fv(xv)} {fv(yv)}) (size {size_d}) (drill {drill_d})'
        f' (layers "F.Cu" "B.Cu") (net {nid}) (uuid "{u()}"))\n'
    )
    eps = 0.001
    for lyr in ('F.Cu', 'B.Cu'):
        CROSSTAB[lyr].append((xv - eps, yv, xv + eps, yv, net_name))
    # Register via as a pseudo-pad so _clear_h/_clear_v enforce DRC clearance
    PAD_LIST.append((xv, yv, net_name, _VIA_SIZE))

def _try_via_route(x1, y1, x2, y2, nid, net_name, w):
    """Route through a single via when direct single-layer routing fails.
    Searches the full board at 2.54mm grid for a clear via landing point."""
    cands = set()
    # Full-board 2.54mm grid (cast wide net for congested boards)
    xi = 2.54
    while xi <= BW - 2.0:
        yi = 2.54
        while yi <= BH - 2.0:
            cands.add((round(xi, 3), round(yi, 3)))
            yi += 2.54
        xi += 2.54
    # Axis-aligned intermediates relative to this route
    for alpha in (0.25, 0.5, 0.75):
        cands.add((round(x1 + alpha * (x2 - x1), 3), round(y1, 3)))
        cands.add((round(x1, 3),                      round(y1 + alpha * (y2 - y1), 3)))
        cands.add((round(x2, 3),                      round(y1 + alpha * (y2 - y1), 3)))
        cands.add((round(x1 + alpha * (x2 - x1), 3), round(y2, 3)))

    valid = [(x, y) for x, y in cands
             if 1.0 <= x <= BW - 1.0 and 1.0 <= y <= BH - 1.0
             and _via_clear(x, y, net_name)]

    # Prefer via candidates near the route midpoint to minimise wire length
    mx, my = (x1 + x2) / 2, (y1 + y2) / 2
    valid.sort(key=lambda p: (p[0] - mx) ** 2 + (p[1] - my) ** 2)

    for xv, yv in valid:
        for l1, l2 in (('F.Cu', 'B.Cu'), ('B.Cu', 'F.Cu')):
            s1 = _find_route(x1, y1, xv, yv, net_name, w, l1)
            if s1 is None:
                continue
            s2 = _find_route(xv, yv, x2, y2, net_name, w, l2)
            if s2 is None:
                continue
            for ax, ay, bx, by in s1:
                seg(ax, ay, bx, by, nid, w, l1, net_name)
            _commit_via(xv, yv, nid, net_name)
            for ax, ay, bx, by in s2:
                seg(ax, ay, bx, by, nid, w, l2, net_name)
            return True
    return False

def safe_route(x1, y1, x2, y2, nid, net_name, w, force_layer=None):
    """Route on the best available layer; _no_cross prevents same-layer crossings.

    Layer preference (portrait board, J1 at left edge x≈2.54..5.08):
      • forced via hint              → that layer first, other as fallback
      • both endpoints right of J1   → B.Cu first (clear rightward corridor)
      • J1 pad → right side          → B.Cu first
      • otherwise                    → F.Cu first
    """
    if force_layer:
        layers = (force_layer, 'B.Cu' if force_layer == 'F.Cu' else 'F.Cu')
    elif min(x1, x2) > _J1_XMAX:      # both clearly right of J1
        layers = ('B.Cu', 'F.Cu')
    elif max(x1, x2) > _J1_XMAX:      # one end in J1, other to right → rightward escape
        layers = ('B.Cu', 'F.Cu')
    else:                              # short / within J1 zone
        layers = ('F.Cu', 'B.Cu')
    for lyr in layers:
        segs = _find_route(x1, y1, x2, y2, net_name, w, lyr)
        if segs is not None:
            for ax, ay, bx, by in segs:
                seg(ax, ay, bx, by, nid, w, lyr, net_name)
            return
    # Via fallback — insert one PTH via to switch layers
    if _try_via_route(x1, y1, x2, y2, nid, net_name, w):
        return
    # Last resort — unchecked H→V (should be rare; causes DRC crossing)
    print(f"  FALLBACK {net_name}: ({x1:.2f},{y1:.2f})→({x2:.2f},{y2:.2f}) layers={layers}")
    lyr = layers[0]
    seg(x1, y1, x2, y1, nid, w, lyr, net_name)
    seg(x2, y1, x2, y2, nid, w, lyr, net_name)

def nn_chain(pts):
    """Nearest-neighbour ordering of pad positions for one net."""
    if len(pts) <= 1:
        return list(pts)
    chain = [pts[0]]
    rem   = list(pts[1:])
    while rem:
        last    = chain[-1]
        nearest = min(rem, key=lambda p: (p[0]-last[0])**2+(p[1]-last[1])**2)
        chain.append(nearest)
        rem.remove(nearest)
    return chain

POWER_W  = 1.0
SIGNAL_W = 0.5
_PWR_NETS = frozenset()  # All nets use 0.5mm signal traces; 2.54mm pitch pads require ≤1.10mm clearance

def _net_span(pts):
    """Horizontal span of a net's pads — used to sort routing order."""
    xs = [p[0] for p in pts]
    return max(xs) - min(xs)

# J1 sits at x=2.54..5.08.  Pads from J1 act as routing hubs.
_J1_XMIN, _J1_XMAX = 1.5, 6.5

_J3_YRANGE = (26.5, 36.5)   # J3 pads span y≈27.5..35.12
_J4_YRANGE = (37.0, 47.0)   # J4 pads span y≈38..45.62 (now at x=22..24.54)

def _route_pairs(pts):
    """Return (p1, p2, layer_hint) routing pairs as a nearest-neighbour chain.

    Strategy: full NN chain across all pads. J1 pads act as preferred start nodes.
    This avoids duplicate star routes (e.g. J1→J5 AND J1→J6) and lets natural
    chains like J1→J3→J4→J5→J6 emerge, keeping each segment short.

    Layer hints:
      • J1→J3 zone (F.Cu) or J1→J4 zone (B.Cu) based on destination y
      • J1 internal chain → F.Cu
      • All other segments → None (safe_route decides)
    """
    j1    = [p for p in pts if _J1_XMIN <= p[0] <= _J1_XMAX]
    other = [p for p in pts if p not in j1]

    if not j1:
        chain = nn_chain(list(pts))
        return [(a, b, None) for a, b in zip(chain, chain[1:])]

    pairs = []
    if len(j1) > 1:
        c = nn_chain(j1)
        pairs.extend((a, b, 'F.Cu') for a, b in zip(c, c[1:]))

    if not other:
        return pairs

    # Chain non-J1 pads in NN order starting from the one closest to any J1 pad
    o_chain = nn_chain(other)
    hub = min(j1, key=lambda h: (h[0]-o_chain[0][0])**2 + (h[1]-o_chain[0][1])**2)

    # Layer hint for J1 → first other pad
    first = o_chain[0]
    if _J3_YRANGE[0] <= first[1] <= _J3_YRANGE[1] and first[0] < 15.0:
        hint0 = 'F.Cu'
    elif _J4_YRANGE[0] <= first[1] <= _J4_YRANGE[1]:
        hint0 = 'B.Cu'
    else:
        hint0 = None
    pairs.append((hub, first, hint0))

    # Chain remaining other pads.
    _UPPER_Y = 15.0   # J5/J6 pads span y=5..12.5; upper zone cutoff
    for i in range(1, len(o_chain)):
        a, b = o_chain[i-1], o_chain[i]
        if a[1] < _UPPER_Y and b[1] < _UPPER_Y:
            hint = 'F.Cu'   # J5↔J6 upper-zone hops on F.Cu, freeing B.Cu for VCC_3V3 vertical
        else:
            hint = None
        pairs.append((a, b, hint))

    return pairs

def route_gnd(x1, y1, x2, y2, nid, w):
    """Route GND on B.Cu, respecting pad clearance, track crossings, and parallel spacing."""
    tw = w / 2
    _min_p = 2 * tw + _GAP
    def ch(xa, ya, xb):
        if not (_clear_h(xa, ya, xb, 'GND', tw) and
                _no_cross(xa, ya, xb, ya, 'B.Cu', 'GND')):
            return False
        xlo_, xhi_ = min(xa, xb), max(xa, xb)
        for ex1, ey1, ex2, ey2, en in CROSSTAB['B.Cu']:
            if en == 'GND': continue
            if abs(ey1 - ey2) < _EPS and abs(ya - ey1) < _min_p:
                exlo_, exhi_ = min(ex1, ex2), max(ex1, ex2)
                if xlo_ < exhi_ and exlo_ < xhi_: return False
        return True
    def cv(xa, ya, yb):
        if not (_clear_v(xa, ya, yb, 'GND', tw) and
                _no_cross(xa, ya, xa, yb, 'B.Cu', 'GND')):
            return False
        ylo_, yhi_ = min(ya, yb), max(ya, yb)
        for ex1, ey1, ex2, ey2, en in CROSSTAB['B.Cu']:
            if en == 'GND': continue
            if abs(ex1 - ex2) < _EPS and abs(xa - ex1) < _min_p:
                eylo_, eyhi_ = min(ey1, ey2), max(ey1, ey2)
                if ylo_ < eyhi_ and eylo_ < yhi_: return False
        return True
    if abs(x1 - x2) < _EPS and abs(y1 - y2) < _EPS:
        return
    # Simple options: H-V, V-H
    if ch(x1, y1, x2) and cv(x2, y1, y2):
        seg(x1, y1, x2, y1, nid, w, 'B.Cu', 'GND')
        seg(x2, y1, x2, y2, nid, w, 'B.Cu', 'GND'); return
    if cv(x1, y1, y2) and ch(x1, y2, x2):
        seg(x1, y1, x1, y2, nid, w, 'B.Cu', 'GND')
        seg(x1, y2, x2, y2, nid, w, 'B.Cu', 'GND'); return
    # Jog above / below
    for dy in [2.54, 5.08, 10.0, 15.0, 20.0, 30.0, 40.0]:
        yj = min(y1, y2) - dy
        if yj >= 1.0 and cv(x1, y1, yj) and ch(x1, yj, x2) and cv(x2, yj, y2):
            seg(x1, y1, x1, yj, nid, w, 'B.Cu', 'GND')
            seg(x1, yj, x2, yj, nid, w, 'B.Cu', 'GND')
            seg(x2, yj, x2, y2, nid, w, 'B.Cu', 'GND'); return
        yj = max(y1, y2) + dy
        if yj <= BH - 1.0 and cv(x1, y1, yj) and ch(x1, yj, x2) and cv(x2, yj, y2):
            seg(x1, y1, x1, yj, nid, w, 'B.Cu', 'GND')
            seg(x1, yj, x2, yj, nid, w, 'B.Cu', 'GND')
            seg(x2, yj, x2, y2, nid, w, 'B.Cu', 'GND'); return
    # GND zone fill covers connectivity — skip unchecked fallback to avoid crossings
    pass

def route_all():
    """Two-pass routing: left-side signals on F.Cu first, right-side on B.Cu second.
    This strict layer separation prevents cross-contamination between layers."""
    signal_nets = [
        (name, pts) for name, pts in NET_PADS.items()
        if name != 'GND' and len(pts) >= 2 and NETS.get(name, 0)
    ]

    def _pairs_for_net(name, pts):
        nid = NETS[name]
        w   = POWER_W if name in _PWR_NETS else SIGNAL_W
        return [(nid, w, p, q, hint) for p, q, hint in _route_pairs(pts)]

    # Classify each (p→q) pair as LEFT (F.Cu), RIGHT (B.Cu), or CROSS
    # Each job carries an optional force_layer hint from _route_pairs.
    left_jobs  = []   # both endpoints on left of J1
    right_jobs = []   # both endpoints on right of J1
    cross_jobs = []   # span crosses J1 column

    for name, pts in signal_nets:
        for nid, w, (px,py), (qx,qy), hint in _pairs_for_net(name, pts):
            xmax = max(px, qx)
            xmin = min(px, qx)
            if xmax < _J1_XMAX:
                left_jobs.append((name, nid, w, px, py, qx, qy, hint))
            elif xmin > _J1_XMIN:
                right_jobs.append((name, nid, w, px, py, qx, qy, hint))
            else:
                cross_jobs.append((name, nid, w, px, py, qx, qy, hint))

    # Sort within each group: SHORT routes first (claim tight corridors),
    # LONG routes last (find paths around).  Tie-break by net name for stability.
    def _sort_key(t):
        # Round span to 3 dp to avoid float-ordering noise between equal-span routes
        return (round(abs(t[4]-t[6])+abs(t[3]-t[5]), 3), t[0])
    for grp in (left_jobs, right_jobs, cross_jobs):
        grp.sort(key=_sort_key)

    # ── Manual pre-routes ────────────────────────────────────────────────────────
    # These nets have congested paths the search algorithm cannot resolve.
    # Committing them first lets all subsequent algorithmic routes plan around them.
    _MANUALLY_ROUTED = set()

    def _mroute(net_name, segs_lyr):
        """Emit explicit segments without clearance-checking."""
        nid = NETS.get(net_name, 0)
        w   = POWER_W if net_name in _PWR_NETS else SIGNAL_W
        for (ax, ay, bx, by), lyr in segs_lyr:
            seg(ax, ay, bx, by, nid, w, lyr, net_name)
        _MANUALLY_ROUTED.add(net_name)

    # MOSI_SPI  J1(2.54,33.70) → J3(9,32.58) → J4(22,43.08)  B.Cu
    # J1→J3: V down 1.12mm from J1 MOSI pad, then H right at y=32.58 split at jog x=6.46.
    # J3→J4: continues V down in corridor at x=6.46 then H to J4.
    # Key clearances: J1 NC@(5.08,33.70) 1.12mm ✓; J3 RTD1_P@(9,35.12) 2.54mm ✓;
    #   x=6.46 is 1.38mm right of J1 right-col (x=5.08), avoids x=10.16 used by CS_MAX1 via.
    _mroute('MOSI_SPI', [
        ((2.54, 33.70,  2.54, 32.58), 'B.Cu'),  # V: J1 MOSI down to H-rail
        ((2.54, 32.58,  6.46, 32.58), 'B.Cu'),  # H: J1 → jog (clears J1 NC@y=33.70 by 1.12mm)
        ((6.46, 32.58,  9.00, 32.58), 'B.Cu'),  # H: jog → J3 MOSI (T-junction at x=6.46)
        ((6.46, 32.58,  6.46, 43.08), 'B.Cu'),  # V: jog down to J4 level (1.38mm from J1 right-col)
        ((6.46, 43.08, 22.00, 43.08), 'B.Cu'),  # H: reach J4 MOSI
    ])

    # TXD_SIM  J1(2.54, 26.08) → J2(30, 35.08)  B.Cu
    # Exits left past board edge to avoid NC pad at (5.08, 26.08),
    # jogs up to y=24.81 (midpoint between J1 rows 23.54 and 26.08, clear of J3),
    # crosses right to x=21 (clear of J3 x<12 and J4 x>22),
    # descends to J2 TXD y=35.08 and arrives horizontally (skips J3 RTD1_P at x=9).
    _mroute('TXD_SIM', [
        ((2.54, 26.08,  1.00, 26.08), 'B.Cu'),   # H exit left (avoids NC @ x=5.08)
        ((1.00, 26.08,  1.00, 24.81), 'B.Cu'),   # V up to board-edge corridor
        ((1.00, 24.81, 21.00, 24.81), 'B.Cu'),   # H above J3 row-0 (y=27.5)
        ((21.00, 24.81, 21.00, 35.08), 'B.Cu'),  # V down (J3 @ x≤12, J4 @ x≥22)
        ((21.00, 35.08, 30.00, 35.08), 'B.Cu'),  # H to J2 TXD (J3 RTD1_P @ x=9 excluded)
    ])

    # ── Algorithmic routing ──────────────────────────────────────────────────────
    # Original fast config: RTD1_N forced F.Cu keeps B.Cu corridor clear for SPI.
    _FORCE_FCU  = frozenset(['RTD1_N'])
    _EARLY_NETS = frozenset(['BAT', 'RTD1_P', 'RTD1_N', 'RTD2_P', 'RTD2_N',
                             'I2C_SDA', 'I2C_SCL'])
    combined   = right_jobs + cross_jobs
    early_jobs = [t for t in combined if t[0] in _EARLY_NETS]
    rest       = [t for t in combined if t[0] not in _EARLY_NETS]
    for name, nid, w, px, py, qx, qy, hint in left_jobs + early_jobs + rest:
        if name in _MANUALLY_ROUTED:
            continue
        if name in _FORCE_FCU and hint is None:
            hint = 'F.Cu'
        safe_route(px, py, qx, qy, nid, name, w, force_layer=hint)

    # Route GND explicitly on B.Cu so kicad-cli drc sees connectivity
    # (zone fill later provides the full copper coverage)
    gnd_id = NETS.get('GND', 0)
    if gnd_id:
        gnd_pts = NET_PADS.get('GND', set())
        if len(gnd_pts) >= 2:
            chain = nn_chain(gnd_pts)
            for i in range(len(chain) - 1):
                route_gnd(chain[i][0], chain[i][1],
                          chain[i+1][0], chain[i+1][1], gnd_id, SIGNAL_W)

# ── footprint instances ───────────────────────────────────────────────────────

_MAX_LABELS = ["VIN","GND","CLK","SDO","SDI","CS","RTD+","RTD-"]

FP_J1  = fp_2xN("J1",  "ESP32_2x16",  *POS['J1'],  16, ESP32_PINS,
                labels=J1_SILK_LABELS)
FP_J2  = fp_1xN("J2",  "SIM800L_1x6", *POS['J2'],   6, SIM800L_PINS,
                labels=["VCC","GND","TXD","RXD","RST","NET"])
FP_J3  = fp_2xN("J3",  "MAX31865_1",  *POS['J3'],   4, MAX31865_1_PINS,
                labels=_MAX_LABELS)
FP_J4  = fp_2xN("J4",  "MAX31865_2",  *POS['J4'],   4, MAX31865_2_PINS,
                labels=_MAX_LABELS)
FP_J5  = fp_jst("J5",  "SHT35_1",     *POS['J5'],   4, SHT35_1_PINS,
                labels=["VCC","GND","SDA","SCL"])
FP_J6  = fp_jst("J6",  "SHT35_2",     *POS['J6'],   4, SHT35_2_PINS,
                labels=["VCC","GND","SDA","SCL"])
FP_J7  = fp_jst("J7",  "PT100_1",     *POS['J7'],   2, PT100_1_PINS,
                labels=["RTD+","RTD-"])
FP_J8  = fp_jst("J8",  "PT100_2",     *POS['J8'],   2, PT100_2_PINS,
                labels=["RTD+","RTD-"])
FP_J9  = fp_jst("J9",  "LiPo",        *POS['J9'],   2, LIPO_PINS,
                labels=["BAT+","BAT-"])
FP_J10 = fp_sma("J10", "ANT_GSM",     *POS['J10'])
FP_C1  = fp_cap("C1",  "1000uF_10V",  *POS['C1'])

route_all()   # fills SEGS from NET_PADS

# ── GND copper pour on B.Cu ──────────────────────────────────────────────────

GND_ZONE = (
    f'  (zone (net 1) (net_name "GND") (layer "B.Cu") (uuid "{u()}")\n'
    f'    (hatch edge 0.508)\n'
    f'    (connect_pads yes (clearance 0.3))\n'
    f'    (min_thickness 0.25)\n'
    f'    (filled_areas_thickness no)\n'
    f'    (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.4))\n'
    f'    (polygon\n'
    f'      (pts\n'
    f'        (xy 0 0) (xy {BW} 0) (xy {BW} {BH}) (xy 0 {BH})\n'
    f'      )\n'
    f'    )\n'
    f'  )\n'
)

# ── board outline & header ────────────────────────────────────────────────────

PCB = (
    f'(kicad_pcb (version 20230607) (generator "pcbnew")\n'
    f'  (general (thickness 1.6) (legacy_teardrops no))\n'
    f'  (paper "A3")\n'
    f'  (layers\n'
    f'    (0 "F.Cu" signal)\n'
    f'    (31 "B.Cu" signal)\n'
    f'    (36 "B.SilkS" user "B.Silkscreen")\n'
    f'    (37 "F.SilkS" user "F.Silkscreen")\n'
    f'    (38 "B.Mask" user)\n'
    f'    (39 "F.Mask" user)\n'
    f'    (44 "Edge.Cuts" user)\n'
    f'    (45 "F.CrtYd" user "F.Courtyard")\n'
    f'    (46 "B.CrtYd" user "B.Courtyard")\n'
    f'    (47 "F.Fab" user "F.Fab")\n'
    f'    (48 "B.Fab" user "B.Fab")\n'
    f'  )\n'
    f'  (setup\n'
    f'    (pad_to_mask_clearance 0)\n'
    f'    (pcbplotparams\n'
    f'      (layerselection 0x00010fc_ffffffff)\n'
    f'      (outputformat 1)\n'
    f'      (outputdirectory "gerbers/")\n'
    f'    )\n'
    f'  )\n'
    + net_decls()
    + f'  (gr_rect (start 0 0) (end {BW} {BH})\n'
      f'    (stroke (width 0.05) (type solid)) (layer "Edge.Cuts") (uuid "{u()}"))\n\n'
    + f'  (gr_text "NEOLINK V1" (at 25 {BH-5}) (layer "F.SilkS")\n'
      f'    (effects (font (size 2 2) (thickness 0.3))) (uuid "{u()}"))\n'
    + f'  (gr_text "ESP32-S3 Waveshare Shield" (at 25 {BH-2.5}) (layer "F.SilkS")\n'
      f'    (effects (font (size 1.2 1.2) (thickness 0.18))) (uuid "{u()}"))\n\n'
    + FP_J1 + FP_J2 + FP_J3 + FP_J4 + FP_J5
    + FP_J6 + FP_J7 + FP_J8 + FP_J9 + FP_J10 + FP_C1
    + '\n'
    + ''.join(SILK_TEXTS)
    + ''.join(SEGS)
    + ''.join(VIAS)
    + '\n'
    + GND_ZONE
    + ')\n'
)

with open("neolink-v1.kicad_pcb", "w") as fh:
    fh.write(PCB)
print("✓ neolink-v1.kicad_pcb written (F.Cu + B.Cu routing, B.Cu GND pour)")

# ── Fix silkscreen DFM warnings BEFORE zone-fill (compact format is easiest) ─
import importlib.util as _ilu, os as _os
_fix_path  = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "fix_silk.py")
_pcb_local = _os.path.abspath("neolink-v1.kicad_pcb")
if _os.path.exists(_fix_path):
    import sys as _sys
    _saved_argv = _sys.argv[:]
    _sys.argv = [_fix_path, _pcb_local]
    _spec = _ilu.spec_from_file_location("fix_silk", _fix_path)
    _mod  = _ilu.module_from_spec(_spec)
    _spec.loader.exec_module(_mod)
    _sys.argv = _saved_argv

# ── Fill GND zone using KiCad's own Python so kicad-cli drc sees copper ─────
import subprocess, tempfile, os

_KICAD_PY = (
    "/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework"
    "/Versions/3.9/bin/python3"
)
_FILL_SCRIPT = """\
import sys, os
sys.path.insert(0,
    '/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework'
    '/Versions/3.9/lib/python3.9/site-packages')
import pcbnew
pcb = pcbnew.LoadBoard(sys.argv[1])
filler = pcbnew.ZONE_FILLER(pcb)
filler.Fill(pcb.Zones())
pcb.Save(sys.argv[1])
print('zones filled OK')
"""

if os.path.exists(_KICAD_PY):
    _pcb_abs = os.path.abspath("neolink-v1.kicad_pcb")
    _res = subprocess.run(
        [_KICAD_PY, '-c', _FILL_SCRIPT, _pcb_abs],
        capture_output=True, text=True
    )
    if _res.returncode == 0:
        print("✓ GND zone filled via pcbnew")
    else:
        print("⚠ zone fill failed:", _res.stderr[:300])
else:
    print("⚠ KiCad Python not found — GND zone unfilled (fill manually in KiCad)")

print("\nAll KiCad 7 files generated.")
print("Open neolink-v1.kicad_pro in KiCad 7+ to review schematic and PCB.")
