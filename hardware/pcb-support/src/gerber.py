# Fichiers de fabrication JLCPCB : Gerber RS-274X, perçages Excellon,
# nomenclature (BOM) et placement (CPL) pour l'assemblage CMS.
import csv
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFont

import model
from geom import RULES

FONT = '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'


def G(x, y):
    """Repère Gerber : origine au coin bas-gauche du PCB support, y vers le haut."""
    return x, model.GEO['H'] - y


def fmt(v):
    return str(int(round(v * 1e6)))


class Gerber:
    def __init__(self, name, func):
        self.name, self.func = name, func
        self.ap = {}
        self.body = []
        self.cur = None

    def aperture(self, kind, *dims):
        key = (kind,) + tuple(round(d, 4) for d in dims)
        if key not in self.ap:
            self.ap[key] = 10 + len(self.ap)
        return self.ap[key]

    def use(self, code):
        if self.cur != code:
            self.body.append(f'D{code}*'); self.cur = code

    def flash_circle(self, x, y, d):
        self.use(self.aperture('C', d))
        gx, gy = G(x, y)
        self.body.append(f'X{fmt(gx)}Y{fmt(gy)}D03*')

    def line(self, pts, w):
        self.use(self.aperture('C', w))
        for k, (x, y) in enumerate(pts):
            gx, gy = G(x, y)
            self.body.append(f'X{fmt(gx)}Y{fmt(gy)}D0{2 if k == 0 else 1}*')

    def region(self, pts):
        self.body.append('G36*')
        for k, (x, y) in enumerate(pts + [pts[0]]):
            gx, gy = G(x, y)
            self.body.append(f'X{fmt(gx)}Y{fmt(gy)}D0{2 if k == 0 else 1}*')
        self.body.append('G37*')

    def polarity(self, dark):
        self.body.append('%LPD*%' if dark else '%LPC*%')

    def write(self, path):
        h = ['G04 TrimBox - PCB support ESP32-S3 Mini Nano*',
             f'%TF.FileFunction,{self.func}*%',
             '%FSLAX46Y46*%', '%MOMM*%', '%LPD*%']
        for key, code in sorted(self.ap.items(), key=lambda kv: kv[1]):
            if key[0] == 'C':
                h.append(f'%ADD{code}C,{key[1]:.6f}*%')
        open(path, 'w').write('\n'.join(h + ['G01*'] + self.body + ['M02*']) + '\n')


def pad_outline(p, x, y, ang, grow=0.0):
    """Contour d'une pastille rectangulaire (coins arrondis si roundrect)."""
    w, h = p.w / 2 + grow, p.h / 2 + grow
    r = 0.0
    if p.shape == 'roundrect':
        r = min(p.w, p.h) * p.rratio + grow
    pts = []
    if r > 0.001:
        for (cx, cy, a0) in ((w - r, h - r, 0), (-w + r, h - r, 90), (-w + r, -h + r, 180), (w - r, -h + r, 270)):
            for k in range(5):
                a = math.radians(a0 + k * 22.5)
                pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    else:
        pts = [(w, h), (-w, h), (-w, -h), (w, -h)]
    a = math.radians(ang)
    return [(x + u * math.cos(a) + v * math.sin(a), y - u * math.sin(a) + v * math.cos(a)) for u, v in pts]


def mask_rects(mask, R):
    """Masque raster → rectangles (fusion verticale des segments identiques)."""
    rects, open_runs = [], {}
    ny = mask.shape[0]
    for j in range(ny + 1):
        runs = set()
        if j < ny:
            row = mask[j]
            if row.any():
                d = np.diff(np.concatenate(([0], row.view(np.int8), [0])))
                st = np.where(d == 1)[0]; en = np.where(d == -1)[0]
                runs = set(zip(st.tolist(), en.tolist()))
        for run in list(open_runs):
            if run not in runs:
                j0 = open_runs.pop(run)
                rects.append((run[0], j0, run[1], j))
        for run in runs:
            if run not in open_runs:
                open_runs[run] = j
    out = []
    for (i0, j0, i1, j1) in rects:
        x0, y0 = R.xy(i0 - 0.5, j0 - 0.5); x1, y1 = R.xy(i1 - 0.5, j1 - 0.5)
        out.append([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])
    return out


def text_mask(R, items):
    """Textes de sérigraphie rastérisés (DejaVu Sans Bold)."""
    img = Image.new('1', (R.nx, R.ny), 0)
    d = ImageDraw.Draw(img)
    for (t, x, y, size, layer, mirror) in items:
        px = int(round(size / R.res * 1.35))
        font = ImageFont.truetype(FONT, px)
        bb = d.textbbox((0, 0), t, font=font)
        tw, th = bb[2] - bb[0], bb[3] - bb[1]
        tmp = Image.new('1', (tw + 4, th + 4), 0)
        ImageDraw.Draw(tmp).text((2 - bb[0], 2 - bb[1]), t, font=font, fill=1)
        if mirror:
            tmp = tmp.transpose(Image.FLIP_LEFT_RIGHT)
        cx, cy = R.px(x, y)
        img.paste(tmp, (int(cx - tmp.width / 2), int(cy - tmp.height / 2)), tmp)
    return np.array(img, bool)


