#!/usr/bin/env python3
"""
Boîtier TrimBox DIY S3 — voiture 1/10 — version 2, SANS AUCUNE VIS.
Génère docs/boitier/trimbox-boitier.3mf (fond + couvercle, orientés pour
l'impression).

  FOND
    • carte ESP32-S3 (57 × 28 mm + antenne 18 × 6,2 mm) posée sur 3 plots :
      côté USB, le bord du circuit glisse dans une RAINURE de la paroi ;
      côté antenne, deux CROCHETS élastiques la verrouillent. Les crochets
      guident aussi l'antenne sur les côtés.
    • ouverture pour les prises USB, 2 encoches de sortie des câbles
    • dessous PLAT (adhésif double face) traversé par 2 PASSANTS à colliers
    • 4 fenêtres où s'enclenchent les languettes du couvercle
  COUVERCLE (imprimé à l'envers, face lisse sur le plateau)
    • GPS HGLRC M100 (circuit 21 × 21) retenu par 2 crochets, antenne
      18 × 18 dans une OUVERTURE : vue directe du ciel
    • IMU retenue par 2 crochets, composants vers le couvercle (orientation
      « à l'endroit » : aucun réglage d'axes à changer dans le firmware)
    • trou au-dessus de la DEL RGB de la carte
    • 4 languettes à cran : fermeture sans vis
    • flèche « avant » gravée (= axe X de l'IMU)

Repère : X le long du boîtier, x = 0 contre la paroi côté USB ; Y en
travers, y = 0 côté « bas » de la carte telle que mesurée (USB à gauche,
composants vers le haut) ; Z vers le haut, z = 0 sous le fond.
Toutes les cotes (mm) sont en tête de fichier.
Usage : python3 tools/gen_boitier.py        (dépendance : pip install manifold3d)
"""
import os, zipfile
from manifold3d import Manifold, CrossSection

# ------------------------------------------------------------------ mesures
TOL      = 0.5           # jeu d'impression autour des pièces
PCB_L, PCB_W, PCB_T = 57.0, 28.0, 1.6   # carte ESP32-S3 (sans l'antenne)
ANT_W, ANT_L = 18.0, 6.2                 # débord de l'antenne (centré en largeur)
LED_X, LED_Y = 8.8, 18.40                # centre de la DEL : depuis le bord USB (gauche)
                                         # et depuis le bord « bas » de la carte
PARTS_H  = 3.6            # composants les plus hauts de la carte (USB-C, module)
PAD_H    = 2.5            # plots sous la carte (queues de soudure dessous)
GPS_PCB, GPS_PCB_T = 21.0, 1.2           # circuit du GPS
GPS_ANT  = 18.0                          # antenne céramique du GPS
GPS_UNDER = 3.0           # composants / connecteur sous le circuit du GPS
IMU_L, IMU_W, IMU_T = 21.0, 17.0, 1.6    # module IMU (à mesurer)
IMU_COMP = 1.5            # hauteur des composants de l'IMU (côté couvercle)

WALL, FLOOR, LID_T = 2.0, 5.0, 2.0
IH       = 20.0           # hauteur intérieure (place pour les fils soudés)
TUN_W, TUN_H, TUN_Z = 6.0, 2.6, 1.2      # passants à colliers (≤ 5 mm)
USB_W    = 24.0
NOTCH_W, NOTCH_D = 8.0, 5.0
SLOT_D   = 1.0            # profondeur de la rainure côté USB
LIP_H, LIP_T = 6.0, 1.2   # lèvre du couvercle (languettes longues = souples)
SNAP_W, SNAP_BUMP, SNAP_H = 7.0, 0.8, 1.2  # languettes à cran
SNAP_X   = (12.0, 50.0)
HOOK_T, HOOK_OVER, HOOK_HEAD = 1.2, 0.5, 1.2   # crochets du couvercle (GPS, IMU)
B_HOOK_T, B_HOOK_OVER, B_HOOK_HEAD = 1.4, 0.8, 1.4   # crochets de la carte (fond)
GPS_DEPTH = 3.5          # face avant du circuit GPS sous le couvercle (rails d'appui)
IMU_STAND = 3.0          # hauteur des plots d'appui de l'IMU
# Allongement des crochets ≈ 3 %, admissible en PETG (et en PLA pour un
# montage peu répété) : ne pas augmenter HOOK_OVER sans allonger les tiges.

