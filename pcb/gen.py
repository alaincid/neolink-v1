#!/usr/bin/env python3
"""
Neolink V1 — KiCad 7 project file generator.
Generates:
  neolink-v1.kicad_pro  — project metadata
  neolink-v1.kicad_sch  — schematic (all connectors + global labels)
  neolink-v1.kicad_pcb  — PCB layout (footprints placed, no routes)

Run from pcb/ directory:  python3 gen.py
"""

import uuid, json

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
    "GND",           "GND",          # row 0
    "~{VCC_3V3}",   "~{VCC_5V}",    # row 1
    "BAT",           "GPIO38",       # row 2  CS_MAX1
    "GPIO39",        "GPIO46",       # row 3  CLK / CS_MAX2
    "GPIO40",        "GPIO41",       # row 4  MOSI / MISO
    "GPIO42",        "GPIO47",       # row 5  SCL / SDA
    "GPIO21",        "GPIO45",       # row 6  RX_SIM / TX_SIM
    "GPIO1",         "GPIO2",        # row 7
    "GPIO3",         "GPIO4",        # row 8
    "GPIO5",         "GPIO6",        # row 9
    "GPIO7",         "GPIO8",        # row 10
    "GPIO9",         "GPIO10",       # row 11
    "GPIO11",        "GPIO12",       # row 12
    "GPIO13",        "GPIO14",       # row 13
    "GPIO15",        "GPIO16",       # row 14
    "GPIO17",        "NC",           # row 15
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
# 3. PCB
# ─────────────────────────────────────────────────────────────────────────────
# Board: 120 × 90 mm, all through-hole, no copper traces.
# Footprints placed in logical zones:
#   Left strip  (x≈10–20):  J1 ESP32 header
#   Middle      (x≈30–70):  J3/J4 MAX31865, J5/J6 SHT35, J7/J8 PT100
#   Right strip (x≈80–115): J2 SIM800L, C1, J10 SMA, J9 LiPo

NETS = {
    "GND": 1, "~{VCC_3V3}": 2, "~{VCC_5V}": 3, "BAT": 4,
    "GPIO38": 5, "GPIO39": 6, "GPIO40": 7, "GPIO41": 8,
    "GPIO42": 9, "GPIO45": 10, "GPIO46": 11, "GPIO47": 12,
    "GPIO21": 13, "CLK_SPI": 14, "MOSI_SPI": 15, "MISO_SPI": 16,
    "CS_MAX1": 17, "CS_MAX2": 18, "I2C_SDA": 19, "I2C_SCL": 20,
    "RXD_SIM": 21, "TXD_SIM": 22, "RTD1_P": 23, "RTD1_N": 24,
    "RTD2_P": 25, "RTD2_N": 26, "ANT_GSM": 27, "RST_SIM": 28,
}

def net_decls():
    out = '  (net 0 "")\n'
    for name, nid in NETS.items():
        out += f'  (net {nid} "{name}")\n'
    return out

def mk_pad(num, x, y, shape="circle", size=1.8, drill=1.0):
    return (f'  (pad "{num}" thru_hole {shape} (at {fv(x)} {fv(y)}) '
            f'(size {size} {size}) (drill {drill}) (layers "*.Cu" "*.Mask") '
            f'(uuid "{u()}"))\n')

def fp_header_2xN(ref, value, ax, ay, n):
    fp = f"Connector_PinHeader_2.54mm:PinHeader_2x{n:02d}_P2.54mm_Vertical"
    out  = f'(footprint "{fp}" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 1.27 -2 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{value}" (at 1.27 1 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    for row in range(n):
        y = row * 2.54
        p1 = 2*row+1; p2 = 2*row+2
        sh = "rect" if p1 == 1 else "circle"
        out += mk_pad(p1, 0,    y, sh)
        out += mk_pad(p2, 2.54, y)
    out += ')\n'
    return out

def fp_header_1xN(ref, value, ax, ay, n):
    fp = f"Connector_PinHeader_2.54mm:PinHeader_1x{n:02d}_P2.54mm_Vertical"
    out  = f'(footprint "{fp}" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 0 -2 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{value}" (at 0 1 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    for i in range(n):
        sh = "rect" if i == 0 else "circle"
        out += mk_pad(i+1, 0, i*2.54, sh)
    out += ')\n'
    return out

def fp_jst_1xN(ref, value, ax, ay, n):
    fp = f"Connector_JST:JST_XH_B{n}B-XH-A_1x{n:02d}_P2.50mm_Vertical"
    out  = f'(footprint "{fp}" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 0 -2 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{value}" (at 0 1 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    for i in range(n):
        sh = "rect" if i == 0 else "circle"
        out += mk_pad(i+1, 0, i*2.5, sh, size=1.6, drill=0.9)
    out += ')\n'
    return out

