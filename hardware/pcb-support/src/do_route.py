# Lance le routage et enregistre le résultat dans routing.json.
import json
import os
import sys
import time

import model
import render
from route import Router

HERE = os.path.dirname(os.path.abspath(__file__))
ORDER = ['SW', 'V5_BUCK', 'BST', 'FB',
         'ELRS_RX', 'ELRS_TX', 'GPS_TX', 'GPS_RX',
         'SDA', 'SCL', 'IMU_INT1', '+3V3',
         'VBEC', '+5V']


def main(out_png=None):
    comps = model.build()
    r = Router(comps)
    t0 = time.time()
    fails = {}
    # masses de l'IMU d'abord (pastilles au pas de 0,5 mm, faciles à enfermer)
    f = r.stitch_gnd([s.src for s in r.shapes if s.net == 'GND' and s.src.startswith('U2.')])
    print('GND IMU', 'ÉCHEC ' + str(f) if f else 'ok')
    for net in ORDER:
        f = r.route_net(net)
        print(f'{net:10s} {len(r.tracks):3d} pistes {len(r.vias):2d} vias  {time.time() - t0:5.1f} s', 'ÉCHEC ' + str(f) if f else '')
        if f:
            fails[net] = f
    gnd_smd = [s.src for s in r.shapes if s.net == 'GND' and s.layers == {'F'} and s.src not in ('trk', 'via')]
    f = r.stitch_gnd(gnd_smd)
    print(f'GND        {len(r.tracks):3d} pistes {len(r.vias):2d} vias  {time.time() - t0:5.1f} s', 'ÉCHEC ' + str(f) if f else '')
    if f:
        fails['GND'] = f
    json.dump({'tracks': r.tracks, 'vias': r.vias}, open(os.path.join(HERE, 'routing.json'), 'w'), indent=1)
    model.TRACKS[:] = r.tracks; model.VIAS[:] = r.vias
    if out_png:
        render.render(out_png, ratsnest=False)
    return fails


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else None)