# ------------------------------------------------------------------ dérivées
IL = PCB_L + ANT_L - SLOT_D + 1.5        # intérieur : du fond de la rainure… à la paroi côté antenne
IW = PCB_W + 2 * TOL
OL, OW = IL + 2 * WALL, IW + 2 * WALL
H_BASE = FLOOR + IH
X0, Y0 = WALL, WALL                      # origine de l'intérieur
PCB_X0 = X0 - SLOT_D                     # bord USB du circuit (dans la rainure)
PCB_X1 = PCB_X0 + PCB_L                  # bord côté antenne
PCB_Y0 = Y0 + TOL                        # bord « bas »
YC = Y0 + IW / 2
Z_PCB = FLOOR + PAD_H                    # dessous du circuit

def ccw(pts):
    """Polygone dans le sens trigonométrique (exigé par CrossSection)."""
    a = sum(p[0] * q[1] - q[0] * p[1] for p, q in zip(pts, pts[1:] + pts[:1]))
    return pts if a > 0 else pts[::-1]
def prof(pts):
    return CrossSection([ccw(pts)])

def box(x, y, z, sx, sy, sz):
    return Manifold.cube([sx, sy, sz]).translate([x, y, z])
def cyl(x, y, z, h, d, seg=40):
    return Manifold.cylinder(h, d / 2, d / 2, seg).translate([x, y, z])
def prism_xz(pts, y0, width):
    """Prisme de profil `pts` dans le plan (x, z), épaisseur `width` en y."""
    return prof(pts).extrude(width).rotate([90, 0, 0]).translate([0, y0 + width, 0])
def prism_yz(pts, x0, width):
    """Prisme de profil `pts` dans le plan (y, z), épaisseur `width` en x."""
    return prof(pts).extrude(width).rotate([90, 0, 90]).translate([x0, 0, 0])

def arrow(x, y, z, length, depth, w=2.0, head=5.0, sign=1):
    """Flèche horizontale (vers +X si sign = 1, vers −X sinon), creusée de `depth`."""
    if sign > 0:
        shaft = box(x, y - w / 2, z, length - head, w, depth)
        tri = prof([[0, -head * 0.7], [head, 0], [0, head * 0.7]]).extrude(depth).translate([x + length - head, y, z])
    else:
        shaft = box(x + head, y - w / 2, z, length - head, w, depth)
        tri = prof([[head, -head * 0.7], [0, 0], [head, head * 0.7]]).extrude(depth).translate([x, y, z])
    return shaft + tri

# ------------------------------------------------------------------ fond
def base():
    b = box(0, 0, 0, OL, OW, H_BASE) - box(X0, Y0, FLOOR, IL, IW, IH + 1)
    # passants pour colliers : tunnels transversaux, dessous plat
    for x in (X0 + 9, X0 + 45):
        b = b - box(x, -1, TUN_Z, TUN_W, OW + 2, TUN_H)
    # plots sous la carte, sur l'axe (loin des soudures des bords)
    for px in (X0 + 6, X0 + 26, X0 + 46):
        b = b + box(px, YC - 4, FLOOR, 5, 8, PAD_H)
    # côté USB : rainure qui reçoit le bord du circuit + ouverture des prises
    b = b - box(X0 - SLOT_D, Y0, Z_PCB - 0.25, SLOT_D + 0.01, IW, PCB_T + 0.5)
    b = b - box(-1, YC - USB_W / 2, Z_PCB + PCB_T, WALL + 2, USB_W, PARTS_H + 0.8)
    # côté antenne : deux crochets élastiques de part et d'autre de l'antenne.
    # Leur tige part du fond d'une poche creusée dans le plancher : plus
    # longue, elle plie sans casser.
    for side in (-1, 1):
        y_in = YC + side * (ANT_W / 2 + 0.4)          # face intérieure : guide l'antenne
        yw = 3.0
        y0 = y_in if side > 0 else y_in - yw
        xs = PCB_X1 + 0.3                              # tige juste après le bord du circuit
        z_root = 1.5
        zc = Z_PCB + PCB_T + 0.2                       # dessous de la tête
        z_top = zc + B_HOOK_HEAD
        b = b - box(xs - 0.6, y0 - 0.6, z_root, B_HOOK_T + 1.2, yw + 1.2, FLOOR - z_root + 0.01)
        # tige + tête d'un seul profil ; chanfrein d'entrée au-dessus
        hook = prism_xz([[xs, z_root], [xs + B_HOOK_T, z_root], [xs + B_HOOK_T, z_top], [xs, z_top],
                         [xs - B_HOOK_OVER, zc + 0.35 * B_HOOK_HEAD], [xs - B_HOOK_OVER, zc], [xs, zc]], y0, yw)
        b = b + hook
    # encoches de sortie des câbles, sur les deux grands côtés
    for y in (-1, OW - WALL - 1):
        b = b - box(X0 + IL / 2 - NOTCH_W / 2, y, H_BASE - NOTCH_D, NOTCH_W, WALL + 2, NOTCH_D + 1)
    # fenêtres des languettes du couvercle
    zc = H_BASE - LIP_H + SNAP_H           # face d'accrochage de la languette
    for x in SNAP_X:
        for y in (-1, OW - WALL - 1):
            b = b - box(X0 + x - (SNAP_W + 0.8) / 2, y, zc - SNAP_H - 0.35, SNAP_W + 0.8, WALL + 2, SNAP_H + 0.5)
    return b

