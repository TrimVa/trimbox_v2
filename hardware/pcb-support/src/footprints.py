# Empreintes : lues dans les fichiers officiels KiCad (lib/*.kicad_mod), plus
# l'empreinte de la carte ESP32-S3 (trous traversants), construite ici.
import math
import os

from sexp import parse, find, find_all

LIB = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'lib')


class Pad:
    def __init__(self, num, kind, shape, x, y, w, h, rot=0.0, drill=0.0, rratio=0.0, layers='F'):
        self.num, self.kind, self.shape = num, kind, shape
        self.x, self.y, self.w, self.h, self.rot = x, y, w, h, rot
        self.drill, self.rratio = drill, rratio

    def __repr__(self):
        return f'Pad({self.num} {self.shape} {self.x},{self.y} {self.w}x{self.h})'


class Footprint:
    def __init__(self, name, descr=''):
        self.name, self.descr = name, descr
        self.pads = []
        self.lines = []          # (layer, x1, y1, x2, y2, width)
        self.crtyd = None        # (xmin, ymin, xmax, ymax)
        self.attr = 'smd'


def _f(v):
    return float(v)


def load(name):
    """Charge lib/<name>.kicad_mod (format KiCad 5)."""
    t = parse(open(os.path.join(LIB, name + '.kicad_mod'), encoding='utf-8').read())
    fp = Footprint(name)
    d = find(t, 'descr')
    fp.descr = d[1] if d else ''
    a = find(t, 'attr')
    fp.attr = a[1] if a else 'virtual'
    xs, ys = [], []
    for ln in find_all(t, 'fp_line'):
        s, e, layer, w = find(ln, 'start'), find(ln, 'end'), find(ln, 'layer')[1], find(ln, 'width')
        x1, y1, x2, y2 = _f(s[1]), _f(s[2]), _f(e[1]), _f(e[2])
        if layer.endswith('CrtYd'):
            xs += [x1, x2]; ys += [y1, y2]
        fp.lines.append((layer, x1, y1, x2, y2, _f(w[1]) if w else 0.1))
    if xs:
        fp.crtyd = (min(xs), min(ys), max(xs), max(ys))
    for p in find_all(t, 'pad'):
        num, kind, shape = p[1], p[2], p[3]
        at = find(p, 'at')
        x, y = _f(at[1]), _f(at[2])
        rot = _f(at[3]) if len(at) > 3 else 0.0
        size = find(p, 'size')
        w, h = _f(size[1]), _f(size[2])
        rr = find(p, 'roundrect_rratio')
        if shape == 'custom':
            # pastille d'angle chanfreinée (LGA) → rectangle englobant du polygone
            prim = find(p, 'primitives')
            poly = find(prim, 'gr_poly')
            pts = [(_f(q[1]), _f(q[2])) for q in find(poly, 'pts')[1:]]
            px = [q[0] for q in pts]; py = [q[1] for q in pts]
            w, h = max(px) - min(px), max(py) - min(py)
            x += (max(px) + min(px)) / 2; y += (max(py) + min(py)) / 2
            shape = 'rect'
        dr = find(p, 'drill')
        fp.pads.append(Pad(num, kind, shape, x, y, w, h, rot,
                           _f(dr[1]) if dr else 0.0, _f(rr[1]) if rr else 0.0))
    return fp


# ---------------------------------------------------------------- carte ESP32
# Carte NiceMCU « ESP32-S3_MINI_Nano » vue DE DESSOUS (face des composants du
# PCB support, sérigraphie « IOxx » de la carte lisible dans ce sens).
# Origine : coin haut-gauche du contour de la carte ESP32 (antenne en haut,
# USB-C en bas) ; x vers la droite, y vers le bas, en mm.
def esp_header(geo, mapping):
    fp = Footprint('TrimBox_ESP32-S3_MiniNano_Carrier',
                   'Carte NiceMCU ESP32-S3 Mini Nano N16R8 vue de dessous, picots 2,54 mm')
    fp.attr = 'virtual'
    for (x, y, name) in esp_holes(geo, mapping):
        fp.pads.append(Pad(name, 'thru_hole', 'circle', x, y, geo['PAD'], geo['PAD'], 0, geo['DRILL']))
    return fp


def esp_holes(geo, mapping):
    """(x, y, nom) de chaque trou utilisé, d'après la correspondance de sérigraphie."""
    out = []
    for k, (inner, outer) in mapping['right'].items():
        y = geo['Y0'] + (k - 1) * geo['PITCH']
        out.append((geo['XR_IN'], y, inner))
        out.append((geo['XR_OUT'], y, outer))
    for k, (outer, inner) in mapping['left'].items():
        y = geo['Y0'] + (k - 1) * geo['PITCH']
        out.append((geo['XL_OUT'], y, outer))
        out.append((geo['XL_IN'], y, inner))
    return out


def xform(x0, y0, rot, lx, ly):
    """Coordonnées locales → carte (rotation KiCad : sens trigonométrique à l'écran, y vers le bas)."""
    a = math.radians(rot)
    return (x0 + lx * math.cos(a) + ly * math.sin(a),
            y0 - lx * math.sin(a) + ly * math.cos(a))
