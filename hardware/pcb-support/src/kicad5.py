# Export au format KiCad 5.1 (le format que les imports EasyEDA Standard et Pro
# acceptent) : PCB (.kicad_pcb), schéma (.sch), symboles (.lib + cache),
# empreintes (.pretty), projet (.pro).
import math
import os

import model
from geom import RULES, WIDTH

OX, OY = 100.0, 60.0          # position de la carte sur la page KiCad (mm)
TITLE = 'TrimBox — PCB support ESP32-S3 Mini Nano'
DATE = '2026-10-17'
REV = '1'


def q(s):
    s = str(s)
    if s == '' or any(c in s for c in ' ()"\'\t') or not s.isascii():
        return '"' + s.replace('"', '\\"') + '"'
    return s


def f(v):
    return f'{v:.4f}'.rstrip('0').rstrip('.') if isinstance(v, float) else str(v)


def ts(n):
    return f'{0x5F000000 + n:08X}'


# ============================================================ PCB
def footprint_sexpr(c, netidx, layer_note=None):
    fp = c.fp
    L = []
    L.append(f'  (module {q("TrimBox:" + fp.name)} (layer F.Cu) (tedit {ts(0)}) (tstamp {ts(0x1000 + COMP_TS[c.ref])})')
    L.append(f'    (at {f(OX + c.x)} {f(OY + c.y)}{" " + f(float(c.rot)) if c.rot else ""})')
    if fp.descr:
        L.append(f'    (descr {q(fp.descr)})')
    L.append(f'    (path /{ts(COMP_TS[c.ref])})')
    if fp.attr == 'smd':
        L.append('    (attr smd)')
    elif fp.attr == 'virtual':
        L.append('    (attr virtual)')
    # textes : référence sur la sérigraphie (petite), valeur sur la couche Fab
    if fp.crtyd:
        top = fp.crtyd[1] - 0.6
        bot = fp.crtyd[3] + 0.6
    else:
        top, bot = -2, 2
    hide_ref = ' hide' if c.ref == 'U3' else ''
    L.append(f'    (fp_text reference {q(c.ref)} (at 0 {f(top)} {f(float(c.rot))}) (layer F.SilkS){hide_ref}')
    L.append('      (effects (font (size 0.8 0.8) (thickness 0.15)))')
    L.append('    )')
    L.append(f'    (fp_text value {q(c.value)} (at 0 {f(bot)} {f(float(c.rot))}) (layer F.Fab)')
    L.append('      (effects (font (size 0.8 0.8) (thickness 0.12)))')
    L.append('    )')
    for (layer, x1, y1, x2, y2, w) in fp.lines:
        if layer in ('F.SilkS', 'F.Fab', 'F.CrtYd'):
            L.append(f'    (fp_line (start {f(x1)} {f(y1)}) (end {f(x2)} {f(y2)}) (layer {layer}) (width {f(w)}))')
    for p in fp.pads:
        net = c.nets.get(p.num)
        ang = (c.rot + p.rot) % 360
        at = f'(at {f(p.x)} {f(p.y)}{" " + f(float(ang)) if ang else ""})'
        if p.kind == 'thru_hole':
            layers = '*.Cu *.Mask'
            drill = f' (drill {f(p.drill)})'
        else:
            layers = 'F.Cu F.Paste F.Mask'
            drill = ''
        shape = p.shape
        extra = f' (roundrect_rratio {f(p.rratio)})' if shape == 'roundrect' else ''
        netx = f' (net {netidx[net]} {q(net)})' if net else ''
        L.append(f'    (pad {q(p.num)} {p.kind} {shape} {at} (size {f(p.w)} {f(p.h)}){drill} (layers {layers}){extra}{netx})')
    L.append('  )')
    return '\n'.join(L)


