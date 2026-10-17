# Routeur maillé (A*) deux couches pour le PCB support.
# Grille 0,05 mm ; obstacles = cuivre des autres réseaux agrandi de
# (isolement + demi-largeur + marge de grille) ; vias aux changements de couche.
import heapq
import math

import numpy as np
from scipy import ndimage

import model
from geom import RULES, WIDTH, Shape, Raster, pad_shapes

RES = 0.05
MARGIN = 0.06           # erreur de rastérisation tolérée (vérifiée ensuite au 0,01 mm)
VIA_COST = 50           # ≈ 2,5 mm de piste
B_COST = 5.0            # la face B (côté ESP32) sert de plan de masse : passages courts seulement
LAYERS = ('F', 'B')
# Largeur imposée pour la liaison vers certaines pastilles (reliées en dernier).
PAD_WIDTH = {'R1.2': 0.25, 'U2.5': 0.15, 'U2.8': 0.15, 'U2.12': 0.15}
KMAX = 24               # pas de grille minimum entre deux vias d'un même tracé (1,2 mm)
DIRS = [(1, 0, 1.0), (-1, 0, 1.0), (0, 1, 1.0), (0, -1, 1.0),
        (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]


class Router:
    def __init__(self, comps):
        self.R = Raster(RES, margin=1.0)
        self.shapes = pad_shapes(comps)          # tout le cuivre posé (pastilles + pistes + vias)
        self.tracks, self.vias = [], []
        edge = ndimage.distance_transform_edt(self.R.board_mask(0.0)) * RES
        self.edge = edge

    # ---------------------------------------------------------------- masques
    def free(self, net, layer, half):
        """Cases où l'axe d'une piste (demi-largeur `half`) du réseau `net` peut passer."""
        other = self.R.mask([s for s in self.shapes if s.net != net], layer)
        d = ndimage.distance_transform_edt(~other) * RES
        ok = (d >= RULES['clearance'] + half + MARGIN) & (self.edge >= RULES['edge'] + half + MARGIN)
        return ok

    def own(self, net, layer, names=None):
        sel = [s for s in self.shapes if s.net == net and (names is None or s.src in names)]
        return self.R.mask(sel, layer)

    # ---------------------------------------------------------------- A*
    def astar(self, net, w, starts, goals, goal_any_b=False):
        half = w / 2
        free = {L: self.free(net, L, half) for L in LAYERS}
        vr = RULES['via_dia'] / 2
        via_ok = (self.free(net, 'F', vr) & self.free(net, 'B', vr))
        # trous : au moins 0,5 mm entre perçages (vias et trous traversants, tous réseaux)
        holes = self.R.mask([s for s in self.shapes if s.src == 'via' or s.layers == {'F', 'B'}], 'F')
        dh = ndimage.distance_transform_edt(~holes) * RES
        via_ok &= dh >= vr + 0.5 + MARGIN
        nx, ny = self.R.nx, self.R.ny
        INF = float('inf')
        dist = {}
        heap = []
        gi = gj = None
        if goals is not None:
            gidx = np.argwhere(goals['F'] | goals['B'])
            gcx, gcy = gidx[:, 1].mean(), gidx[:, 0].mean()
        for li, L in enumerate(LAYERS):
            idx = np.argwhere(starts[L] & free[L])
            for (j, i) in idx:
                k = (li, int(i), int(j), KMAX)
                dist[k] = 0.0
                h = 0.0 if goals is None else math.hypot(i - gcx, j - gcy) * 0.5
                heapq.heappush(heap, (h, 0.0, k))
        prev = {}
        while heap:
            f, g, k = heapq.heappop(heap)
            if g > dist.get(k, INF):
                continue
            li, i, j, kv = k
            L = LAYERS[li]
            if (goal_any_b and li == 1) or (goals is not None and goals[L][j, i]):
                path = [k]
                while path[-1] in prev:
                    path.append(prev[path[-1]])
                return [(a, b, c) for (a, b, c, _) in path[::-1]]
            mult = B_COST if li == 1 else 1.0
            kn = min(KMAX, kv + 1)
            for di, dj, c in DIRS:
                ii, jj = i + di, j + dj
                if not (0 <= ii < nx and 0 <= jj < ny) or not free[L][jj, ii]:
                    continue
                if di and dj and not (free[L][j, ii] and free[L][jj, i]):
                    continue      # pas de coupe d'angle entre deux obstacles
                kk = (li, ii, jj, kn)
                ng = g + c * mult
                if ng < dist.get(kk, INF):
                    dist[kk] = ng; prev[kk] = k
                    h = 0.0 if goals is None else math.hypot(ii - gcx, jj - gcy) * 0.9
                    heapq.heappush(heap, (ng + h, ng, kk))
            if via_ok[j, i] and kv >= KMAX:
                kk = (1 - li, i, j, 0)
                if free[LAYERS[1 - li]][j, i]:
                    ng = g + VIA_COST
                    if ng < dist.get(kk, INF):
                        dist[kk] = ng; prev[kk] = k
                        h = 0.0 if goals is None else math.hypot(i - gcx, j - gcy) * 0.9
                        heapq.heappush(heap, (ng + h, ng, kk))
        return None

    # ---------------------------------------------------------------- pose
    def commit(self, net, w, path):
        """Chemin de cases → segments simplifiés + vias, ajoutés au cuivre."""
        pts, cur = [], None
        segs = []

        def flush():
            if len(pts) >= 2:
                simp = [pts[0]]
                for a, b in zip(pts[1:], pts[2:] + [None]):
                    if b is None:
                        simp.append(a); break
                    d1 = (a[0] - simp[-1][0], a[1] - simp[-1][1]); d2 = (b[0] - a[0], b[1] - a[1])
                    if d1[0] * d2[1] - d1[1] * d2[0] != 0:
                        simp.append(a)
                segs.append((LAYERS[cur], simp))
        for (li, i, j) in path:
            if cur is not None and li != cur:
                flush()
                x, y = self.R.xy(i, j)
                self.vias.append((round(x, 3), round(y, 3), net))
                self.shapes.append(Shape(net, LAYERS, 'circle', (x, y, RULES['via_dia'] / 2), 'via'))
                pts = []
            cur = li
            pts.append((i, j))
        flush()
        for L, simp in segs:
            xy = [tuple(round(v, 3) for v in self.R.xy(i, j)) for (i, j) in simp]
            if len(xy) == 1:
                continue
            self.tracks.append((L + '.Cu', w, net, xy))
            for a, b in zip(xy, xy[1:]):
                self.shapes.append(Shape(net, (L,), 'seg', (a[0], a[1], b[0], b[1], w), 'trk'))

    def route_net(self, net, order=None):
        w = WIDTH.get(net, 0.2)
        pads = [s for s in self.shapes if s.net == net and s.src not in ('via', 'trk')]
        if order:
            pads.sort(key=lambda s: order.index(s.src) if s.src in order else 99)
        done = [pads[0]]
        rest = pads[1:]
        late = [s for s in rest if s.src in PAD_WIDTH]
        rest = [s for s in rest if s.src not in PAD_WIDTH]
        fails = []
        while rest or late:
            if not rest:
                rest = late; late = []
            # pastille la plus proche du réseau déjà relié
            def dmin(s):
                bx = s.bbox(); cx, cy = (bx[0] + bx[2]) / 2, (bx[1] + bx[3]) / 2
                return min(math.hypot(cx - (d.bbox()[0] + d.bbox()[2]) / 2, cy - (d.bbox()[1] + d.bbox()[3]) / 2)
                           for d in done)
            if not order:
                rest.sort(key=dmin)
            tgt = rest.pop(0)
            starts = {L: self.R.mask([s for s in self.shapes if s.net == net and s is not tgt and
                                      (s in done or s.src in ('via', 'trk'))], L) for L in LAYERS}
            goals = {L: self.R.mask([tgt], L) for L in LAYERS}
            wc = PAD_WIDTH.get(tgt.src, w)
            path = self.astar(net, wc, starts, goals)
            if path is None:
                fails.append(tgt.src)
            else:
                self.commit(net, wc, path)
            done.append(tgt)
        return fails

    def stitch_gnd(self, names):
        """Relie chaque pastille CMS de masse au plan de la face B : par un via
        nouveau, ou en rejoignant un via / trou de masse existant."""
        fails = []
        for s in [s for s in self.shapes if s.net == 'GND' and s.src in names]:
            starts = {'F': self.R.mask([s], 'F'), 'B': np.zeros((self.R.ny, self.R.nx), bool)}
            exist = [q for q in self.shapes if q.net == 'GND' and q.layers == {'F', 'B'}]
            goals = {'F': self.R.mask(exist, 'F'), 'B': np.ones((self.R.ny, self.R.nx), bool)}
            path = self.astar('GND', WIDTH['GND'] if 'U2' not in s.src else 0.2, starts, goals)
            if path is None:
                fails.append(s.src)
            else:
                self.commit('GND', WIDTH['GND'] if 'U2' not in s.src else 0.2, path)
        return fails
