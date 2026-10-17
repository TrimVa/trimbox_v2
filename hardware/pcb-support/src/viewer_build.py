# Assemble le visualiseur autonome apercus/visualiseur.html (données embarquées).
import json
import os

import viewer_data

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'apercus', 'visualiseur.html')

if __name__ == '__main__':
    d = viewer_data.build()
    tpl = open(os.path.join(HERE, 'viewer_template.html'), encoding='utf-8').read()
    html = tpl.replace('/*DATA*/', json.dumps(d, ensure_ascii=False, separators=(',', ':')))
    open(OUT, 'w', encoding='utf-8').write(html)
    print(OUT, len(html) // 1024, 'Kio')