SILK_TEXT = [   # (texte, x, y, taille, couche)
    ('GPS', 7.0, 11.9, 0.9, 'F.SilkS'),
    ('1', 6.3, 6.1, 0.7, 'F.SilkS'),
    ('S', 5.75, 18.4, 0.8, 'F.SilkS'), ('-', 5.75, 13.32, 0.8, 'F.SilkS'),
    ('S', 5.75, 27.15, 0.8, 'F.SilkS'), ('-', 5.75, 22.07, 0.8, 'F.SilkS'),
    ('TX', 7.0, 15.86, 0.9, 'F.SilkS'), ('RX', 7.0, 24.61, 0.9, 'F.SilkS'),
    ('TrimBox v1', 11.7, 4.6, 0.9, 'F.SilkS'),
]


def write_pcb(path, comps, nets):
    netidx = {n: i + 1 for i, n in enumerate(nets)}
    g = model.GEO
    out = ['(kicad_pcb (version 20171130) (host pcbnew 5.1.2)', '',
           f'  (general (thickness 1.6) (drawings 4) (tracks {sum(len(t[3]) - 1 for t in model.TRACKS) + len(model.VIAS)}) '
           f'(zones 0) (modules {len(comps)}) (nets {len(nets) + 1}))', '',
           '  (page A4)',
           f'  (title_block (title {q(TITLE)}) (date {DATE}) (rev {REV}) (company TrimBox))', '',
           '  (layers',
           '    (0 F.Cu signal)', '    (31 B.Cu signal)',
           '    (32 B.Adhes user) (33 F.Adhes user) (34 B.Paste user) (35 F.Paste user)',
           '    (36 B.SilkS user) (37 F.SilkS user) (38 B.Mask user) (39 F.Mask user)',
           '    (40 Dwgs.User user) (41 Cmts.User user) (42 Eco1.User user) (43 Eco2.User user)',
           '    (44 Edge.Cuts user) (45 Margin user) (46 B.CrtYd user) (47 F.CrtYd user)',
           '    (48 B.Fab user) (49 F.Fab user)', '  )', '',
           '  (setup',
           f'    (last_trace_width 0.2) (trace_clearance {RULES["clearance"]}) (zone_clearance {RULES["pour_clear"]})',
           '    (zone_45_only no) (trace_min 0.15)',
           f'    (via_size {RULES["via_dia"]}) (via_drill {RULES["via_drill"]}) (via_min_size 0.45) (via_min_drill 0.2)',
           '    (uvia_size 0.3) (uvia_drill 0.1) (uvias_allowed no) (uvia_min_size 0.2) (uvia_min_drill 0.1)',
           '    (edge_width 0.05) (segment_width 0.2) (pcb_text_width 0.15) (pcb_text_size 0.9 0.9)',
           '    (mod_edge_width 0.12) (mod_text_size 0.8 0.8) (mod_text_width 0.15)',
           '    (pad_size 1.7 1.7) (pad_drill 1) (pad_to_mask_clearance 0.05)',
           '    (aux_axis_origin 0 0) (visible_elements FFFFFF7F)',
           '    (pcbplotparams (layerselection 0x010fc_ffffffff) (usegerberextensions false)',
           '      (usegerberattributes false) (usegerberadvancedattributes false) (creategerberjobfile false)',
           '      (excludeedgelayer true) (linewidth 0.1) (plotframeref false) (viasonmask false) (mode 1)',
           '      (useauxorigin false) (hpglpennumber 1) (hpglpenspeed 20) (hpglpendiameter 15.000000)',
           '      (psnegative false) (psa4output false) (plotreference true) (plotvalue false)',
           '      (plotinvisibletext false) (padsonsilk false) (subtractmaskfromsilk true) (outputformat 1)',
           '      (mirror false) (drillshape 0) (scaleselection 1) (outputdirectory "gerber/"))',
           '  )', '', '  (net 0 "")']
    for n in nets:
        out.append(f'  (net {netidx[n]} {q(n)})')
    out.append('')
    out.append('  (net_class Default "Règles JLCPCB 2 couches"')
    out.append(f'    (clearance {RULES["clearance"]}) (trace_width 0.2) (via_dia {RULES["via_dia"]}) (via_drill {RULES["via_drill"]})')
    out.append('    (uvia_dia 0.3) (uvia_drill 0.1)')
    for n in nets:
        out.append(f'    (add_net {q(n)})')
    out.append('  )')
    out.append('')
    for c in comps:
        out.append(footprint_sexpr(c, netidx))
        out.append('')
    # contour (PCB support : rien sous l'antenne de l'ESP32)
    x0, y0, x1, y1 = OX, OY + g['YCUT'], OX + g['W'], OY + g['H']
    for a, b in (((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))):
        out.append(f'  (gr_line (start {f(a[0])} {f(a[1])}) (end {f(b[0])} {f(b[1])}) (layer Edge.Cuts) (width 0.05))')
    # contour de la carte ESP32 posée de l'autre côté (repère sur B.SilkS / Dwgs)
    ex0, ey0, ex1, ey1 = OX, OY, OX + g['W'], OY + g['H']
    for a, b in (((ex0, ey0), (ex1, ey0)), ((ex1, ey0), (ex1, ey1)), ((ex1, ey1), (ex0, ey1)), ((ex0, ey1), (ex0, ey0))):
        out.append(f'  (gr_line (start {f(a[0])} {f(a[1])}) (end {f(b[0])} {f(b[1])}) (layer Dwgs.User) (width 0.1))')
    out.append(f'  (gr_text "ESP32-S3 Mini Nano de ce côté, USB-C en bas" (at {f(OX + g["W"] / 2)} {f(OY + 4.6)}) (layer B.SilkS)')
    out.append('    (effects (font (size 0.8 0.8) (thickness 0.15)) (justify mirror))')
    out.append('  )')
    for (t, x, y, s, layer) in SILK_TEXT:
        out.append(f'  (gr_text {q(t)} (at {f(OX + x)} {f(OY + y)}) (layer {layer})')
        out.append(f'    (effects (font (size {s} {s}) (thickness 0.15)))')
        out.append('  )')
    out.append('')
    for (layer, w, net, pts) in model.TRACKS:
        for a, b in zip(pts, pts[1:]):
            out.append(f'  (segment (start {f(OX + a[0])} {f(OY + a[1])}) (end {f(OX + b[0])} {f(OY + b[1])}) '
                       f'(width {f(w)}) (layer {layer}) (net {netidx[net]}))')
    for (x, y, net) in model.VIAS:
        out.append(f'  (via (at {f(OX + x)} {f(OY + y)}) (size {RULES["via_dia"]}) (drill {RULES["via_drill"]}) '
                   f'(layers F.Cu B.Cu) (net {netidx[net]}))')
    out.append('')
    # plans de masse (à remplir dans KiCad/EasyEDA : « reconstruire les zones »)
    for layer in ('F.Cu', 'B.Cu'):
        m = RULES['edge']
        pts = [(x0 + m, y0 + m), (x1 - m, y0 + m), (x1 - m, y1 - m), (x0 + m, y1 - m)]
        out.append(f'  (zone (net {netidx["GND"]}) (net_name GND) (layer {layer}) (tstamp {ts(7 if layer[0] == "F" else 8)}) (hatch edge 0.508)')
        out.append(f'    (connect_pads (clearance {RULES["pour_clear"]}))')
        out.append('    (min_thickness 0.2)')
        out.append('    (fill (arc_segments 32) (thermal_gap 0.3) (thermal_bridge_width 0.4))')
        out.append('    (polygon')
        out.append('      (pts')
        out.append('        ' + ' '.join(f'(xy {f(a)} {f(b)})' for a, b in pts))
        out.append('      )')
        out.append('    )')
        out.append('  )')
    out.append(')')
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')