def write_all(outdir, comps, pours, R_pour, silk_texts):
    os.makedirs(outdir, exist_ok=True)
    g = model.GEO
    name = 'trimbox-support'
    cuF, cuB = Gerber('F_Cu', 'Copper,L1,Top'), Gerber('B_Cu', 'Copper,L2,Bot')
    mF, mB = Gerber('F_Mask', 'Soldermask,Top'), Gerber('B_Mask', 'Soldermask,Bot')
    pF = Gerber('F_Paste', 'Paste,Top')
    sF, sB = Gerber('F_Silkscreen', 'Legend,Top'), Gerber('B_Silkscreen', 'Legend,Bot')
    edge = Gerber('Edge_Cuts', 'Profile,NP')
    # 1) plans de masse (rectangles), puis tout le reste par-dessus
    for L, gb in (('F', cuF), ('B', cuB)):
        for rc in mask_rects(pours[L], R_pour):
            gb.region(rc)
    # 2) pastilles
    for c in comps:
        for p, x, y, ang in c.pads_abs():
            if p.kind == 'thru_hole':
                for gb in (cuF, cuB):
                    gb.flash_circle(x, y, p.w)
                for gb in (mF, mB):
                    gb.flash_circle(x, y, p.w + 0.1)
            else:
                cuF.region(pad_outline(p, x, y, ang))
                mF.region(pad_outline(p, x, y, ang, 0.05))
                if c.ref != 'U3':
                    pF.region(pad_outline(p, x, y, ang))
    # 3) pistes et vias (vias recouverts de vernis)
    for (layer, w, net, pts) in model.TRACKS:
        (cuF if layer == 'F.Cu' else cuB).line(pts, w)
    for (x, y, net) in model.VIAS:
        cuF.flash_circle(x, y, RULES['via_dia']); cuB.flash_circle(x, y, RULES['via_dia'])
    # 4) sérigraphie : contours des empreintes + textes
    for c in comps:
        for (layer, x1, y1, x2, y2, w) in c.fp.lines:
            if layer == 'F.SilkS':
                from footprints import xform
                a = xform(c.x, c.y, c.rot, x1, y1); b = xform(c.x, c.y, c.rot, x2, y2)
                sF.line([a, b], max(w, 0.15))
    from geom import Raster
    Rs = Raster(0.01, margin=0.5)
    tm_F = text_mask(Rs, [t for t in silk_texts if t[4] == 'F.SilkS'])
    tm_B = text_mask(Rs, [t for t in silk_texts if t[4] == 'B.SilkS'])
    for rc in mask_rects(tm_F, Rs):
        sF.region(rc)
    for rc in mask_rects(tm_B, Rs):
        sB.region(rc)
    # 5) contour
    edge.line([(0, g['YCUT']), (g['W'], g['YCUT']), (g['W'], g['H']), (0, g['H']), (0, g['YCUT'])], 0.05)
    files = {}
    for gb, ext in ((cuF, 'GTL'), (cuB, 'GBL'), (mF, 'GTS'), (mB, 'GBS'), (pF, 'GTP'), (sF, 'GTO'), (sB, 'GBO'),
                    (edge, 'GKO')):
        fn = f'{name}-{gb.name}.{ext}'
        gb.write(os.path.join(outdir, fn)); files[gb.name] = fn
    # 6) perçages (métallisés)
    tools = {}
    for (x, y, n) in model.VIAS:
        tools.setdefault(RULES['via_drill'], []).append((x, y))
    for c in comps:
        for p, x, y, ang in c.pads_abs():
            if p.drill:
                tools.setdefault(p.drill, []).append((x, y))
    drl = ['M48', '; TrimBox - PCB support, percages metallises', 'FMAT,2', 'METRIC,TZ']
    for k, d in enumerate(sorted(tools), 1):
        drl.append(f'T{k}C{d:.3f}')
    drl += ['%', 'G90', 'G05']
    for k, d in enumerate(sorted(tools), 1):
        drl.append(f'T{k}')
        for (x, y) in tools[d]:
            gx, gy = G(x, y)
            drl.append(f'X{gx:.3f}Y{gy:.3f}')
    drl += ['T0', 'M30']
    fn = f'{name}-PTH.drl'
    open(os.path.join(outdir, fn), 'w').write('\n'.join(drl) + '\n')
    files['PTH'] = fn
    return files, {d: len(v) for d, v in tools.items()}


def write_bom_cpl(outdir, comps):
    os.makedirs(outdir, exist_ok=True)
    groups = {}
    for c in comps:
        if c.dnp:
            continue
        key = (c.value, c.fp.name, c.lcsc, c.mpn)
        groups.setdefault(key, []).append(c.ref)
    with open(os.path.join(outdir, 'trimbox-support-BOM-JLCPCB.csv'), 'w', newline='', encoding='utf-8') as fh:
        w = csv.writer(fh)
        w.writerow(['Comment', 'Designator', 'Footprint', 'LCSC Part #', 'Manufacturer Part'])
        for (val, fp, lcsc, mpn), refs in sorted(groups.items(), key=lambda kv: kv[1][0]):
            w.writerow([val, ','.join(sorted(refs, key=lambda r: (r[0], int(r[1:])))), fp, lcsc, mpn])
    with open(os.path.join(outdir, 'trimbox-support-CPL-JLCPCB.csv'), 'w', newline='', encoding='utf-8') as fh:
        w = csv.writer(fh)
        w.writerow(['Designator', 'Mid X', 'Mid Y', 'Layer', 'Rotation'])
        for c in comps:
            if c.dnp:
                continue
            gx, gy = G(c.x, c.y)
            w.writerow([c.ref, f'{gx:.3f}mm', f'{gy:.3f}mm', 'Top', f'{c.rot % 360:.0f}'])
    return groups
