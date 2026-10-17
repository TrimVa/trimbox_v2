# Rendus PNG de contrôle (placement, routage, couches).
import math
import sys

from PIL import Image, ImageDraw, ImageFont

import model
from footprints import xform

S = 40           # px par mm
M = 3.0          # marge autour de la carte (mm)
FONT = None
for f in ('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf',):
    try:
        FONT = ImageFont.truetype(f, 11); FONT_S = ImageFont.truetype(f, 9); FONT_B = ImageFont.truetype(f, 14)
    except OSError:
        pass

NETCOL = {}
PALETTE = ['#e6194b', '#3cb44b', '#4363d8', '#f58231', '#911eb4', '#42d4f4', '#f032e6', '#bfef45',
           '#469990', '#9A6324', '#800000', '#808000', '#000075', '#e6beff', '#aaffc3', '#ffd8b1']


def col(net):
    if net is None:
        return '#9a9a9a'
    if net == 'GND':
        return '#555555'
    if net not in NETCOL:
        NETCOL[net] = PALETTE[len(NETCOL) % len(PALETTE)]
    return NETCOL[net]


def P(x, y):
    return ((x + M) * S, (y + M) * S)


def pad_poly(p, x, y, ang):
    """Contour (liste de points carte) d'une pastille rectangulaire/arrondie."""
    w, h = p.w / 2, p.h / 2
    pts = [(-w, -h), (w, -h), (w, h), (-w, h)]
    a = math.radians(ang)
    return [(x + u * math.cos(a) + v * math.sin(a), y - u * math.sin(a) + v * math.cos(a)) for u, v in pts]


def draw_board(d, g):
    x0, y0, x1, y1 = 0, g['YCUT'], g['W'], g['H']
    d.rectangle([P(0, 0), P(g['W'], g['H'])], outline='#bbbbbb', width=1)            # carte ESP32
    d.rectangle([P(x0, y0), P(x1, y1)], outline='#d4a000', width=3)                    # PCB support
    d.text(P(0.2, 0.3), 'antenne ESP32 (pas de PCB dessous)', fill='#999999', font=FONT_S)


def render(path, ratsnest=True, tracks=True, labels=True):
    comps = model.build()
    g = model.GEO
    img = Image.new('RGB', (int((g['W'] + 2 * M) * S), int((g['H'] + 2 * M) * S)), '#fbfaf7')
    d = ImageDraw.Draw(img)
    draw_board(d, g)
    padpos = {}
    for c in comps:
        # contour de courtyard
        if c.fp.crtyd:
            a, b, e, f = c.fp.crtyd
            pts = [xform(c.x, c.y, c.rot, u, v) for u, v in ((a, b), (e, b), (e, f), (a, f))]
            d.polygon([P(*q) for q in pts], outline='#c8c8ff')
        for p, x, y, ang in c.pads_abs():
            net = c.nets.get(p.num)
            if p.shape == 'circle':
                r = p.w / 2
                d.ellipse([P(x - r, y - r), P(x + r, y + r)], fill=col(net), outline='#222')
                rr = p.drill / 2
                d.ellipse([P(x - rr, y - rr), P(x + rr, y + rr)], fill='#fbfaf7')
            else:
                d.polygon([P(*q) for q in pad_poly(p, x, y, ang)], fill=col(net), outline='#222')
            if net:
                padpos.setdefault(net, []).append((x, y))
            if labels and c.ref == 'U3':
                d.text(P(x - 0.8, y + 0.9), p.num, fill='#333', font=FONT_S)
        if labels and c.ref != 'U3':
            d.text(P(c.x - 0.6, c.y - 0.3), c.ref, fill='#000', font=FONT_B)
    if tracks:
        for (layer, w, net, pts) in model.TRACKS:
            cl = col(net)
            for (a, b) in zip(pts, pts[1:]):
                d.line([P(*a), P(*b)], fill=cl if layer == 'F.Cu' else '#4a90e2', width=max(1, int(w * S)))
                for q in (a, b):
                    r = w / 2
                    d.ellipse([P(q[0] - r, q[1] - r), P(q[0] + r, q[1] + r)], fill=cl if layer == 'F.Cu' else '#4a90e2')
        for (x, y, net) in model.VIAS:
            d.ellipse([P(x - 0.3, y - 0.3), P(x + 0.3, y + 0.3)], fill='#e0c000', outline='#222')
            d.ellipse([P(x - 0.15, y - 0.15), P(x + 0.15, y + 0.15)], fill='#fbfaf7')
    if ratsnest:
        for net, pts in padpos.items():
            if net == 'GND':
                continue
            # arbre couvrant minimal (Prim)
            done, rest = [pts[0]], pts[1:]
            while rest:
                best = min(((a, b) for a in done for b in rest), key=lambda ab: math.dist(*ab))
                d.line([P(*best[0]), P(*best[1])], fill=col(net), width=1)
                done.append(best[1]); rest.remove(best[1])
    # légende des réseaux
    yy = 4
    for net in model.nets():
        d.rectangle([P(g['W'] + 0.3, yy - 0.3), P(g['W'] + 0.9, yy + 0.3)], fill=col(net))
        yy += 0
    img.save(path)
    return padpos


if __name__ == '__main__':
    render(sys.argv[1] if len(sys.argv) > 1 else 'placement.png')
