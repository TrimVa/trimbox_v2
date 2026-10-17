# Relit les fichiers KiCad produits et compare leurs listes de connexions au modèle.
import math, os, re, sys
import model
from sexp import parse, find, find_all

K = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'kicad')

def model_netlist():
    nl = {}
    for c in model.build():
        for pad, net in c.nets.items():
            nl.setdefault(net, set()).add((c.ref, pad))
    return {n: frozenset(v) for n, v in nl.items()}

def pcb_netlist():
    t = parse(open(os.path.join(K, 'trimbox-support.kicad_pcb'), encoding='utf-8').read())
    nl = {}; npad = 0
    for m in find_all(t, 'module'):
        ref = [x for x in find_all(m, 'fp_text') if x[1] == 'reference'][0][2]
        for p in find_all(m, 'pad'):
            npad += 1
            n = find(p, 'net')
            if n: nl.setdefault(n[2], set()).add((ref, p[1]))
    segs = len(find_all(t, 'segment')); vias = len(find_all(t, 'via')); zones = len(find_all(t, 'zone'))
    return {n: frozenset(v) for n, v in nl.items()}, npad, segs, vias, zones

def sch_netlist():
    lib = open(os.path.join(K, 'trimbox-support-cache.lib'), encoding='utf-8').read()
    syms = {}
    for blk in re.findall(r'DEF (\S+) .*?ENDDEF', lib, re.S):
        pass
    for m in re.finditer(r'DEF (\S+) (.*?)ENDDEF', lib, re.S):
        syms[m.group(1)] = [(a[2], int(a[3]), int(a[4])) for a in
                            (l.split() for l in m.group(2).splitlines() if l.startswith('X '))]
    sch = open(os.path.join(K, 'trimbox-support.sch'), encoding='utf-8').read()
    pins = []   # (x, y, ref, num)
    for m in re.finditer(r'\$Comp\nL TrimBox:(\S+) (\S+)\n.*?\nP (-?\d+) (-?\d+)\n', sch, re.S):
        name, ref, X, Y = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
        for (num, x, y) in syms['TrimBox_' + name]:
            pins.append((X + x, Y - y, ref, num))
    wires = [tuple(map(int, w.split())) for w in re.findall(r'Wire Wire Line\n\t([^\n]+)', sch)]
    labels = [(int(a), int(b), n) for a, b, n in re.findall(r'Text Label (-?\d+) (-?\d+) \d+ +\d+ +~ 0\n(\S+)', sch)]
    # chaque fil relie une extrémité de broche à une étiquette
    pt_net = {(x, y): n for (x, y, n) in labels}
    nl = {}
    for (x1, y1, x2, y2) in wires:
        n = pt_net.get((x2, y2)) or pt_net.get((x1, y1))
        for (px, py, ref, num) in pins:
            if (px, py) in ((x1, y1), (x2, y2)):
                nl.setdefault(n, set()).add((ref, num))
    nc = len(re.findall(r'NoConn ~', sch))
    return {n: frozenset(v) for n, v in nl.items()}, nc

M = model_netlist()
Pn, npad, segs, vias, zones = pcb_netlist()
S, nc = sch_netlist()
ok = True
for name, X in (('PCB', Pn), ('schéma', S)):
    if X != M:
        ok = False
        for n in sorted(set(M) | set(X)):
            if M.get(n) != X.get(n):
                print(name, 'diffère sur', n, sorted(M.get(n, ())), sorted(X.get(n, ())))
    else:
        print(f'{name} : {len(X)} réseaux identiques au modèle')
print(f'PCB : {npad} pastilles, {segs} segments, {vias} vias, {zones} zones ; schéma : {nc} broches non connectées')
print('KICAD OK' if ok else 'KICAD : ÉCART'); sys.exit(0 if ok else 1)
