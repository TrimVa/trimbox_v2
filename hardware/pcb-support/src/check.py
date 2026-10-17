# Plans de masse, contrôle des règles (DRC) et de la continuité, au 0,01 mm.
# Indépendant du routeur : repart des formes finales (pastilles, pistes, vias).
import json
import math
import os

import numpy as np
from scipy import ndimage

import model
from geom import RULES, Shape, Raster, pad_shapes

HERE = os.path.dirname(os.path.abspath(__file__))
RES = 0.01
TOL = 0.012          # tolérance de rastérisation


def load_routing():
    d = json.load(open(os.path.join(HERE, 'routing.json')))
    model.TRACKS[:] = [tuple(t) for t in d['tracks']]
    model.VIAS[:] = [tuple(v) for v in d['vias']]


def copper_shapes(comps):
    sh = pad_shapes(comps)
    for (layer, w, net, pts) in model.TRACKS:
        L = layer[0]
        for a, b in zip(pts, pts[1:]):
            sh.append(Shape(net, (L,), 'seg', (a[0], a[1], b[0], b[1], w), 'trk'))
    for (x, y, net) in model.VIAS:
        sh.append(Shape(net, ('F', 'B'), 'circle', (x, y, RULES['via_dia'] / 2), 'via'))
    return sh


def pours(R, shapes):
    """Plans de masse F et B (masques booléens)."""
    board_in = ndimage.distance_transform_edt(R.board_mask(0.0)) * RES >= RULES['edge']
    out = {}
    for L in ('F', 'B'):
        other = R.mask([s for s in shapes if s.net not in ('GND', None)], L)
        nc = R.mask([s for s in shapes if s.net is None], L)       # pastilles sans réseau
        d = ndimage.distance_transform_edt(~(other | nc)) * RES
        # trous traversants sans réseau et vias : on garde aussi l'isolement
        area = board_in & (d >= RULES['pour_clear'])
        # suppression des isthmes plus fins que 0,2 mm (ouverture morphologique)
        r = int(round(0.10 / RES))
        st = np.zeros((2 * r + 1, 2 * r + 1), bool)
        yy, xx = np.ogrid[-r:r + 1, -r:r + 1]
        st[xx * xx + yy * yy <= r * r] = True
        area = ndimage.binary_opening(area, structure=st)
        gnd = R.mask([s for s in shapes if s.net == 'GND'], L)
        lab, n = ndimage.label(area | gnd)
        keep = np.unique(lab[gnd]); keep = keep[keep > 0]
        out[L] = np.isin(lab, keep) & area
    return out


