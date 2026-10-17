# Rendu SVG du schéma (mêmes données que le fichier .sch KiCad).
import html

import kicad5
import model

K = 0.1          # px par mil


def esc(t):
    return html.escape(str(t))


def render(path, comps, labels, wires, noconns):
    defs = kicad5.symbol_defs()
    W, H = 11693 * K, 8268 * K
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.0f}" height="{H:.0f}" viewBox="0 0 {W:.0f} {H:.0f}" '
         'font-family="DejaVu Sans, Arial, sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#fffdf8"/>',
         f'<rect x="20" y="20" width="{W - 40}" height="{H - 40}" fill="none" stroke="#8a2d2d" stroke-width="1"/>',
         f'<text x="40" y="52" font-size="20" font-weight="700" fill="#5a1414">{esc(kicad5.TITLE)}</text>',
         f'<text x="40" y="74" font-size="11" fill="#7a4a4a">Rév. {kicad5.REV} — {kicad5.DATE} — schéma généré depuis '
         'hardware/pcb-support/src/model.py</text>']
    # blocs
    for (x, y, w, h, t) in ((80, 90, 260, 300, 'Connecteurs'), (360, 90, 520, 230, 'Alimentation 5 V (TPS563201)'),
                            (220, 400, 470, 300, 'Centrale inertielle (LSM6DS3TR-C, I2C 0x6A)'),
                            (860, 330, 260, 300, 'Carte ESP32-S3 Mini Nano')):
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="none" stroke="#c9a" stroke-dasharray="6 4"/>')
        o.append(f'<text x="{x + 8}" y="{y + 16}" font-size="11" fill="#a06" font-weight="700">{esc(t)}</text>')
    for c in comps:
        prefix, draw, pins, descr = defs[c.sym]
        X, Y = kicad5.SCH_POS[c.ref]

        def P(u, v):
            return ((X + u) * K, (Y - v) * K)
        for ln in draw:
            t = ln.split()
            if t[0] == 'S':
                (a, b), (e, f) = P(int(t[1]), int(t[2])), P(int(t[3]), int(t[4]))
                o.append(f'<rect x="{min(a, e):.1f}" y="{min(b, f):.1f}" width="{abs(e - a):.1f}" height="{abs(f - b):.1f}" '
                         f'fill="{"#fff7d6" if t[-1] == "f" else "none"}" stroke="#8a2d2d" stroke-width="1.4"/>')
            elif t[0] == 'P':
                n = int(t[1]); pts = [P(int(t[5 + 2 * k]), int(t[6 + 2 * k])) for k in range(n)]
                fill = '#8a2d2d' if t[-1] == 'F' else 'none'
                o.append('<polyline points="' + ' '.join(f'{a:.1f},{b:.1f}' for a, b in pts) +
                         f'" fill="{fill}" stroke="#8a2d2d" stroke-width="1.4"/>')
            elif t[0] == 'A':
                cx, cy = P(int(t[1]), int(t[2])); r = int(t[3]) * K
                o.append(f'<path d="M{cx - r:.1f},{cy:.1f} A{r:.1f},{r:.1f} 0 0 1 {cx + r:.1f},{cy:.1f}" fill="none" '
                         'stroke="#8a2d2d" stroke-width="1.4"/>')
        for (nm, num, x, y, ori, typ, ln) in pins:
            dx, dy = {'R': (1, 0), 'L': (-1, 0), 'U': (0, 1), 'D': (0, -1)}[ori]
            a, b = P(x, y); e, f = P(x + dx * ln, y + dy * ln)
            o.append(f'<line x1="{a:.1f}" y1="{b:.1f}" x2="{e:.1f}" y2="{f:.1f}" stroke="#8a2d2d" stroke-width="1.2"/>')
            two = c.sym in ('R', 'C', 'L', 'D')
            if not two and not c.sym.startswith('CONN'):
                if ori in ('R', 'L'):
                    tx = e + (4 if ori == 'R' else -4)
                    anc = 'start' if ori == 'R' else 'end'
                    o.append(f'<text x="{tx:.1f}" y="{f + 3.5:.1f}" font-size="8.5" text-anchor="{anc}" fill="#00507a">{esc(nm)}</text>')
                    o.append(f'<text x="{(a + e) / 2:.1f}" y="{b - 2:.1f}" font-size="7" text-anchor="middle" fill="#8a2d2d">{esc(num)}</text>')
                else:
                    o.append(f'<text x="{e:.1f}" y="{f - 3:.1f}" font-size="8" text-anchor="middle" fill="#00507a">{esc(nm)}</text>')
        two = c.sym in ('R', 'C', 'L', 'D')
        top = max(p[3] for p in pins)
        bot = min(p[3] for p in pins)
        rx, ry = P(0, (top + (60 if not two else 50)) if not two else 60)
        if c.sym.startswith('CONN'):
            ry -= 8
        o.append(f'<text x="{rx:.1f}" y="{ry - (2 if not two else 4):.1f}" font-size="10" font-weight="700" text-anchor="middle" fill="#333">{esc(c.ref)}</text>')
        vx, vy = P(0, (bot - 60) if not two else -60)
        if c.sym.startswith('CONN'):
            vx, vy = P(-250, 0); vy -= 10
        o.append(f'<text x="{vx:.1f}" y="{vy + 10:.1f}" font-size="9" text-anchor="middle" fill="#555">{esc(c.value)}</text>')
    for (x1, y1, x2, y2) in wires:
        o.append(f'<line x1="{x1 * K:.1f}" y1="{y1 * K:.1f}" x2="{x2 * K:.1f}" y2="{y2 * K:.1f}" stroke="#2a7a2a" stroke-width="1.2"/>')
    for (x, y, spin, net) in labels:
        anc = {0: 'start', 2: 'end', 1: 'start', 3: 'start'}[spin]
        if spin in (1, 3):
            rot = -90 if spin == 1 else 90
            o.append(f'<text transform="translate({x * K:.1f},{y * K:.1f}) rotate({rot})" x="2" y="-2" font-size="8.5" '
                     f'fill="#1d5a1d" font-weight="700">{esc(net)}</text>')
        else:
            dx = 2 if spin == 0 else -2
            o.append(f'<text x="{x * K + dx:.1f}" y="{y * K - 2:.1f}" font-size="8.5" text-anchor="{anc}" fill="#1d5a1d" '
                     f'font-weight="700">{esc(net)}</text>')
    for (x, y) in noconns:
        a, b = x * K, y * K
        o.append(f'<path d="M{a - 3},{b - 3} L{a + 3},{b + 3} M{a - 3},{b + 3} L{a + 3},{b - 3}" stroke="#2a2ab0" stroke-width="1.2"/>')
    notes = ['PCB support à la taille de la carte ESP32 : composants sur cette face, ESP32 de l\'autre côté (picots 2,54 mm).',
             'J1 : 1 masse · 2 +BEC (6-8,4 V) · 3 vers RX du récepteur (IO15) · 4 depuis TX du récepteur (IO16) · 5 masse.',
             'J2 : 1 masse · 2 +5 V · 3 depuis TX du GPS (IO18) · 4 vers RX du GPS (IO17) — ordre G V T R du HGLRC M100 Mini.',
             'Régulateur : Vout = 0,768 × (1 + R1/R2) = 5,07 V ; D1 empêche l\'USB de la carte d\'alimenter le BEC.',
             'IMU : adresse I2C 0x6A (SA0 à la masse), CS au 3,3 V (mode I2C), INT1 sur IO7.',
             'Brochage de la carte ESP32 (côté IO) : hypothèse de sérigraphie A, à confirmer (voir LISEZMOI).']
    for k, t in enumerate(notes):
        o.append(f'<text x="70" y="{712 + k * 15}" font-size="10.5" fill="#444">{esc(t)}</text>')
    o.append('</svg>')
    open(path, 'w', encoding='utf-8').write('\n'.join(o))
