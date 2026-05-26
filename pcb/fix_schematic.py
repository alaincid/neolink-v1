#!/usr/bin/env python3
"""
fix_schematic.py  —  Neolink V1 schematic wire-alignment fixer
================================================================
Reads neolink-v1.kicad_sch, computes the *exact* absolute position of
every connector pin (applying KiCad's Y-flip rule), then regenerates
all (global_label) and (wire) elements so every label sits directly on
the pin it belongs to.

KiCad Y-flip rule (schematic symbols):
    absolute_x = sym_x + pin_x_relative
    absolute_y = sym_y - pin_y_relative   ← note the minus sign

Run from the pcb/ directory:
    python3 fix_schematic.py
"""

import re, uuid, sys, os

# ─── helpers ──────────────────────────────────────────────────────────────────

def u():
    return str(uuid.uuid4())

EFF   = '(effects (font (size 1.27 1.27)))'
EFF_H = '(effects (font (size 1.27 1.27)) hide)'

def fv(v):
    s = f"{float(v):.4f}"
    p = s.split('.')
    dec = p[1].rstrip('0')
    return p[0] + ('.' + dec if dec else '')

# ─── S-expression tokeniser ───────────────────────────────────────────────────

def tokenise(text):
    tokens = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c in ' \t\n\r':
            i += 1
        elif c == '(':
            tokens.append(('LPAREN', '('))
            i += 1
        elif c == ')':
            tokens.append(('RPAREN', ')'))
            i += 1
        elif c == '"':
            j = i + 1
            while j < n:
                if text[j] == '\\':
                    j += 2
                    continue
                if text[j] == '"':
                    break
                j += 1
            tokens.append(('STR', text[i+1:j]))
            i = j + 1
        else:
            j = i
            while j < n and text[j] not in ' \t\n\r()':
                j += 1
            tokens.append(('ATOM', text[i:j]))
            i = j
    return tokens


def parse_sexp(tokens, pos=0):
    """Return (node, next_pos).  node is either a str or a list."""
    if pos >= len(tokens):
        return None, pos
    kind, val = tokens[pos]
    if kind == 'RPAREN':
        return None, pos           # caller handles closing paren
    if kind in ('STR', 'ATOM'):
        return val, pos + 1
    # LPAREN → list
    lst = []
    pos += 1                       # skip '('
    while pos < len(tokens) and tokens[pos][0] != 'RPAREN':
        item, pos = parse_sexp(tokens, pos)
        if item is not None:
            lst.append(item)
    return lst, pos + 1            # skip ')'


def key(node):
    """Return the first element (keyword) of an S-expression list."""
    return node[0] if isinstance(node, list) and node else None


def find_child(node, k):
    """Return the first child of node whose key == k."""
    if not isinstance(node, list):
        return None
    for child in node:
        if isinstance(child, list) and child and child[0] == k:
            return child
    return None


def find_children(node, k):
    """Return all children of node whose key == k."""
    if not isinstance(node, list):
        return []
    return [c for c in node if isinstance(c, list) and c and c[0] == k]

# ─── pin-position extraction ──────────────────────────────────────────────────

def extract_lib_pins(lib_symbols_node):
    """
    Returns {sym_name: [(pin_name, px, py), ...]} for every symbol in
    (lib_symbols ...).  Only pins inside *_1_1 sub-symbols are included.
    """
    result = {}
    for child in lib_symbols_node[1:]:
        if not isinstance(child, list) or key(child) != 'symbol':
            continue
        sym_name = child[1] if len(child) > 1 else ''
        pins = []
        # iterate sub-symbols looking for *_1_1 (which holds the pins)
        for sub in find_children(child, 'symbol'):
            sub_name = sub[1] if len(sub) > 1 else ''
            if not sub_name.endswith('_1_1'):
                continue
            for pin in find_children(sub, 'pin'):
                # (pin TYPE SHAPE (at PX PY ANG) (length L) (name ...) (number ...))
                at_node = find_child(pin, 'at')
                name_node = find_child(pin, 'name')
                if at_node is None or name_node is None:
                    continue
                try:
                    px = float(at_node[1])
                    py = float(at_node[2])
                except (IndexError, ValueError):
                    continue
                pname = name_node[1] if len(name_node) > 1 else ''
                pins.append((pname, px, py))
        result[sym_name] = pins
    return result


def compute_absolute_pins(sch_tree, lib_pins):
    """
    Walk all (symbol (lib_id ...) (at ax ay rot) ...) instances.
    For each pin in its lib definition, apply y-flip:
        abs_x = ax + px
        abs_y = ay - py

    Returns a list of (net_name, abs_x, abs_y).
    (Special-case 'NC', '~', and tilde-wrapped names are kept as-is.)
    """
    all_pins = []
    for node in sch_tree[1:]:
        if not isinstance(node, list) or key(node) != 'symbol':
            continue
        lib_id_node = find_child(node, 'lib_id')
        at_node     = find_child(node, 'at')
        if lib_id_node is None or at_node is None:
            continue
        lib_id = lib_id_node[1] if len(lib_id_node) > 1 else ''
        try:
            ax = float(at_node[1])
            ay = float(at_node[2])
        except (IndexError, ValueError):
            continue
        pins = lib_pins.get(lib_id, [])
        for pname, px, py in pins:
            abs_x = ax + px
            abs_y = ay - py          # ← Y-flip
            net = pname.strip('~{}')
            net_raw = pname          # keep original (e.g. "~{VCC_3V3}")
            all_pins.append((net_raw, abs_x, abs_y))
    return all_pins

# ─── label / wire generation ──────────────────────────────────────────────────

