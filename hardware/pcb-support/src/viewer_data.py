# Données du visualiseur 2D/3D : textures des deux faces (cuivre, vernis,
# sérigraphie, perçages) et volumes des composants, tirés du même modèle que
# les Gerber.
import base64
import io
import json

import numpy as np
from PIL import Image

import check
import gerber
import kicad5
import model
from footprints import xform
from geom import RULES, Shape

PXMM = 30          # résolution des textures (px par mm)

# Volumes des boîtiers : (centre local x, y, largeur x, profondeur y, hauteur, couleur, genre)
BODY = {
    'R_0402_1005Metric': (0, 0, 1.0, 0.5, 0.35, '#1d1d1f', 'res'),
    'C_0402_1005Metric': (0, 0, 1.0, 0.5, 0.5, '#b8976a', 'cap'),
    'C_0603_1608Metric': (0, 0, 1.6, 0.8, 0.8, '#b8976a', 'cap'),
    'C_0805_2012Metric': (0, 0, 2.0, 1.25, 1.25, '#a98c60', 'cap'),
    'SOT-23-6': (0, 0, 1.6, 2.9, 1.1, '#202022', 'ic'),
    'LGA-14_3x2.5mm_P0.5mm_LayoutBorder3x4y': (0, 0, 3.0, 2.5, 0.83, '#26262a', 'lga'),
    'L_Taiyo-Yuden_NR-30xx': (0, 0, 3.0, 3.0, 1.5, '#3b3d40', 'ind'),
    'D_SOD-123': (0, 0, 2.7, 1.6, 1.1, '#202022', 'diode'),
    'JST_SH_SM04B-SRSS-TB_1x04-1MP_P1.00mm_Horizontal': (0, 0.45, 6.0, 4.25, 2.9, '#efe6cf', 'jst'),
    'JST_SH_SM05B-SRSS-TB_1x05-1MP_P1.00mm_Horizontal': (0, 0.45, 7.0, 4.25, 2.9, '#efe6cf', 'jst'),
}

# Dessus de la carte ESP32 (relevé sur la photo, vue de face : x depuis le bord
# gauche, y depuis le bord haut, en mm) : (nom, x, y, largeur, profondeur, hauteur, couleur, genre)
ESP_TOP = [
    ('Antenne céramique', 8.4, 1.25, 11.4, 2.1, 1.1, '#e8e8e6', 'ant'),
    ('Prise IPEX', 17.4, 1.8, 2.6, 2.6, 1.2, '#c9c9c9', 'box'),
    ('ESP32-S3R8', 9.6, 10.0, 7.0, 7.0, 0.9, '#2a2b2e', 'chip'),
    ('Quartz 40 MHz', 16.0, 9.8, 3.2, 2.5, 0.8, '#bdbdbd', 'box'),
    ('Flash 16 Mo', 8.2, 17.6, 3.5, 3.5, 0.8, '#232325', 'box'),
    ('DEL RGB', 13.0, 17.6, 3.5, 3.5, 1.4, '#f4f4f2', 'led'),
    ('Régulateur 3,3 V', 16.6, 17.8, 3.0, 1.6, 1.1, '#232325', 'box'),
    ('Bouton RST', 8.6, 22.7, 4.0, 3.0, 1.6, '#c4c4c4', 'btn'),
    ('Bouton BOOT', 14.3, 22.7, 4.0, 3.0, 1.6, '#c4c4c4', 'btn'),
    ('USB-C', 11.7, 29.4, 9.0, 7.4, 3.2, '#cfd2d6', 'usb'),
]


def png_uri(arr):
    im = Image.fromarray(arr)
    buf = io.BytesIO()
    im.save(buf, 'PNG', optimize=True)
    return 'data:image/png;base64,' + base64.b64encode(buf.getvalue()).decode()