def write_pretty(dirpath, comps):
    os.makedirs(dirpath, exist_ok=True)
    seen = set()
    for c in comps:
        if c.fp.name in seen:
            continue
        seen.add(c.fp.name)
        src = os.path.join(model.__file__.rsplit('/', 2)[0], 'lib', c.fp.name + '.kicad_mod')
        if os.path.exists(src):
            txt = open(src, encoding='utf-8').read()
        else:          # empreinte de la carte ESP32 (générée)
            fp = c.fp
            L = [f'(module {fp.name} (layer F.Cu) (tedit {ts(0)})', f'  (descr {q(fp.descr)})', '  (attr virtual)',
                 '  (fp_text reference REF** (at 11.7 2) (layer F.SilkS) hide (effects (font (size 1 1) (thickness 0.15))))',
                 f'  (fp_text value {fp.name} (at 11.7 34) (layer F.Fab) (effects (font (size 1 1) (thickness 0.15))))']
            g = model.GEO
            L.append(f'  (fp_line (start 0 0) (end {g["W"]} 0) (layer F.Fab) (width 0.1))')
            L.append(f'  (fp_line (start {g["W"]} 0) (end {g["W"]} {g["H"]}) (layer F.Fab) (width 0.1))')
            L.append(f'  (fp_line (start {g["W"]} {g["H"]}) (end 0 {g["H"]}) (layer F.Fab) (width 0.1))')
            L.append(f'  (fp_line (start 0 {g["H"]}) (end 0 0) (layer F.Fab) (width 0.1))')
            for p in fp.pads:
                L.append(f'  (pad {q(p.num)} thru_hole circle (at {f(p.x)} {f(p.y)}) (size {f(p.w)} {f(p.h)}) (drill {f(p.drill)}) (layers *.Cu *.Mask))')
            L.append(')')
            txt = '\n'.join(L) + '\n'
        open(os.path.join(dirpath, c.fp.name + '.kicad_mod'), 'w', encoding='utf-8').write(txt)


