# Rendu des deux faces cuivre (plans de masse compris), d'après check.run().
import sys

import numpy as np
from PIL import Image

import check
import model
from geom import Raster


def main(out):
    rep, P, R = check.run(verbose=False)
    comps = model.build()
    sh = check.copper_shapes(comps)
    imgs = []
    for L, colc, colp in (('F', (200, 60, 40), (235, 150, 130)), ('B', (40, 90, 200), (140, 170, 235))):
        cu = R.mask([s for s in sh if s.net], L) | R.mask([s for s in sh if s.net is None], L)
        a = np.full(cu.shape + (3,), (18, 70, 40), np.uint8)          # vernis vert
        a[P[L]] = colp
        a[cu] = colc
        # perçages
        hol = R.blank()
        from geom import Shape, RULES
        for (x, y, n) in model.VIAS:
            R.draw(hol, Shape(None, 'F', 'circle', (x, y, RULES['via_drill'] / 2)))
        for c in comps:
            for p, x, y, ang in c.pads_abs():
                if p.drill:
                    R.draw(hol, Shape(None, 'F', 'circle', (x, y, p.drill / 2)))
        a[np.array(hol, bool)] = (250, 250, 250)
        if L == 'B':
            a = a[:, ::-1]       # face B vue de son côté
        imgs.append(Image.fromarray(a))
    w, h = imgs[0].size
    out_img = Image.new('RGB', (2 * w + 40, h), (255, 255, 255))
    out_img.paste(imgs[0], (0, 0)); out_img.paste(imgs[1], (w + 40, 0))
    out_img = out_img.resize((out_img.width // 4, out_img.height // 4), Image.LANCZOS)
    out_img.save(out)
    print(rep['erreurs'])


if __name__ == '__main__':
    main(sys.argv[1])
