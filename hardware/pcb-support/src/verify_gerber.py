# Relit les Gerber/Excellon produits (lecteur indépendant du générateur) et
# compare le cuivre rastérisé à celui du contrôle DRC.
import glob, os, re, sys
import numpy as np
from PIL import Image, ImageDraw
import check, model

RES = 0.02
def read_gerber(path, R):
    img = Image.new('1', (R.nx, R.ny), 0); d = ImageDraw.Draw(img)
    ap = {}; cur = None; x = y = 0; region = None; H = model.GEO['H']
    def px(gx, gy): return R.px(gx, H - gy)
    for tok in re.findall(r'%[^%]*%|[^*%\n]+\*', open(path).read()):
        tok = tok.strip('%*\n')
        m = re.match(r'ADD(\d+)C,([\d.]+)', tok)
        if m: ap[int(m.group(1))] = float(m.group(2)); continue
        if re.fullmatch(r'D(\d+)', tok) and int(tok[1:]) >= 10: cur = int(tok[1:]); continue
        if tok == 'G36': region = []; continue
        if tok == 'G37':
            d.polygon([px(*p) for p in region], fill=1); region = None; continue
        m = re.fullmatch(r'X(-?\d+)Y(-?\d+)D0([123])', tok)
        if not m: continue
        nx_, ny_ = int(m.group(1)) / 1e6, int(m.group(2)) / 1e6; op = m.group(3)
        if region is not None:
            region.append((nx_, ny_))
        elif op == '3':
            r = ap[cur] / 2; a, b = px(nx_ - r, ny_ + r); e, f = px(nx_ + r, ny_ - r); d.ellipse([a, b, e, f], fill=1)
        elif op == '1':
            w = ap[cur]; d.line([px(x, y), px(nx_, ny_)], fill=1, width=max(1, int(round(w / R.res))))
            for (qx, qy) in ((x, y), (nx_, ny_)):
                a, b = px(qx - w/2, qy + w/2); e, f = px(qx + w/2, qy - w/2); d.ellipse([a, b, e, f], fill=1)
        x, y = nx_, ny_
    return np.array(img, bool)

def main():
    rep, P, Rc = check.run(verbose=False)
    from geom import Raster
    R = Raster(RES, margin=0.5)
    comps = model.build(); sh = check.copper_shapes(comps)
    gdir = os.path.join(os.path.dirname(__file__), '..', 'fabrication', 'gerber')
    ok = True
    for L, ext in (('F', 'GTL'), ('B', 'GBL')):
        g = read_gerber(glob.glob(os.path.join(gdir, f'*.{ext}'))[0], R)
        ref = R.mask([s for s in sh], L)
        pour = np.array(Image.fromarray(P[L]).resize((R.nx, R.ny), Image.NEAREST), bool) if P[L].shape != ref.shape else P[L]
        ref = ref | pour
        diff = g ^ ref
        frac = diff.sum() / max(ref.sum(), 1)
        print(f'{L}.Cu : cuivre Gerber {g.sum()*RES*RES:.1f} mm², attendu {ref.sum()*RES*RES:.1f} mm², écart {100*frac:.2f} %')
        ok &= frac < 0.03
    # perçages : chaque trou au centre d'un via ou d'une pastille traversante
    txt = open(glob.glob(os.path.join(gdir, '*.drl'))[0]).read()
    holes = [(float(a), float(b)) for a, b in re.findall(r'X([\d.]+)Y([\d.]+)', txt)]
    H = model.GEO['H']
    targets = [(x, H - y) for (x, y, n) in model.VIAS] + [(x, H - y) for c in comps for p, x, y, a in c.pads_abs() if p.drill]
    miss = [h for h in holes if min(abs(h[0]-t[0]) + abs(h[1]-t[1]) for t in targets) > 0.002]
    print(f'perçages : {len(holes)} trous, {len(miss)} mal placés')
    ok &= not miss and len(holes) == len(targets)
    print('GERBER OK' if ok else 'GERBER : ÉCART'); return ok

if __name__ == '__main__':
    sys.exit(0 if main() else 1)