# ============================================================ symboles
# broche : (nom, numéro, x, y, orientation, type, longueur) — (x, y) = extrémité
# de connexion (mils), orientation = sens de la broche vers le corps (R/L/U/D),
# type KiCad (I O B P W N).
def symbol_defs():
    d = {}
    d['R'] = ('R', ['S -100 30 100 -30 0 1 10 N'],
              [('~', '1', -150, 0, 'R', 'P', 50), ('~', '2', 150, 0, 'L', 'P', 50)], 'résistance')
    d['C'] = ('C', ['P 2 0 1 15 -10 40 -10 -40 N', 'P 2 0 1 15 10 40 10 -40 N'],
              [('~', '1', -150, 0, 'R', 'P', 140), ('~', '2', 150, 0, 'L', 'P', 140)], 'condensateur')
    d['L'] = ('L', ['A -45 0 15 1800 0 0 1 10 N', 'A -15 0 15 1800 0 0 1 10 N', 'A 15 0 15 1800 0 0 1 10 N',
                    'A 45 0 15 1800 0 0 1 10 N'],
              [('~', '1', -150, 0, 'R', 'P', 90), ('~', '2', 150, 0, 'L', 'P', 90)], 'inductance')
    # diode : 1 = cathode (droite), 2 = anode (gauche)
    d['D'] = ('D', ['P 2 0 1 10 40 40 40 -40 N', 'P 4 0 1 10 -40 40 -40 -40 40 0 -40 40 F'],
              [('K', '1', 150, 0, 'L', 'P', 110), ('A', '2', -150, 0, 'R', 'P', 110)], 'diode Schottky')
    d['TPS'] = ('U', ['S -300 250 300 -250 0 1 10 f'],
                [('VIN', '3', -500, 150, 'R', 'W', 200), ('EN', '5', -500, 0, 'R', 'I', 200),
                 ('GND', '1', 0, -450, 'U', 'W', 200),
                 ('VBST', '6', 500, 150, 'L', 'P', 200), ('SW', '2', 500, 0, 'L', 'O', 200),
                 ('VFB', '4', 500, -150, 'L', 'I', 200)], 'abaisseur TPS563201')
    d['IMU'] = ('U', ['S -350 450 350 -450 0 1 10 f'],
                [('VDD', '8', -550, 350, 'R', 'W', 200), ('VDDIO', '5', -550, 250, 'R', 'W', 200),
                 ('CS', '12', -550, 100, 'R', 'I', 200), ('SDO/SA0', '1', -550, -50, 'R', 'B', 200),
                 ('SDx', '2', -550, -150, 'R', 'B', 200), ('SCx', '3', -550, -250, 'R', 'B', 200),
                 ('GND', '6', -100, -650, 'U', 'W', 200), ('GND', '7', 100, -650, 'U', 'W', 200),
                 ('SDA', '14', 550, 350, 'L', 'B', 200), ('SCL', '13', 550, 250, 'L', 'I', 200),
                 ('INT1', '4', 550, 100, 'L', 'O', 200), ('INT2', '9', 550, 0, 'L', 'O', 200),
                 ('NC', '10', 550, -100, 'L', 'N', 200), ('NC', '11', 550, -200, 'L', 'N', 200)], 'LSM6DS3TR-C')
    d['CONN5'] = ('J', ['S -100 300 50 -300 0 1 10 f'],
                  [(str(i), str(i), 250, 200 - (i - 1) * 100, 'L', 'P', 200) for i in range(1, 6)] +
                  [('MP', 'MP', 0, -500, 'U', 'P', 200)], 'JST SH 5 broches')
    d['SERVO'] = ('J', ['S -100 150 50 -150 0 1 10 f'],
                  [('S', '1', 250, 100, 'L', 'P', 200), ('+', '2', 250, 0, 'L', 'P', 200),
                   ('-', '3', 250, -100, 'L', 'P', 200)], 'picot servo 1×3')
    d['CONN4'] = ('J', ['S -100 250 50 -250 0 1 10 f'],
                  [(str(i), str(i), 250, 150 - (i - 1) * 100, 'L', 'P', 200) for i in range(1, 5)] +
                  [('MP', 'MP', 0, -450, 'U', 'P', 200)], 'JST SH 4 broches')
    # carte ESP32 : broches utiles (côté droit vue de dessous) à gauche du symbole,
    # trous de tenue mécanique (côté gauche) à droite du symbole
    pins = []
    y = 1000
    for k in range(1, 12):
        for nm in model.MAPPING['right'][k]:
            if any(p[1] == nm for p in pins):
                continue
            pins.append((nm, nm, -600, y, 'R', 'W' if nm in ('VIN', '3V3', 'GND') else 'B', 200))
            y -= 100
    y = 1000
    for k, pair in model.MAPPING['left'].items():
        for nm in pair:
            pins.append((nm, nm, 600, y, 'L', 'N', 200))
            y -= 100
    d['ESP'] = ('U', ['S -400 1100 400 -1000 0 1 10 f'], pins, 'carte ESP32-S3 Mini Nano')
    return d