def fp_cap(ref, value, ax, ay):
    out  = f'(footprint "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 0 -2.5 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{value}" (at 0 8 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    out += mk_pad(1, 0,   0, "rect",   size=2.0, drill=1.0)   # + anode
    out += mk_pad(2, 5.0, 0, "circle", size=2.0, drill=1.0)   # - cathode
    out += ')\n'
    return out

def fp_sma(ref, value, ax, ay):
    out  = f'(footprint "Connector_Coaxial:SMA_Molex_73251-1153_Vertical" (layer "F.Cu") (uuid "{u()}")\n'
    out += f'  (at {fv(ax)} {fv(ay)})\n'
    out += f'  (property "Reference" "{ref}" (at 0 -6 0) (layer "F.SilkS") (effects (font (size 1 1))))\n'
    out += f'  (property "Value" "{value}" (at 0 5 0) (layer "F.Fab") (effects (font (size 1 1))))\n'
    out += mk_pad(1, 0, 0, "circle", size=2.0, drill=1.3)          # signal
    for i, (gx, gy) in enumerate([(-3.05,-3.05),(3.05,-3.05),(-3.05,3.05),(3.05,3.05)]):
        out += mk_pad(i+2, gx, gy, "circle", size=2.0, drill=1.3)  # GND mounting
    out += ')\n'
    return out

# ── board outline & header ────────────────────────────────────────────────────

BW, BH = 120, 90   # board dimensions mm

PCB = f"""(kicad_pcb (version 20230607) (generator "pcbnew")
  (general (thickness 1.6) (legacy_teardrops no))
  (paper "A3")
  (layers
    (0 "F.Cu" signal)
    (31 "B.Cu" signal)
    (36 "B.SilkS" user "B.Silkscreen")
    (37 "F.SilkS" user "F.Silkscreen")
    (38 "B.Mask" user)
    (39 "F.Mask" user)
    (44 "Edge.Cuts" user)
    (45 "F.CrtYd" user "F.Courtyard")
    (46 "B.CrtYd" user "B.Courtyard")
    (47 "F.Fab" user "F.Fab")
    (48 "B.Fab" user "B.Fab")
  )
  (setup
    (pad_to_mask_clearance 0)
    (pcbplotparams
      (layerselection 0x00010fc_ffffffff)
      (outputformat 1)
      (outputdirectory "gerbers/")
    )
  )
{net_decls()}
  (gr_rect (start 0 0) (end {BW} {BH})
    (stroke (width 0.05) (type solid)) (layer "Edge.Cuts") (uuid "{u()}"))

  (gr_text "NEOLINK V1" (at 5 3) (layer "F.SilkS")
    (effects (font (size 2 2) (thickness 0.3))) (uuid "{u()}"))
  (gr_text "Shield ESP32-S3 Waveshare Touch LCD 3.5" (at 5 6) (layer "F.SilkS")
    (effects (font (size 1.2 1.2) (thickness 0.18))) (uuid "{u()}"))

"""

# ── footprint placement ───────────────────────────────────────────────────────
# All coordinates: distance from board corner (0,0)

# J1 — ESP32 connector 2×16  (left edge, centred in height)
# 16 rows × 2.54 = 38.1 mm; place top pin at y=5 → bottom at y=43.1
PCB += fp_header_2xN("J1", "ESP32_2x16", 10, 5, 16)

# J3 — MAX31865 #1 (2×4)
PCB += fp_header_2xN("J3", "MAX31865_1", 25, 5, 4)

# J4 — MAX31865 #2 (2×4)
PCB += fp_header_2xN("J4", "MAX31865_2", 38, 5, 4)

# J5 — SHT35 #1 JST-XH 4p
PCB += fp_jst_1xN("J5", "SHT35_1", 53, 5, 4)

# J6 — SHT35 #2 JST-XH 4p
PCB += fp_jst_1xN("J6", "SHT35_2", 63, 5, 4)

# J7 — PT100 #1 JST-XH 2p
PCB += fp_jst_1xN("J7", "PT100_1", 25, 50, 2)

# J8 — PT100 #2 JST-XH 2p
PCB += fp_jst_1xN("J8", "PT100_2", 35, 50, 2)

# J9 — LiPo JST-XH 2p
PCB += fp_jst_1xN("J9", "LiPo", 45, 50, 2)

# J2 — SIM800L 1×6
PCB += fp_header_1xN("J2", "SIM800L_1x6", 85, 5, 6)

# C1 — 1000uF bulk capacitor (next to SIM800L, +pin toward VCC trace)
PCB += fp_cap("C1", "1000uF_10V", 97, 7)

# J10 — SMA coaxial antenna
PCB += fp_sma("J10", "ANT_GSM", 110, 12)

PCB += ")\n"

with open("neolink-v1.kicad_pcb", "w") as fh:
    fh.write(PCB)
print("✓ neolink-v1.kicad_pcb")

print("\nAll KiCad 7 files generated.")
print("Open neolink-v1.kicad_pro in KiCad 7+ to review schematic and PCB.")
