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

ESP32_PINS = [
    "GND",           "GND",          # row 0   pins 1,2
    "~{VCC_3V3}",   "~{VCC_5V}",    # row 1   pins 3,4
    "BAT",           "CS_MAX1",      # row 2   pins 5,6   GPIO38  → CS1
    "CLK_SPI",       "CS_MAX2",      # row 3   pins 7,8   GPIO39  → CLK, GPIO46 → CS2
    "MOSI_SPI",      "MISO_SPI",     # row 4   pins 9,10  GPIO40  → MOSI, GPIO41 → MISO
    "I2C_SCL",       "I2C_SDA",      # row 5   pins 11,12 GPIO42  → SCL,  GPIO47 → SDA
    "TXD_SIM",       "RXD_SIM",      # row 6   pins 13,14 GPIO21←SIM_TXD, GPIO45→SIM_RXD
    "GPIO1",         "GPIO2",        # row 7   pins 15,16 (unconnected)
    "GPIO3",         "GPIO4",        # row 8   pins 17,18
    "GPIO5",         "GPIO6",        # row 9   pins 19,20
    "GPIO7",         "GPIO8",        # row 10  pins 21,22
    "GPIO9",         "GPIO10",       # row 11  pins 23,24
    "GPIO11",        "GPIO12",       # row 12  pins 25,26
    "GPIO13",        "GPIO14",       # row 13  pins 27,28
    "GPIO15",        "GPIO16",       # row 14  pins 29,30
    "GPIO17",        "NC",           # row 15  pins 31,32
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
    # Unconnected GPIO pads keep named nets (no routing needed)
    "GPIO1":21, "GPIO2":22, "GPIO3":23, "GPIO4":24, "GPIO5":25,
    "GPIO6":26, "GPIO7":27, "GPIO8":28, "GPIO9":29, "GPIO10":30,
    "GPIO11":31,"GPIO12":32,"GPIO13":33,"GPIO14":34,"GPIO15":35,
    "GPIO16":36,"GPIO17":37,
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
BW, BH = 110, 80

POS = {
    'J1':  (50.0, 18.0),   # ESP32 2×16        — center
    'J2':  (90.0, 14.19),  # SIM800L 1×6 — aligned to J1 row midpoints
    'J3':  ( 5.0,  5.0),   # MAX31865 #1 2×4   — left top
    'J4':  ( 5.0, 22.0),   # MAX31865 #2 2×4   — left
    'J5':  (60.0,  5.0),   # SHT35 #1 1×4 JST  — right of J1, top (avoids y-conflicts with J3/J4)
    'J6':  (60.0, 22.0),   # SHT35 #2 1×4 JST  — right of J1
    'J7':  (70.0,  5.0),   # PT100 #1 1×2 JST  — top, beside J5
    'J8':  (70.0, 14.0),   # PT100 #2 1×2 JST  — beside J6
    'J9':  ( 5.0, 67.0),   # LiPo 1×2 JST      — bottom left
    'J10': (97.0, 62.0),   # SMA coaxial        — bottom right
    'C1':  (79.0, 14.0),   # Capacitor 1000 µF next to SIM800L (pad2 at x+5)
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
    """Register a pad for routing and collision detection."""
    if net and net not in ('NC', '~'):
        NET_PADS.setdefault(net, []).append((abs_x, abs_y))
        PAD_LIST.append((abs_x, abs_y, net, half))

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

def fp_2xN(ref, val, ax, ay, n, pins):
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
    return _hdr(ref, val, ax, ay, fp, rows, 1.27)

def fp_1xN(ref, val, ax, ay, n, pins, pitch=2.54, psz=1.3, dr=0.8):
    fp   = f'Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical'
    rows = []
    for i, net in enumerate(pins):
        y  = i * pitch
        sh = 'rect' if i == 0 else 'circle'
        nid = NETS.get(net, 0)
        rows.append(mk_pad_n(i+1, 0, y, sh, psz, dr, nid, net))
        _reg(net, ax, ay + y, half=psz/2)
    return _hdr(ref, val, ax, ay, fp, rows, 0)

def fp_jst(ref, val, ax, ay, n, pins):
    fp   = f'Connector_JST:JST_XH_B{n}B-XH-A_1x{n:02d}_P2.50mm_Vertical'
    rows = []
    for i, net in enumerate(pins):
        y  = i * 2.50
        sh = 'rect' if i == 0 else 'circle'
        nid = NETS.get(net, 0)
        rows.append(mk_pad_n(i+1, 0, y, sh, 1.6, 0.9, nid, net))
        _reg(net, ax, ay + y, half=0.8)   # JST pads are 1.6 mm → half=0.8
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
    _reg(sn, ax, ay)
    for i, (gx, gy) in enumerate([(-3.05,-3.05),(3.05,-3.05),(-3.05,3.05),(3.05,3.05)]):
        out += mk_pad_n(i+2, gx, gy, 'circle', 2.0, 1.3, gid, gn)
        _reg(gn, ax+gx, ay+gy)
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
SEGS = []

def seg(x1, y1, x2, y2, nid, w=0.5, lyr='F.Cu'):
    if abs(x1-x2) + abs(y1-y2) < 0.001:
        return
    SEGS.append(
        f'  (segment (start {fv(x1)} {fv(y1)}) (end {fv(x2)} {fv(y2)})\n'
        f'    (width {w}) (layer "{lyr}") (net {nid}) (uuid "{u()}"))\n'
    )

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

def safe_route(x1, y1, x2, y2, nid, net_name, w):
    """Pad-aware routing with 8 fallback strategies (H, V, jog-above/below/left/right, diagonals)."""
    tw = w / 2  # actual trace half-width for clearance checks

    def ch(xa, ya, xb):
        return _clear_h(xa, ya, xb, net_name, tw)

    def cv(xa, ya, yb):
        return _clear_v(xa, ya, yb, net_name, tw)

    def emit4(ax, ay, bx, by, cx, cy, dx, dy):
        seg(ax, ay, bx, by, nid, w)
        seg(bx, by, cx, cy, nid, w)
        seg(cx, cy, dx, dy, nid, w)

    # A: H then V  (L-shape)
    if ch(x1, y1, x2) and cv(x2, y1, y2):
        seg(x1, y1, x2, y1, nid, w)
        seg(x2, y1, x2, y2, nid, w)
        return

    # B: V then H  (L-shape)
    if cv(x1, y1, y2) and ch(x1, y2, x2):
        seg(x1, y1, x1, y2, nid, w)
        seg(x1, y2, x2, y2, nid, w)
        return

    # C: jog ABOVE (3-seg via yj < min(y1,y2))
    for dy in [2.54, 5.08, 7.62, 10.16, 15.0]:
        yj = min(y1, y2) - dy
        if yj < 1.0:
            continue
        if cv(x1, y1, yj) and ch(x1, yj, x2) and cv(x2, yj, y2):
            emit4(x1, y1, x1, yj, x2, yj, x2, y2)
            return

    # D: jog BELOW (3-seg via yj > max(y1,y2))
    for dy in [2.54, 5.08, 7.62, 10.16, 15.0]:
        yj = max(y1, y2) + dy
        if yj > BH - 1.0:
            continue
        if cv(x1, y1, yj) and ch(x1, yj, x2) and cv(x2, yj, y2):
            emit4(x1, y1, x1, yj, x2, yj, x2, y2)
            return

    # E+F: horizontal jog — escape to xj, then vertical, then horizontal.
    _xj_cands = set()
    for base in [x1, x2]:
        for sign in (-1, +1):
            for dx in (2.54, 5.08, 7.62, 10.16, 15.0, 20.0, 25.0):
                _xj_cands.add(round(base + sign * dx, 3))
    for xj in sorted(_xj_cands, key=lambda v: abs(v - x1) + abs(v - x2)):
        if xj < 1.0 or xj > BW - 1.0:
            continue
        if ch(x1, y1, xj) and cv(xj, y1, y2) and ch(xj, y2, x2):
            emit4(x1, y1, xj, y1, xj, y2, x2, y2)
            return

    # G: comprehensive 4-segment S-bend search.
    # Pattern: H(x1→xj), V(y1→yj), H(xj→x2), V(yj→y2).
    _DX = [1.27, 2.54, 3.81, 5.08, 7.62, 10.16, 12.7, 15.0, 20.0, 25.0, 30.0]
    _DY = [1.27, 2.54, 3.81, 5.08, 7.62, 10.16, 12.7, 15.0, 20.0]
    _xj_set = set()
    for _b in [x1, x2, (x1 + x2) / 2]:
        for _s in (-1, +1):
            for _d in _DX:
                _v = round(_b + _s * _d, 3)
                if 1.0 <= _v <= BW - 1.0:
                    _xj_set.add(_v)
    _yj_set = set()
    for _b in [y1, y2, (y1 + y2) / 2]:
        for _s in (-1, +1):
            for _d in _DY:
                _v = round(_b + _s * _d, 3)
                if 1.0 <= _v <= BH - 1.0:
                    _yj_set.add(_v)
    _xj_sorted = sorted(_xj_set, key=lambda v: abs(v - x1) + abs(v - x2))
    _yj_sorted = sorted(_yj_set, key=lambda v: abs(v - y1) + abs(v - y2))
    for _xj in _xj_sorted:
        for _yj in _yj_sorted:
            if (ch(x1, y1, _xj) and cv(_xj, y1, _yj) and
                    ch(_xj, _yj, x2) and cv(x2, _yj, y2)):
                seg(x1, y1, _xj, y1, nid, w)
                seg(_xj, y1, _xj, _yj, nid, w)
                seg(_xj, _yj, x2, _yj, nid, w)
                seg(x2, _yj, x2, y2, nid, w)
                return

    # H: last resort — plain H-first L (may leave minor DRC warning)
    seg(x1, y1, x2, y1, nid, w)
    seg(x2, y1, x2, y2, nid, w)

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
_PWR_NETS = frozenset({'~{VCC_3V3}', '~{VCC_5V}', 'BAT'})

def route_all():
    for net_name, pts in NET_PADS.items():
        if net_name == 'GND':
            continue   # GND via B.Cu pour
        if len(pts) < 2:
            continue
        nid = NETS.get(net_name, 0)
        if not nid:
            continue
        w     = POWER_W if net_name in _PWR_NETS else SIGNAL_W
        chain = nn_chain(pts)
        for i in range(len(chain) - 1):
            safe_route(chain[i][0], chain[i][1],
                       chain[i+1][0], chain[i+1][1], nid, net_name, w)

# ── footprint instances ───────────────────────────────────────────────────────

FP_J1  = fp_2xN("J1",  "ESP32_2x16",  *POS['J1'],  16, ESP32_PINS)
FP_J2  = fp_1xN("J2",  "SIM800L_1x6", *POS['J2'],   6, SIM800L_PINS)
FP_J3  = fp_2xN("J3",  "MAX31865_1",  *POS['J3'],   4, MAX31865_1_PINS)
FP_J4  = fp_2xN("J4",  "MAX31865_2",  *POS['J4'],   4, MAX31865_2_PINS)
FP_J5  = fp_jst("J5",  "SHT35_1",     *POS['J5'],   4, SHT35_1_PINS)
FP_J6  = fp_jst("J6",  "SHT35_2",     *POS['J6'],   4, SHT35_2_PINS)
FP_J7  = fp_jst("J7",  "PT100_1",     *POS['J7'],   2, PT100_1_PINS)
FP_J8  = fp_jst("J8",  "PT100_2",     *POS['J8'],   2, PT100_2_PINS)
FP_J9  = fp_jst("J9",  "LiPo",        *POS['J9'],   2, LIPO_PINS)
FP_J10 = fp_sma("J10", "ANT_GSM",     *POS['J10'])
FP_C1  = fp_cap("C1",  "1000uF_10V",  *POS['C1'])

route_all()   # fills SEGS from NET_PADS

# ── GND copper pour on B.Cu ──────────────────────────────────────────────────

GND_ZONE = (
    f'  (zone (net 1) (net_name "GND") (layer "B.Cu") (uuid "{u()}")\n'
    f'    (hatch edge 0.508)\n'
    f'    (connect_pads (clearance 0.2))\n'
    f'    (min_thickness 0.25)\n'
    f'    (filled_areas_thickness no)\n'
    f'    (fill yes (thermal_gap 0.5) (thermal_bridge_width 0.25))\n'
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
    + f'  (gr_text "NEOLINK V1" (at 5 {BH-6}) (layer "F.SilkS")\n'
      f'    (effects (font (size 2 2) (thickness 0.3))) (uuid "{u()}"))\n'
    + f'  (gr_text "ESP32-S3 Waveshare Shield" (at 5 {BH-3}) (layer "F.SilkS")\n'
      f'    (effects (font (size 1.2 1.2) (thickness 0.18))) (uuid "{u()}"))\n\n'
    + FP_J1 + FP_J2 + FP_J3 + FP_J4 + FP_J5
    + FP_J6 + FP_J7 + FP_J8 + FP_J9 + FP_J10 + FP_C1
    + '\n'
    + ''.join(SEGS)
    + '\n'
    + GND_ZONE
    + ')\n'
)

with open("neolink-v1.kicad_pcb", "w") as fh:
    fh.write(PCB)
print("✓ neolink-v1.kicad_pcb (nets, F.Cu routing, B.Cu GND pour)")

print("\nAll KiCad 7 files generated.")
print("Open neolink-v1.kicad_pro in KiCad 7+ to review schematic and PCB.")