SYMNAME = {'R': 'R', 'C': 'C', 'L': 'L', 'D': 'D_Schottky', 'TPS': 'TPS563201', 'IMU': 'LSM6DS3TR-C',
           'CONN5': 'JST_SH_5', 'SERVO': 'Servo_1x03', 'CONN4': 'JST_SH_4', 'ESP': 'ESP32-S3_MiniNano'}


def write_lib(path, cache=False):
    defs = symbol_defs()
    out = ['EESchema-LIBRARY Version 2.4', '#encoding utf-8']
    for key, (prefix, draw, pins, descr) in defs.items():
        name = ('TrimBox_' if cache else '') + SYMNAME[key]
        two = key in ('R', 'C', 'L', 'D')
        out += ['#', f'# {name}', '#',
                f'DEF {name} {prefix} 0 {"0" if two else "40"} {"N" if two else "Y"} {"N" if two else "Y"} 1 F N',
                f'F0 "{prefix}" 0 {100 if two else 50} 50 H V C CNN',
                f'F1 "{SYMNAME[key]}" 0 {-100 if two else -50} 50 H V C CNN',
                'F2 "" 0 0 50 H I C CNN', 'F3 "" 0 0 50 H I C CNN', 'DRAW']
        out += draw
        for (nm, num, x, y, o, t, ln) in pins:
            out.append(f'X {nm} {num} {x} {y} {ln} {o} 50 50 1 1 {t}{" N" if t == "N" else ""}')
        out += ['ENDDRAW', 'ENDDEF']
    out += ['#', '#End Library']
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return defs