# ------------------------------------------------------------------ couvercle
# Modélisé en position de service : dessous du couvercle à z = 0, le couvercle
# occupe 0 ≤ z ≤ LID_T, tout ce qui pend est en z < 0.
def lid():
    l = box(0, 0, 0, OL, OW, LID_T)
    c = 0.25                                         # jeu lèvre / paroi
    li_x0, li_y0 = X0 + c, Y0 + c
    li_l, li_w = IL - 2 * c, IW - 2 * c
    lip = box(li_x0, li_y0, -LIP_H, li_l, li_w, LIP_H) - \
          box(li_x0 + LIP_T, li_y0 + LIP_T, -LIP_H - 1, li_l - 2 * LIP_T, li_w - 2 * LIP_T, LIP_H + 2)
    for y in (li_y0 - 1, li_y0 + li_w - LIP_T - 1):  # passages des câbles
        lip = lip - box(X0 + IL / 2 - NOTCH_W / 2 - 0.3, y, -LIP_H - 1, NOTCH_W + 0.6, LIP_T + 2, LIP_H + 2)
    # languettes à cran : fentes de part et d'autre + bossage vers l'extérieur
    for x in SNAP_X:
        xa = X0 + x - SNAP_W / 2
        for side in (0, 1):
            yl = li_y0 if side == 0 else li_y0 + li_w - LIP_T
            for xs in (xa - 0.8, xa + SNAP_W):
                lip = lip - box(xs, yl - 1, -LIP_H - 1, 0.8, LIP_T + 2, LIP_H + 1 - 0.01)
            # bossage : face d'accrochage horizontale en haut, chanfrein en bas
            zb, zt = -LIP_H, -LIP_H + SNAP_H
            if side == 0:
                yo = yl
                pr = [[yo + 0.3, zb], [yo + 0.3, zt], [yo - SNAP_BUMP, zt], [yo - SNAP_BUMP, zb + 0.5], [yo - 0.2, zb]]
            else:
                yo = yl + LIP_T
                pr = [[yo - 0.3, zb], [yo + 0.2, zb], [yo + SNAP_BUMP, zb + 0.5], [yo + SNAP_BUMP, zt], [yo - 0.3, zt]]
            lip = lip + prism_yz(pr, xa, SNAP_W)
    l = l + lip

    # ---- GPS : 2 rails d'appui (bords du circuit) + 2 crochets aux bouts,
    #      ouverture chanfreinée pour l'antenne (vue directe du ciel)
    gx0 = X0 + 11.5
    g = GPS_PCB + 2 * 0.3
    gy0 = YC - g / 2
    back = GPS_DEPTH + GPS_PCB_T + 0.2
    for side in (0, 1):
        ys = gy0 if side == 0 else gy0 + g - 1.2
        yo = gy0 - 1.2 if side == 0 else gy0 + g
        l = l + box(gx0, ys, -GPS_DEPTH, g, 1.2, GPS_DEPTH)          # appui
        l = l + box(gx0 - 1.0, yo, -back, g + 2.0, 1.2, back)        # guide latéral
    l = l + hooks_x(gx0, g, YC, back)
    a = GPS_ANT + 0.6
    ax = gx0 + g / 2
    l = l - (box(ax - a / 2, YC - a / 2, -GPS_DEPTH - 0.5, a, a, GPS_DEPTH + 0.5) +
             Manifold.hull(box(ax - a / 2, YC - a / 2, -0.01, a, a, 0.01) +
                           box(ax - a / 2 - LID_T, YC - a / 2 - LID_T, LID_T, a + 2 * LID_T, a + 2 * LID_T, 0.5)))
    # ---- IMU : plots d'appui (composants côté couvercle) + 2 crochets
    ix0 = gx0 + g + 2 * HOOK_T + 1.5
    iw_ = IMU_W + 0.6
    il_ = IMU_L + 0.6
    iy0 = YC - iw_ / 2
    for (px, py) in ((ix0, iy0), (ix0 + il_ - 3, iy0), (ix0, iy0 + iw_ - 3), (ix0 + il_ - 3, iy0 + iw_ - 3)):
        l = l + box(px, py, -IMU_STAND, 3, 3, IMU_STAND)
    l = l + hooks_x(ix0, il_, YC, IMU_STAND + IMU_T + 0.2)
    # repère de l'axe X de l'IMU, gravé sous le couvercle (vers l'avant = côté USB)
    l = l - arrow(ix0 + 4, YC, -0.01, 12, 0.5, sign=-1)
    # ---- DEL de la carte
    led_x = PCB_X0 + LED_X
    led_y = PCB_Y0 + LED_Y
    l = l - cyl(led_x, led_y, -1, LID_T + 2, 3.4)
    # ---- flèche « avant » gravée sur le dessus
    l = l - arrow(ix0 + 1, YC, LID_T - 0.6, il_ - 2, 0.61, w=3.0, head=7.0, sign=-1)
    return l

