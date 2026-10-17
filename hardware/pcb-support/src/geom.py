# Géométrie du cuivre : formes (pastilles, pistes, vias) et rastérisation.
import math

import numpy as np
from PIL import Image, ImageDraw

import model

RULES = dict(
    clearance=0.15,      # cuivre ↔ cuivre (JLCPCB : 0,1 mm possible)
    edge=0.30,           # cuivre ↔ bord de carte
    via_drill=0.30, via_dia=0.60,
    pour_clear=0.25,     # plan de masse ↔ autres réseaux
    min_width=0.15,
)

WIDTH = {   # largeur de piste par réseau (mm)
    'SW': 0.6, 'V5_BUCK': 0.6, 'VBEC': 0.5, '+5V': 0.5, 'GND': 0.4,
    'BST': 0.25, 'FB': 0.2, '+3V3': 0.2,
    'SDA': 0.15, 'SCL': 0.15, 'IMU_INT1': 0.15,
    'ELRS_TX': 0.2, 'ELRS_RX': 0.2, 'GPS_TX': 0.2, 'GPS_RX': 0.2,
}


class Shape:
    """kind : 'poly' (points), 'circle' (x, y, r), 'seg' (x1, y1, x2, y2, w)."""
    def __init__(self, net, layers, kind, data, src=''):
        self.net, self.layers, self.kind, self.data, self.src = net, set(layers), kind, data, src

    def bbox(self):
        if self.kind == 'poly':
            xs = [p[0] for p in self.data]; ys = [p[1] for p in self.data]
            return min(xs), min(ys), max(xs), max(ys)
        if self.kind == 'circle':
            x, y, r = self.data
            return x - r, y - r, x + r, y + r
        x1, y1, x2, y2, w = self.data
        return min(x1, x2) - w / 2, min(y1, y2) - w / 2, max(x1, x2) + w / 2, max(y1, y2) + w / 2


def pad_shape(c, p, x, y, ang, net):
    layers = ('F', 'B') if p.kind == 'thru_hole' else ('F',)
    if p.shape == 'circle':
        return Shape(net, layers, 'circle', (x, y, p.w / 2), f'{c.ref}.{p.num}')
    w, h = p.w / 2, p.h / 2
    a = math.radians(ang)
    pts = [(x + u * math.cos(a) + v * math.sin(a), y - u * math.sin(a) + v * math.cos(a))
           for u, v in ((-w, -h), (w, -h), (w, h), (-w, h))]
    return Shape(net, layers, 'poly', pts, f'{c.ref}.{p.num}')


def pad_shapes(comps):
    out = []
    for c in comps:
        for p, x, y, ang in c.pads_abs():
            out.append(pad_shape(c, p, x, y, ang, c.nets.get(p.num)))
    return out


def board_poly():
    g = model.GEO
    return [(0, g['YCUT']), (g['W'], g['YCUT']), (g['W'], g['H']), (0, g['H'])]


class Raster:
    """Grille régulière couvrant la carte, résolution `res` mm."""
    def __init__(self, res, margin=0.0):
        g = model.GEO
        self.res = res
        self.x0, self.y0 = -margin, g['YCUT'] - margin
        self.nx = int(math.ceil((g['W'] + 2 * margin) / res)) + 1
        self.ny = int(math.ceil((g['H'] - g['YCUT'] + 2 * margin) / res)) + 1

    def px(self, x, y):
        return ((x - self.x0) / self.res, (y - self.y0) / self.res)

    def xy(self, i, j):
        return (self.x0 + i * self.res, self.y0 + j * self.res)

    def blank(self):
        return Image.new('1', (self.nx, self.ny), 0)

    def draw(self, img, s, grow=0.0):
        d = ImageDraw.Draw(img)
        r = self.res
        if s.kind == 'circle':
            x, y, rad = s.data
            rad += grow
            a, b = self.px(x - rad, y - rad); e, f = self.px(x + rad, y + rad)
            d.ellipse([a, b, e, f], fill=1)
        elif s.kind == 'seg':
            x1, y1, x2, y2, w = s.data
            w += 2 * grow
            p1, p2 = self.px(x1, y1), self.px(x2, y2)
            d.line([p1, p2], fill=1, width=max(1, int(round(w / r))))
            for (xx, yy) in ((x1, y1), (x2, y2)):
                a, b = self.px(xx - w / 2, yy - w / 2); e, f = self.px(xx + w / 2, yy + w / 2)
                d.ellipse([a, b, e, f], fill=1)
        else:
            pts = s.data
            if grow:
                # agrandissement approximatif d'un rectangle : décalage des sommets
                cx = sum(p[0] for p in pts) / len(pts); cy = sum(p[1] for p in pts) / len(pts)
                pts = []
                for (x, y) in s.data:
                    dx, dy = x - cx, y - cy
                    n = math.hypot(dx, dy) or 1
                    pts.append((x + dx / n * grow * 1.4142, y + dy / n * grow * 1.4142))
            d.polygon([self.px(*p) for p in pts], fill=1)

    def mask(self, shapes, layer, grow=0.0):
        img = self.blank()
        for s in shapes:
            if layer in s.layers:
                self.draw(img, s, grow)
        return np.array(img, dtype=bool)

    def board_mask(self, inset=0.0):
        img = self.blank()
        g = model.GEO
        a, b = self.px(inset, g['YCUT'] + inset); e, f = self.px(g['W'] - inset, g['H'] - inset)
        ImageDraw.Draw(img).rectangle([a, b, e, f], fill=1)
        return np.array(img, dtype=bool)