# ============================================================ schéma
# Placement des symboles (mils, feuille A4 paysage 11693 × 8268)
SCH_POS = {
    'J1': (1300, 1500), 'J3': (1300, 2300), 'J2': (1300, 3300),
    'U1': (4300, 1800), 'C1': (3100, 2400), 'C2': (3100, 2800), 'C3': (5500, 1350),
    'L1': (6000, 1800), 'R1': (6100, 2450), 'R2': (6100, 2850), 'C4': (7000, 1450), 'C5': (7000, 1850),
    'D1': (7300, 2350), 'C8': (7300, 2850),
    'U2': (4300, 5300), 'C6': (2800, 4700), 'C7': (2800, 5100), 'R3': (5800, 4600), 'R4': (5800, 5000),
    'U3': (9600, 4600),
}
COMP_TS = {}


def sch_pins(c, defs):
    """[(num, nom, x, y, orientation)] des extrémités de broches, en mils schéma."""
    prefix, draw, pins, _ = defs[c.sym]
    X, Y = SCH_POS[c.ref]
    return [(num, nm, X + x, Y - y, o) for (nm, num, x, y, o, t, ln) in pins]


def write_sch(path, comps, project):
    defs = symbol_defs()
    for i, c in enumerate(comps):
        COMP_TS[c.ref] = i + 1
    out = ['EESchema Schematic File Version 4', 'EELAYER 30 0', 'EELAYER END',
           '$Descr A4 11693 8268', 'encoding utf-8', 'Sheet 1 1',
           f'Title "{TITLE}"', f'Date "{DATE}"', f'Rev "{REV}"', 'Comp "TrimBox"',
           'Comment1 "ESP32-S3 Mini Nano (dessus) + LSM6DS3TR-C + régulateur 5 V TPS563201 + JST SH GPS et récepteur"',
           'Comment2 "Brochage ESP32 : hypothèse de sérigraphie A (à confirmer, voir LISEZMOI)"',
           'Comment3 ""', 'Comment4 ""', '$EndDescr']
    labels, wires, noconns = [], [], []
    for c in comps:
        X, Y = SCH_POS[c.ref]
        out += ['$Comp', f'L TrimBox:{SYMNAME[c.sym]} {c.ref}', f'U 1 1 {ts(COMP_TS[c.ref])}', f'P {X} {Y}']
        two = c.sym in ('R', 'C', 'L', 'D')
        ry = -100 if two else -int(max(p[3] for p in defs[c.sym][2])) - 100
        vy = 100 if two else int(-min(p[3] for p in defs[c.sym][2])) + 100
        fpname = 'TrimBox:' + c.fp.name
        out += [f'F 0 "{c.ref}" H {X} {Y + ry} 50  0000 C CNN',
                f'F 1 "{c.value}" H {X} {Y + vy} 50  0000 C CNN',
                f'F 2 "{fpname}" H {X} {Y} 50  0001 C CNN',
                'F 3 "" H {X} {Y} 50  0001 C CNN'.replace('{X}', str(X)).replace('{Y}', str(Y))]
        n = 4
        for fld, val in (('LCSC', c.lcsc), ('MPN', c.mpn)):
            if val:
                out.append(f'F {n} "{val}" H {X} {Y} 50  0001 C CNN "{fld}"'); n += 1
        out += [f'\t1    {X} {Y}', '\t1    0    0    -1  ', '$EndComp']
        # étiquettes de réseau au bout d'un court fil sur chaque broche
        for (num, nm, px, py, o) in sch_pins(c, defs):
            net = c.nets.get(num)
            dx, dy = {'R': (-1, 0), 'L': (1, 0), 'U': (0, 1), 'D': (0, -1)}[o]
            ex, ey = px + dx * 100, py + dy * 100
            if net:
                wires.append((px, py, ex, ey))
                spin = {(-1, 0): 2, (1, 0): 0, (0, 1): 3, (0, -1): 1}[(dx, dy)]
                labels.append((ex, ey, spin, net))
            else:
                noconns.append((px, py))
    for (x1, y1, x2, y2) in wires:
        out += ['Wire Wire Line', f'\t{x1} {y1} {x2} {y2}']
    for (x, y, spin, net) in labels:
        out += [f'Text Label {x} {y} {spin}    50   ~ 0', net]
    for (x, y) in noconns:
        out.append(f'NoConn ~ {x} {y}')
    notes = [
        (700, 7100, 'PCB support à la taille de la carte ESP32 : composants sur cette face, ESP32 de l\'autre côté (picots 2,54 mm).'),
        (700, 7250, 'J1 / J3 : picots coudés pour rallonges servo (S, +, -) : J1 sortie 2 du récepteur (TX -> IO16), J3 sortie 3 (RX <- IO15), + = BEC.'),
        (700, 7400, 'J2 : 1 masse, 2 +5 V, 3 depuis TX du GPS (IO18), 4 vers RX du GPS (IO17) — ordre G V T R du HGLRC M100 Mini.'),
        (700, 7550, 'Régulateur : Vout = 0,768 × (1 + R1/R2) = 5,07 V ; D1 empêche l\'USB de la carte d\'alimenter le BEC.'),
        (700, 7700, 'IMU : adresse I2C 0x6A (SA0 à la masse), CS au 3,3 V (mode I2C), INT1 sur IO7.'),
    ]
    for (x, y, t) in notes:
        out += [f'Text Notes {x} {y} 0    50   ~ 0', t]
    out.append('$EndSCHEMATC')
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n')
    return labels, wires, noconns