def run(verbose=True):
    load_routing()
    comps = model.build()
    R = Raster(RES, margin=0.5)
    sh = copper_shapes(comps)
    P = pours(R, sh)
    report = {'erreurs': [], 'mesures': {}}
    nets = sorted({s.net for s in sh if s.net})
    masks = {L: {n: R.mask([s for s in sh if s.net == n], L) for n in nets} for L in ('F', 'B')}
    for L in ('F', 'B'):
        masks[L]['GND'] = masks[L]['GND'] | P[L]
    nc = {L: R.mask([s for s in sh if s.net is None], L) for L in ('F', 'B')}

    # ---------------- isolement entre réseaux
    worst = {}
    for L in ('F', 'B'):
        for n in nets:
            m = masks[L][n]
            if not m.any():
                continue
            d = ndimage.distance_transform_edt(~m) * RES
            for o in nets + ['(sans réseau)']:
                if o == n:
                    continue
                om = nc[L] if o == '(sans réseau)' else masks[L][o]
                if not om.any():
                    continue
                dm = d[om].min()
                key = (L,) + tuple(sorted((n, o)))
                worst[key] = min(worst.get(key, 9), dm)
    for k, v in sorted(worst.items(), key=lambda kv: kv[1]):
        lim = RULES['clearance'] - TOL
        if v < lim:
            report['erreurs'].append(f'isolement {k[0]}.Cu {k[1]} ↔ {k[2]} : {v:.3f} mm < {RULES["clearance"]}')
    report['mesures']['isolement_min'] = round(min(worst.values()), 3)
    report['mesures']['paires_les_plus_proches'] = [(f'{k[0]}:{k[1]}↔{k[2]}', round(v, 3))
                                                    for k, v in sorted(worst.items(), key=lambda kv: kv[1])[:6]]

    # ---------------- bord de carte
    inside = ndimage.distance_transform_edt(R.board_mask(0.0)) * RES
    for L in ('F', 'B'):
        allc = np.zeros_like(inside, bool)
        for n in nets:
            allc |= masks[L][n]
        allc |= nc[L]
        em = inside[allc].min() if allc.any() else 9
        report['mesures'][f'bord_{L}'] = round(float(em), 3)
        if em < RULES['edge'] - TOL:
            report['erreurs'].append(f'cuivre {L}.Cu à {em:.3f} mm du bord (< {RULES["edge"]})')

    # ---------------- perçages
    holes = [(x, y, RULES['via_drill'] / 2, 'via') for (x, y, _) in model.VIAS]
    for c in comps:
        for p, x, y, a in c.pads_abs():
            if p.drill:
                holes.append((x, y, p.drill / 2, f'{c.ref}.{p.num}'))
    hmin = 9
    for i in range(len(holes)):
        for j in range(i + 1, len(holes)):
            a, b = holes[i], holes[j]
            gap = math.dist(a[:2], b[:2]) - a[2] - b[2]
            hmin = min(hmin, gap)
            if gap < 0.25:
                report['erreurs'].append(f'trous trop proches {a[3]} ↔ {b[3]} : {gap:.2f} mm')
    report['mesures']['entre_trous_min'] = round(hmin, 3)

    # ---------------- continuité (deux faces reliées par vias et trous traversants)
    parent = {}

    def find(a):
        while parent.setdefault(a, a) != a:
            parent[a] = parent[parent[a]]; a = parent[a]
        return a

    def union(a, b):
        parent[find(a)] = find(b)
    labels = {}
    for L in ('F', 'B'):
        for n in nets:
            lab, k = ndimage.label(masks[L][n])
            labels[(L, n)] = lab
    for s in sh:
        if s.net and s.layers == {'F', 'B'}:
            if s.kind == 'poly':
                x = sum(q[0] for q in s.data) / len(s.data); y = sum(q[1] for q in s.data) / len(s.data)
            else:
                x, y = s.data[0], s.data[1]
            i, j = R.px(x, y)
            a = labels[('F', s.net)][int(round(j)), int(round(i))]
            b = labels[('B', s.net)][int(round(j)), int(round(i))]
            if a and b:
                union(('F', s.net, a), ('B', s.net, b))
    for n in nets:
        comps_n = set()
        for L in ('F', 'B'):
            lab = labels[(L, n)]
            for v in np.unique(lab):
                if v:
                    comps_n.add(find((L, n, int(v))))
        # composantes qui touchent une pastille du réseau (les îlots de plan sans pastille sont retirés plus haut)
        pads_n = [s for s in sh if s.net == n and s.src not in ('trk', 'via')]
        roots = set()
        for s in pads_n:
            L = 'F' if 'F' in s.layers else 'B'
            if s.kind == 'circle':
                x, y = s.data[0], s.data[1]
            else:
                x = sum(p[0] for p in s.data) / len(s.data); y = sum(p[1] for p in s.data) / len(s.data)
            i, j = R.px(x, y)
            v = labels[(L, n)][int(round(j)), int(round(i))]
            if not v:
                report['erreurs'].append(f'pastille {s.src} ({n}) hors cuivre ?')
                continue
            roots.add(find((L, n, int(v))))
        if len(roots) > 1:
            report['erreurs'].append(f'réseau {n} coupé en {len(roots)} morceaux')
        report['mesures'].setdefault('morceaux', {})[n] = len(roots)

    report['mesures']['pistes'] = len(model.TRACKS)
    report['mesures']['vias'] = len(model.VIAS)
    report['mesures']['plan_GND_F_mm2'] = round(float(P['F'].sum()) * RES * RES, 1)
    report['mesures']['plan_GND_B_mm2'] = round(float(P['B'].sum()) * RES * RES, 1)
    if verbose:
        print(json.dumps(report, ensure_ascii=False, indent=1))
    return report, P, R


if __name__ == '__main__':
    run()