def hooks_x(x0, span, yc, depth, width=6.0):
    """Deux crochets pendants aux extrémités X d'un logement de longueur
    `span` commençant en x0 ; ils retiennent une plaque dont la face arrière
    est à `depth` sous le couvercle. Têtes vers l'intérieur, chanfrein dessous."""
    out = None
    top, bot = 0.3, -(depth + HOOK_HEAD)             # la tige entre de 0,3 dans le couvercle
    for side in (0, 1):
        if side == 0:                                # tige à gauche, tête vers +X
            xi = x0
            hp = [[xi - HOOK_T, top], [xi - HOOK_T, bot], [xi, bot], [xi + HOOK_OVER, -depth], [xi, -depth], [xi, top]]
        else:                                        # tige à droite, tête vers −X
            xi = x0 + span
            hp = [[xi, top], [xi, -depth], [xi - HOOK_OVER, -depth], [xi, bot], [xi + HOOK_T, bot], [xi + HOOK_T, top]]
        piece = prism_xz(hp, yc - width / 2, width)
        out = piece if out is None else out + piece
    return out

# ------------------------------------------------------------------ 3MF
def mesh_of(m):
    me = m.to_mesh()
    return me.vert_properties[:, :3], me.tri_verts

def write_3mf(path, objects):
    parts, items = [], []
    for i, (name, m) in enumerate(objects, start=1):
        v, t = mesh_of(m)
        vs = ''.join(f'<vertex x="{x:.4f}" y="{y:.4f}" z="{z:.4f}"/>' for x, y, z in v)
        ts = ''.join(f'<triangle v1="{a}" v2="{b}" v3="{c}"/>' for a, b, c in t)
        parts.append(f'<object id="{i}" name="{name}" type="model"><mesh><vertices>{vs}</vertices>'
                     f'<triangles>{ts}</triangles></mesh></object>')
        items.append(f'<item objectid="{i}"/>')
    model = ('<?xml version="1.0" encoding="UTF-8"?>\n'
             '<model unit="millimeter" xml:lang="fr-FR" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Title">TrimBox DIY S3 — boîtier 1/10 sans vis</metadata>'
             '<metadata name="Designer">TrimBox DIY</metadata>'
             f'<resources>{"".join(parts)}</resources><build>{"".join(items)}</build></model>')
    ct = ('<?xml version="1.0" encoding="UTF-8"?>\n<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
          '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
          '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>\n<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr('[Content_Types].xml', ct)
        z.writestr('_rels/.rels', rels)
        z.writestr('3D/3dmodel.model', model)

def build():
    b = base()
    l = lid()
    # couvercle retourné pour l'impression : dessus lisse sur le plateau
    lp = l.mirror([0, 0, 1]).translate([0, 0, LID_T]).translate([0, OW + 10, 0])
    return b, l, lp

if __name__ == '__main__':
    here = os.path.dirname(os.path.abspath(__file__))
    outdir = os.path.join(here, '..', 'docs', 'boitier')
    os.makedirs(outdir, exist_ok=True)
    b, l, lp = build()
    for name, m in (('fond', b), ('couvercle', l)):
        assert m.status().name == 'NoError', m.status()
        print(f'{name:10s} volume {m.volume()/1000:5.1f} cm³  genre {m.genus()}  boîte {tuple(round(v,1) for v in m.bounding_box())}')
    write_3mf(os.path.join(outdir, 'trimbox-boitier.3mf'),
              [('TrimBox fond', b), ('TrimBox couvercle (retourne)', lp)])
    print(f'extérieur : {OL:.1f} × {OW:.1f} × {H_BASE + LID_T:.1f} mm ; intérieur {IL:.1f} × {IW:.1f} × {IH:.1f} mm')