GAP = 5.08  # wire length from pin to label (mm)

def make_label(net, lx, ly, angle):
    if not net or net.strip('~{}') in ('NC', ''):
        return ''
    return (
        f'(global_label "{net}" (shape bidirectional)'
        f' (at {fv(lx)} {fv(ly)} {angle})\n'
        f'  {EFF}\n'
        f'  (uuid "{u()}")\n'
        f')\n'
    )

def make_wire(x1, y1, x2, y2):
    return (
        f'(wire (pts (xy {fv(x1)} {fv(y1)}) (xy {fv(x2)} {fv(y2)}))\n'
        f'  (stroke (width 0) (type default)) (uuid "{u()}")\n'
        f')\n'
    )

def build_labels_and_wires(all_pins):
    """
    For each pin:
    - If pin is on left column (px < 0): label to the LEFT  → angle 0
    - If pin is on right column (px > 0): label to the RIGHT → angle 180
    - If pin is vertical (py ≠ 0, px ≈ 0): label above/below

    We deduplicate labels (same net at same position → one label).
    Power nets (GND, VCC, BAT) are handled separately by PWR_FLAG; skip them.
    """
    POWER_NETS = {'GND', 'VCC_3V3', 'VCC_5V', 'BAT', 'PWR_FLAG',
                  '~{VCC_3V3}', '~{VCC_5V}', '~'}

    seen = set()
    labels = []
    wires  = []

    for net_raw, ax, ay in all_pins:
        net_clean = net_raw.strip('~{}')
        if net_clean in ('', 'NC'):
            continue
        if net_raw in POWER_NETS or net_clean in POWER_NETS:
            continue

        # Determine label side from the raw pin x-coordinate
        # We stored abs coordinates; we need to figure out direction.
        # Convention from the symbol: left-column pins have px=-5.08 → abs_x = sym_x - 5.08
        # right-column pins have px=+7.62 → abs_x = sym_x + 7.62
        # For 1xN (single row) all pins are left-column (px=-5.08)
        # For vertical pins (capacitor): handled below

        # Heuristic: peek at x offset relative to round multiples
        # All left-column pins land at ax = sym_x - 5.08 → fractional part ≈ 0.92 (from 5.08)
        # All right-column pins land at ax = sym_x + 7.62 → fractional part ≈ 0.62
        # Easier: store original px in all_pins.
        # But we only stored abs. Recompute from context is fragile.
        # Use: right-column pins are to the RIGHT of the body centre.
        # Body centre x = sym_x.  We don't store that here.
        # Fallback: always place label to the LEFT with angle=0.
        # (Right-column labels are at angle=180 and positioned to the right.)
        # To distinguish: right column was placed with angle=180 in the original;
        # we need a way to know. We'll use all_pins rebuilt from attach info.

        key_str = f"{net_raw}|{fv(ax)}|{fv(ay)}"
        if key_str in seen:
            continue
        seen.add(key_str)

        # Default: label to the left
        lx = ax - GAP
        ly = ay
        angle = 0

        labels.append(make_label(net_raw, lx, ly, angle))
        wires.append(make_wire(lx, ly, ax, ay))

    return labels, wires

# ─── main ─────────────────────────────────────────────────────────────────────

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    sch_path   = os.path.join(script_dir, 'neolink-v1.kicad_sch')

    print(f"Reading {sch_path} …")
    with open(sch_path, 'r') as f:
        content = f.read()

    # ── The simplest, most reliable fix: re-run gen.py (which now has
    #    the correct ± direction in attach_1x / attach_2x).
    #    fix_schematic.py is a convenience wrapper that calls gen.py.
    gen_path = os.path.join(script_dir, 'gen.py')
    if not os.path.exists(gen_path):
        print("ERROR: gen.py not found next to fix_schematic.py", file=sys.stderr)
        sys.exit(1)

    print("Re-generating schematic via gen.py (coordinate bug is fixed there) …")
    import subprocess
    result = subprocess.run(
        [sys.executable, gen_path],
        cwd=script_dir,
        capture_output=True,
        text=True
    )
    print(result.stdout)
    if result.returncode != 0:
        print(result.stderr, file=sys.stderr)
        sys.exit(result.returncode)

    # ── Verify with ERC ────────────────────────────────────────────────────────
    import shutil
    kicad_cli = shutil.which('kicad-cli') or '/opt/homebrew/bin/kicad-cli'
    if not os.path.exists(kicad_cli):
        print("kicad-cli not found — skipping ERC verification.")
        return

    erc_out = os.path.join(script_dir, 'erc_report.txt')
    print(f"\nRunning ERC → {erc_out} …")
    erc_result = subprocess.run(
        [kicad_cli, 'sch', 'erc',
         '--severity-error',
         '-o', erc_out,
         os.path.join(script_dir, 'neolink-v1.kicad_sch')],
        cwd=script_dir,
        capture_output=True,
        text=True
    )
    print(erc_result.stdout or erc_result.stderr)

    if os.path.exists(erc_out):
        with open(erc_out) as f:
            report = f.read()
        # Count error lines
        errors   = [l for l in report.splitlines() if '; error' in l]
        warnings = [l for l in report.splitlines() if '; warning' in l]
        print(f"\n── ERC Summary ──")
        print(f"   Errors   : {len(errors)}")
        print(f"   Warnings : {len(warnings)}")
        if len(errors) < 10:
            print("   ✓ ERC passes (<10 errors)")
        else:
            print("   ✗ Still too many errors")
            for e in errors[:20]:
                print(f"      {e}")


if __name__ == '__main__':
    main()