def write_project(dirpath, name, comps, nets):
    os.makedirs(dirpath, exist_ok=True)
    write_lib(os.path.join(dirpath, 'TrimBox.lib'))
    write_lib(os.path.join(dirpath, f'{name}-cache.lib'), cache=True)
    write_sch(os.path.join(dirpath, f'{name}.sch'), comps, name)
    write_pcb(os.path.join(dirpath, f'{name}.kicad_pcb'), comps, nets)
    write_pretty(os.path.join(dirpath, 'TrimBox.pretty'), comps)
    open(os.path.join(dirpath, 'sym-lib-table'), 'w').write(
        '(sym_lib_table\n  (lib (name TrimBox)(type Legacy)(uri ${KIPRJMOD}/TrimBox.lib)(options "")(descr ""))\n)\n')
    open(os.path.join(dirpath, 'fp-lib-table'), 'w').write(
        '(fp_lib_table\n  (lib (name TrimBox)(type KiCad)(uri ${KIPRJMOD}/TrimBox.pretty)(options "")(descr ""))\n)\n')
    open(os.path.join(dirpath, f'{name}.pro'), 'w').write(
        'update=17/10/2026\nversion=1\nlast_client=kicad\n[general]\nversion=1\nRootSch=\nBoardNm=\n'
        '[pcbnew]\nversion=1\nLastNetListRead=\nUseCmpFile=1\nPadDrill=1\nPadDrillOvalY=1\nPadSizeH=1.7\nPadSizeV=1.7\n'
        'PcbTextSizeV=0.9\nPcbTextSizeH=0.9\nPcbTextThickness=0.15\nModuleTextSizeV=0.8\nModuleTextSizeH=0.8\n'
        'ModuleTextSizeThickness=0.15\nSolderMaskClearance=0.05\nSolderMaskMinWidth=0\nDrawSegmentWidth=0.2\n'
        'BoardOutlineThickness=0.05\nModuleOutlineThickness=0.12\n[cvpcb]\nversion=1\nNetIExt=net\n'
        '[eeschema]\nversion=1\nLibDir=\n[eeschema/libraries]\n[schematic_editor]\nversion=1\nPageLayoutDescrFile=\n'
        'PlotDirectoryName=\nSubpartIdSeparator=0\nSubpartFirstId=65\nNetFmtName=\nSpiceAjustPassiveValues=0\n'
        'LabSize=50\nERC_TestSimilarLabels=1\n')
