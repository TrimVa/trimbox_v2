# Schéma de câblage TrimBox DIY S3 — génère docs/cablage-trimbox-s3.svg
# Usage : python3 tools/gen_cablage.py   (PDF : cairosvg docs/cablage-trimbox-s3.svg -o docs/cablage-trimbox-s3.pdf)
W, H = 1900, 1060
out = []
def a(s): out.append(s)
def esc(t): return t.replace('&','&amp;').replace('<','&lt;').replace('>','&gt;')
def text(x, y, t, size=13, anchor='start', weight='400', fill='#1d1d28', cls=''):
    a(f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" fill="{fill}">{esc(t)}</text>')
def rect(x, y, w, h, fill='#fff', stroke='#1d1d28', rx=8, sw=1.6, dash=None):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    a(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"{d}/>')
def wire(pts, color, width=3, dash=None):
    d = ' '.join(('M' if i == 0 else 'L') + f'{x},{y}' for i, (x, y) in enumerate(pts))
    ds = f' stroke-dasharray="{dash}"' if dash else ''
    a(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" stroke-linecap="round"{ds}/>')
def dot(x, y, color): a(f'<circle cx="{x}" cy="{y}" r="4.5" fill="{color}" stroke="#fff" stroke-width="1"/>')
def pin(x, y, color='#1d1d28'): a(f'<circle cx="{x}" cy="{y}" r="4" fill="#fff" stroke="{color}" stroke-width="2"/>')

C = dict(v5='#d62828', v33='#f08c00', gnd='#2b2b2b', gtx='#2a9d8f', grx='#1d6fb8',
         sda='#7b2cbf', scl='#c2185b', ctx='#0f9d58', crx='#b8860b', xbus='#6d6d6d',
         pwm='#8d99ae', bat='#6a040f')

a(f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Arial, Helvetica, sans-serif">')
a(f'<rect width="{W}" height="{H}" fill="#fbfaf7"/>')
text(40, 48, 'TrimBox DIY S3 — schéma de câblage', 26, weight='700')
text(40, 74, 'ESP32-S3-DevKitC-1 (N16R8) · GPS HGLRC M100 Mini · IMU LSM6DS3 · récepteur RadioMaster ER5C-i (ExpressLRS) · ESC XC-E8', 14, fill='#555')
text(40, 94, 'Firmware 2.0-a4 — brochage défini dans trimbox_s3/src/config.h', 13, fill='#777')

# ---------------------------------------------------------------- carte ESP32
BX, BY, BW, BH = 940, 150, 270, 720
rect(BX, BY, BW, BH, fill='#243447', stroke='#10202f', rx=10, sw=2)
CX = BX + BW / 2
rect(CX - 45, BY + 150, 90, 80, fill='#9aa7b4', stroke='#5d6b78', rx=4)
text(CX, BY + 185, 'ESP32-S3', 12, 'middle', '700', '#10202f')
text(CX, BY + 202, 'WROOM-1', 10, 'middle', '400', '#10202f')
text(CX, BY + 255, 'DevKitC-1', 15, 'middle', '700', '#fff')
text(CX, BY + 273, 'N16R8', 12, 'middle', '400', '#c9d3dd')
text(CX, BY + 300, 'antenne ↑', 10, 'middle', '400', '#8fa1b3')
J1 = ['3V3','3V3','RST','4','5','6','7','15','16','17','18','8','3','46','9','10','11','12','13','14','5V','GND']
J3 = ['GND','TX','RX','1','2','42','41','40','39','38','37','36','35','0','45','48','47','21','20','19','GND','GND']
PY0, PSTEP = BY + 45, 30.5
def j1y(name, occ=0):
    idx = [i for i, n in enumerate(J1) if n == name][occ]
    return PY0 + idx * PSTEP
for i, n in enumerate(J1):
    y = PY0 + i * PSTEP
    a(f'<rect x="{BX-8}" y="{y-8}" width="16" height="16" rx="2" fill="#d4af37" stroke="#8a6d1d"/>')
    lbl = n if not n.isdigit() else 'GPIO ' + n
    text(BX + 14, y + 4, lbl, 11, 'start', '400', '#e6edf3')
for i, n in enumerate(J3):
    y = PY0 + i * PSTEP
    a(f'<rect x="{BX+BW-8}" y="{y-8}" width="16" height="16" rx="2" fill="#d4af37" stroke="#8a6d1d"/>')
    lbl = {'TX':'TX0 (43)','RX':'RX0 (44)'}.get(n, n if not n.isdigit() else 'GPIO ' + n)
    text(BX + BW - 14, y + 4, lbl, 11, 'end', '400', '#e6edf3')
text(BX + 20, BY - 10, 'J1', 13, 'start', '700', '#243447')
text(BX + BW - 20, BY - 10, 'J3', 13, 'end', '700', '#243447')
# annotations côté J3
y0 = PY0 + J3.index('0') * PSTEP
wire([(BX+BW+10, y0), (BX+BW+40, y0)], '#555', 1.5)
text(BX+BW+46, y0 + 4, 'bouton BOOT (3 s : Wi-Fi)', 12, fill='#333')
y48 = PY0 + J3.index('48') * PSTEP
wire([(BX+BW+10, y48), (BX+BW+40, y48)], '#555', 1.5)
text(BX+BW+46, y48 + 4, 'DEL RGB (38 si carte v1.1)', 12, fill='#333')
yu = PY0 + J3.index('19') * PSTEP
text(BX+BW+46, yu - 11, '19 / 20 : USB, ne pas utiliser', 12, fill='#a33')
text(BX+BW+46, PY0 + J3.index('36') * PSTEP + 4, '35 / 36 / 37 : PSRAM,', 12, fill='#a33')
text(BX+BW+46, PY0 + J3.index('35') * PSTEP + 4, 'ne pas utiliser', 12, fill='#a33')
# prises USB
for k, (lab, sub) in enumerate([('USB', 'journal série + flash'), ('UART', 'pont série')]):
    ux = BX + 50 + k * 120
    rect(ux, BY + BH - 6, 50, 26, fill='#b0b8c0', stroke='#555', rx=6)
    text(ux + 25, BY + BH + 42, lab, 13, 'middle', '700')
    text(ux + 25, BY + BH + 58, sub, 10, 'middle', '400', '#555')
text(BX + BW/2, BY + BH + 80, '→ brancher le câble sur « USB »', 12, 'middle', '700', '#243447')

# ---------------------------------------------------------------- composants
def comp(x, y, w, h, title, sub, pins, side='right', fill='#ffffff'):
    rect(x, y, w, h, fill=fill)
    text(x + 12, y + 22, title, 15, weight='700')
    if sub: text(x + 12, y + 38, sub, 11, fill='#666')
    pos = {}
    for i, p in enumerate(pins):
        label, py = p[0], p[1]
        shown = p[2] if len(p) > 2 else label
        px = x + w if side == 'right' else x
        pin(px, py)
        if side == 'right': text(px - 10, py + 4, shown, 12, 'end')
        else: text(px + 10, py + 4, shown, 12, 'start')
        pos[label] = (px, py)
    return pos

gps = comp(470, 140, 220, 170, 'GPS HGLRC M100 Mini', 'u-blox M10 · 25 Hz · UART', [
    ('VCC', 190, 'VCC → 5V'), ('GND', 220, 'GND → GND'), ('TX', 250, 'TX → GPIO 18'), ('RX', 280, 'RX ← GPIO 17')])
imu = comp(470, 350, 220, 170, 'IMU LSM6DS3', 'accéléromètre ±16 g · I2C 0x6A/0x6B', [
    ('VCC', 400, 'VCC → 3V3'), ('GND', 430, 'GND → GND'), ('SDA', 460, 'SDA → GPIO 8'), ('SCL', 490, 'SCL → GPIO 9')])
rx  = comp(470, 560, 220, 230, 'Récepteur ER5C-i', 'ExpressLRS · sorties 2/3 en CRSF', [
    ('Sortie 2  S = TX', 610, 'Sortie 2 S (TX) → GPIO 16'), ('Sortie 3  S = RX', 645, 'Sortie 3 S (RX) ← GPIO 15'), ('− (GND)', 680, '− (masse) → GND')])
rxl = {}
for label, py in [('Sortie 1', 700), ('Sortie 4', 725), ('Sortie 5  +', 750), ('Sortie 5  −', 772)]:
    pin(470, py); text(482, py + 4, label, 12); rxl[label] = (470, py)
reg = comp(470, 840, 220, 110, 'Régulateur 5 V', 'abaisseur ≥ 1 A (ex. Pololu D24V10F5)', [
    ('OUT 5 V', 890, 'OUT 5 V → 5V'), ('GND', 920)])
pin(470, 890); text(482, 894, 'IN (6-8,4 V)', 12); pin(470, 920); text(482, 924, 'GND', 12)

servo = comp(60, 560, 230, 90, 'Servo de direction', 'voie CH1', [('câble servo', 615)])
esc_ = comp(60, 680, 230, 180, 'ESC XC-E8', 'BEC 6,0 / 7,4 / 8,4 V', [
    ('câble gaz (S + −)', 730), ('X-Bus (S)', 800)])
bat = comp(60, 900, 230, 70, 'Batterie de propulsion', None, [('+ / −', 945)], fill='#fff5f5')
wire([(290, 945), (330, 945), (330, 830), (290, 830)], C['bat'], 5)
text(338, 892, 'puissance', 10, fill=C['bat'])

# ---------------------------------------------------------------- câbles
X1 = BX - 8   # bord des broches J1
def to_board(src, lane, pin_y, color, label, dash=None, width=3):
    pts = [src, (lane, src[1]), (lane, pin_y), (X1, pin_y)]
    wire(pts, color, width, dash)
    dot(X1, pin_y, color)
    pass

# alimentation
to_board(gps['VCC'], 720, j1y('5V'), C['v5'], '→ 5V')
to_board(reg['OUT 5 V'], 720, j1y('5V'), C['v5'], '')
dot(720, 890, C['v5'])
to_board(imu['VCC'], 740, j1y('3V3'), C['v33'], '→ 3V3')
# masses : rail commun
GL = 760
for src in (gps['GND'], imu['GND'], rx['− (GND)'], reg['GND']):
    wire([src, (GL, src[1])], C['gnd'], 3); dot(GL, src[1], C['gnd'])
wire([(GL, 220), (GL, j1y('GND')), (X1, j1y('GND'))], C['gnd'], 3)
dot(X1, j1y('GND'), C['gnd']); text(GL + 8, j1y('GND') - 6, 'masse commune → GND', 10, 'start', '700', C['gnd'])
# GPS (TX et RX croisés)
to_board(gps['TX'], 800, j1y('18'), C['gtx'], '→ GPIO 18')
to_board(gps['RX'], 820, j1y('17'), C['grx'], '← GPIO 17')
# IMU
to_board(imu['SDA'], 840, j1y('8'), C['sda'], '→ GPIO 8')
to_board(imu['SCL'], 860, j1y('9'), C['scl'], '→ GPIO 9')
# CRSF (croisés)
to_board(rx['Sortie 2  S = TX'], 880, j1y('16'), C['ctx'], '→ GPIO 16')
to_board(rx['Sortie 3  S = RX'], 900, j1y('15'), C['crx'], '← GPIO 15')
# X-Bus (futur) via pont diviseur
DX, DY = 360, 780
rect(DX, DY - 36, 80, 72, fill='#f4f4f4', stroke='#888', rx=6, dash='5 4')
text(DX + 40, DY - 18, 'pont', 11, 'middle', '700', '#555'); text(DX + 40, DY - 4, '10 kΩ / 20 kΩ', 10, 'middle', '400', '#555')
text(DX + 40, DY + 10, 'si signal 5 V', 10, 'middle', '400', '#555'); text(DX + 40, DY + 24, '(à mesurer)', 10, 'middle', '400', '#a33')
wire([esc_['X-Bus (S)'], (DX, 800)], C['xbus'], 2.5, '7 5')
wire([(DX + 40, 816), (DX + 40, 848)], C['xbus'], 2.5, '7 5')
# renvoi : même repère des deux côtés
def tag(x, y, t, anchor):
    w = 7 * len(t) + 14
    x0 = x if anchor == 'start' else x - w
    a(f'<rect x="{x0}" y="{y-11}" width="{w}" height="20" rx="10" fill="#fff" stroke="{C["xbus"]}" stroke-dasharray="4 3"/>')
    text(x0 + w/2, y + 4, t, 10, 'middle', '700', C['xbus'])
tag(DX + 40 + 45, 860, 'vers GPIO 5', 'end')
wire([(X1 - 60, j1y('5')), (X1, j1y('5'))], C['xbus'], 2.5, '7 5'); dot(X1, j1y('5'), C['xbus'])
tag(X1 - 62, j1y('5'), 'X-Bus ESC (à venir)', 'end')
# récepteur : servo, ESC, alimentation du régulateur
wire([servo['câble servo'], (330, 615), (330, 700), rxl['Sortie 1']], C['pwm'], 3)
wire([esc_['câble gaz (S + −)'], (380, 730), (380, 725), rxl['Sortie 4']], C['pwm'], 3)
text(72, 770, 'Le câble gaz alimente aussi le', 11, fill='#555'); text(72, 785, 'récepteur (BEC) : sortie 4 = CH2', 11, fill='#555')
wire([rxl['Sortie 5  +'], (455, 750), (455, 890), (470, 890)], C['v5'], 3)
wire([rxl['Sortie 5  −'], (445, 772), (445, 920), (470, 920)], C['gnd'], 3)
text(462, 822, 'BEC 6-8,4 V', 10, 'start', '700', C['v5'])

# ---------------------------------------------------------------- légende et règles
LX, LY = 1510, 150
rect(LX, LY, 360, 240, fill='#fff', rx=8)
text(LX + 14, LY + 24, 'Légende', 15, weight='700')
items = [('5 V', C['v5'], None), ('3,3 V', C['v33'], None), ('Masse (GND)', C['gnd'], None),
         ('GPS UART (TX / RX)', C['gtx'], None), ('IMU I2C (SDA / SCL)', C['sda'], None),
         ('CRSF récepteur (TX / RX)', C['ctx'], None), ('Voies PWM du récepteur', C['pwm'], None),
         ('Futur : X-Bus de l\'ESC', C['xbus'], '7 5')]
for i, (lab, col, dash) in enumerate(items):
    y = LY + 48 + i * 23
    wire([(LX + 16, y), (LX + 60, y)], col, 3.5, dash)
    text(LX + 72, y + 4, lab, 12)
text(LX + 14, LY + 232, '● = connexion · croisement sans point = pas de contact', 10.5, fill='#555')

RX0, RY0 = 1510, 420
rect(RX0, RY0, 360, 300, fill='#fff8e6', stroke='#c98a00', rx=8)
text(RX0 + 14, RY0 + 24, 'À respecter', 15, weight='700', fill='#8a5a00')
rules = ['Jamais plus de 3,3 V sur une broche GPIO.',
         'TX et RX CROISÉS : TX du module → RX de la carte.',
         'BEC (6 à 8,4 V) : jamais directement sur « 5V »,',
         '   toujours via le régulateur 5 V.',
         'Au banc : l\'USB alimente tout — régulateur',
         '   débranché (ne pas cumuler USB + régulateur).',
         'Masse commune entre tous les éléments.',
         'ER5C-i (page web, onglet Model) : sorties 2 et 3',
         '   en Serial TX / RX, protocole CRSF ; direction',
         '   sur la sortie 1 (CH1) ; ESC sur la sortie 4 (CH2).',
         'Sorties du récepteur : ordre physique et fils',
         '   selon la sérigraphie ; couleurs du GPS à vérifier.',
         'IMU vissée rigidement, axes alignés sur la voiture.']
for i, r in enumerate(rules):
    text(RX0 + 16, RY0 + 50 + i * 19, r, 12, fill='#333')
a('</svg>')
import os; open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'docs', 'cablage-trimbox-s3.svg'), 'w').write('\n'.join(out))
print('ok')