def textures():
    rep, P, R = check.run(verbose=False)
    comps = model.build()
    sh = check.copper_shapes(comps)
    g = model.GEO
    # zone utile (contour du PCB support)
    i0, j0 = [int(round(v)) for v in R.px(0, g['YCUT'])]
    i1, j1 = [int(round(v)) for v in R.px(g['W'], g['H'])]
    holes = R.blank()
    for (x, y, n) in model.VIAS:
        R.draw(holes, Shape(None, 'F', 'circle', (x, y, RULES['via_drill'] / 2)))
    for c in comps:
        for p, x, y, a in c.pads_abs():
            if p.drill:
                R.draw(holes, Shape(None, 'F', 'circle', (x, y, p.drill / 2)))
    holes = np.array(holes, bool)
    silk_items = [(t, x, y, s, L, False) for (t, x, y, s, L) in kicad5.SILK_TEXT]
    silk_items.append(('ESP32 de ce côté, USB-C en bas', g['W'] / 2, 4.6, 0.8, 'B.SilkS', True))   # lu depuis la face B : miroir dans le repère du PCB
    out = {}
    for L in ('F', 'B'):
        cu = R.mask([s for s in sh], L) | P[L]
        pads = R.mask([s for s in sh if s.src not in ('trk', 'via')], L)
        silk = gerber.text_mask(R, [t for t in silk_items if t[4] == L + '.SilkS'])
        if L == 'F':
            from PIL import ImageDraw
            im = Image.new('1', (R.nx, R.ny), 0); d = ImageDraw.Draw(im)
            for c in comps:
                for (layer, x1, y1, x2, y2, w) in c.fp.lines:
                    if layer == 'F.SilkS':
                        a = R.px(*xform(c.x, c.y, c.rot, x1, y1)); b = R.px(*xform(c.x, c.y, c.rot, x2, y2))
                        d.line([a, b], fill=1, width=max(1, int(round(0.15 / R.res))))
            silk |= np.array(im, bool)
        silk &= ~pads
        a = np.zeros(cu.shape + (3,), np.float32)
        a[:] = (22, 84, 52)                     # vernis vert sur FR4
        a[cu] = (46, 128, 74)                   # cuivre sous vernis
        a[pads] = (205, 207, 210)               # pastilles étamées (HASL)
        a[silk] = (245, 245, 240)
        a[holes] = (28, 30, 32)
        a = a[j0:j1, i0:i1]
        im = Image.fromarray(a.astype(np.uint8))
        w = int(round(g['W'] * PXMM)); h = int(round((g['H'] - g['YCUT']) * PXMM))
        im = im.resize((w, h), Image.LANCZOS)
        out[L] = png_uri(np.array(im))
    return out, rep


def components():
    comps = model.build()
    out = []
    for c in comps:
        if c.ref == 'U3':
            continue
        b = BODY[c.fp.name]
        cx, cy = xform(c.x, c.y, c.rot, b[0], b[1])
        nets = sorted({n for n in c.nets.values() if n})
        out.append(dict(ref=c.ref, value=c.value, lcsc=c.lcsc, mpn=c.mpn, descr=c.descr, x=round(cx, 3),
                        y=round(cy, 3), rot=c.rot, w=b[2], d=b[3], h=b[4], color=b[5], kind=b[6], nets=nets))
    return out


def holes_esp():
    comps = model.build()
    u3 = [c for c in comps if c.ref == 'U3'][0]
    return [dict(x=round(x, 3), y=round(y, 3), name=p.num, net=u3.nets.get(p.num)) for p, x, y, a in u3.pads_abs()]


def build():
    tex, rep = textures()
    g = model.GEO
    return dict(
        board=dict(W=g['W'], H=g['H'], YCUT=g['YCUT'], T=1.6),
        esp=dict(W=g['W'], H=g['H'], T=1.2, gap=2.5, Y0=g['Y0'], PITCH=g['PITCH'],
                 cols=[g['XL_OUT'], g['XL_IN'], g['XR_IN'], g['XR_OUT']], top=[dict(name=n, x=x, y=y, w=w, d=d, h=h, color=col, kind=k)
                                                          for (n, x, y, w, d, h, col, k) in ESP_TOP]),
        textures=tex, comps=components(), holes=holes_esp(),
        stats=dict(tracks=len(model.TRACKS), vias=len(model.VIAS), clearance=rep['mesures']['isolement_min'],
                   drc=len(rep['erreurs'])),
    )


if __name__ == '__main__':
    import sys
    d = build()
    json.dump(d, open(sys.argv[1], 'w'))
    print({k: (len(v) if isinstance(v, (list, dict)) else v) for k, v in d.items()})
