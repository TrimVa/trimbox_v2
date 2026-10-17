# Génère tout le dossier hardware/pcb-support à partir de model.py et routing.json :
#   kicad/         projet KiCad 5.1 (import EasyEDA Standard / Pro)
#   fabrication/   Gerber + perçages (zip JLCPCB), BOM et CPL (assemblage)
#   apercus/       rendus PNG/SVG/PDF
# Usage : python3 build.py            (python3 do_route.py pour refaire le routage)
import json
import os
import shutil
import sys
import zipfile

import check
import gerber
import kicad5
import model
import render
import render_layers
import sch_svg

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
NAME = 'trimbox-support'


def zipdir(src, dst, arc=''):
    with zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as z:
        for base, dirs, files in os.walk(src):
            for fn in sorted(files):
                p = os.path.join(base, fn)
                z.write(p, os.path.join(arc, os.path.relpath(p, src)))


def main():
    rep, pours, R = check.run(verbose=False)
    print('DRC :', 'aucune erreur' if not rep['erreurs'] else rep['erreurs'])
    if rep['erreurs']:
        sys.exit(1)
    comps = model.build()
    nets = model.nets()
    kdir = os.path.join(ROOT, 'kicad')
    if os.path.isdir(kdir):
        shutil.rmtree(kdir)
    kicad5.write_project(kdir, NAME, comps, nets)
    labels, wires, noconns = kicad5.write_sch(os.path.join(kdir, f'{NAME}.sch'), comps, NAME)
    fdir = os.path.join(ROOT, 'fabrication')
    gdir = os.path.join(fdir, 'gerber')
    if os.path.isdir(fdir):
        shutil.rmtree(fdir)
    silk = [(t, x, y, s, L, False) for (t, x, y, s, L) in kicad5.SILK_TEXT]
    silk.append(('ESP32 de ce côté, USB-C en bas', model.GEO['W'] / 2, 4.6, 0.8, 'B.SilkS', True))
    files, drills = gerber.write_all(gdir, comps, pours, R, silk)
    zipdir(gdir, os.path.join(fdir, f'{NAME}-gerber-JLCPCB.zip'))
    groups = gerber.write_bom_cpl(fdir, comps)
    adir = os.path.join(ROOT, 'apercus')
    os.makedirs(adir, exist_ok=True)
    render.render(os.path.join(adir, 'placement-routage.png'), ratsnest=False)
    render_layers.main(os.path.join(adir, 'faces-cuivre.png'))
    sch_svg.render(os.path.join(adir, 'schema.svg'), comps, labels, wires, noconns)
    # archive à importer dans EasyEDA (Fichier → Ouvrir → KiCad)
    zipdir(kdir, os.path.join(ROOT, f'{NAME}-kicad5-pour-EasyEDA.zip'), NAME)
    json.dump({'drc': rep, 'gerber': files, 'percages': {str(k): v for k, v in drills.items()},
               'bom_lignes': len(groups)}, open(os.path.join(ROOT, 'apercus', 'rapport.json'), 'w'),
              ensure_ascii=False, indent=1)
    print('ok :', kdir, fdir, adir)


if __name__ == '__main__':
    main()
