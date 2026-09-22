#!/usr/bin/env python3
"""
Banc d'intégration sous émulateur (QEMU esp32s3) — sans aucun matériel.

  QEMU n'émule que UART0 et UART1 de l'ESP32-S3 ; la variante émulateur du
  firmware (compilée avec -DTRIMBOX_QEMU, voir .github/workflows/build-s3.yml) les répartit ainsi :
  UART0 : journal du firmware + faux GNSS u-blox (répond aux VALSET, émet
          des NAV-PVT à 25 Hz) — les trames UBX sont extraites du flux texte
  UART1 : faux récepteur ExpressLRS (voies CRSF à 50 Hz, lit la télémétrie)

Scénario : la voiture attend 33 s au stand (le point d'accès Wi-Fi doit
s'allumer seul après 30 s d'arrêt), puis tourne sur un circuit en « stade »
(tour de 12,000 s) : le Wi-Fi doit se couper dès qu'elle roule. On pose la
ligne avec l'inter CH8 en roulant, et on vérifie que la radio reçoit bien des
tours de 12,000 s.
Avec --lua : la ligne n'est plus posée par un inter simulé mais par le VRAI
script trmbox.lua (API EdgeTX simulée, tests/lua/radio_bridge.lua) : appui
long sur « Poser DEPART » → GV9 → voie 8 → module ; le script reçoit en
retour l'accusé et les tours, et annonce les temps.
Usage : sim.py <image_flash_16Mo> [--reboot] [--lua]
"""
import socket, struct, subprocess, sys, time, math, threading, os, re, shutil

QEMU = os.environ.get("QEMU", "/opt/esp/qemu/qemu/bin/qemu-system-xtensa")
IMG = sys.argv[1] if len(sys.argv) > 1 else ""
PORTS = (5550, 5551)
LAP_S = 12.0
STILL_S = 33.0          # attente au stand avant de rouler

def fletcher(b):
    a = c = 0
    for x in b:
        a = (a + x) & 0xFF; c = (c + a) & 0xFF
    return bytes([a, c])

def ubx(cls, i, payload):
    h = bytes([cls, i]) + struct.pack('<H', len(payload)) + payload
    return b'\xb5\x62' + h + fletcher(h)

def crc8(b):
    c = 0
    for x in b:
        c ^= x
        for _ in range(8):
            c = ((c << 1) ^ 0xD5) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c

def crsf_channels(ch):
    bits = 0; n = 0; out = bytearray()
    for v in ch:
        bits |= (v & 0x7FF) << n; n += 11
        while n >= 8:
            out.append(bits & 0xFF); bits >>= 8; n -= 8
    body = bytes([0x16]) + bytes(out)
    return bytes([0xC8, len(body) + 1]) + body + bytes([crc8(body)])

# --- trajectoire -------------------------------------------------------------
R, S = 20.0, 30.0
PER = 2*S + 2*math.pi*R
V = PER / LAP_S
LAT0, LON0 = 45.0, 5.0
def pos(t):
    s = (V * t) % PER
    if s < S: x, y, h = s, 0.0, 90.0
    elif s - S < math.pi*R:
        a = (s - S)/R; x, y, h = S + R*math.sin(a), R - R*math.cos(a), 90 - math.degrees(a)
    elif s - S - math.pi*R < S:
        x, y, h = S - (s - S - math.pi*R), 2*R, 270.0
    else:
        a = (s - 2*S - math.pi*R)/R; x, y, h = -R*math.sin(a), R + R*math.cos(a), 270 - math.degrees(a)
    lat = LAT0 + y/110540.0
    lon = LON0 + x/(111320.0*math.cos(math.radians(LAT0)))
    return lat, lon, h % 360

def nav_pvt(itow, t):
    moving = t >= STILL_S
    lat, lon, h = pos(max(0.0, t - STILL_S))
    p = bytearray(92)
    struct.pack_into('<IHBBBBBB', p, 0, itow, 2026, 9, 20, 12, 0, 0, 0x37)
    p[20] = 3; p[21] = 0x01; p[23] = 14
    struct.pack_into('<ii', p, 24, round(lon*1e7), round(lat*1e7))
    struct.pack_into('<iiII', p, 32, 200000, 200000, 400, 600)
    struct.pack_into('<i', p, 60, round(V*1000) if moving else 0)
    struct.pack_into('<i', p, 64, round(h*1e5))
    struct.pack_into('<H', p, 76, 110)
    return ubx(0x01, 0x07, bytes(p))

# --- émulateur ---------------------------------------------------------------
def start_qemu():
    args = [QEMU, "-nographic", "-machine", "esp32s3", "-m", "4M",
            "-drive", f"file={IMG},if=mtd,format=raw", "-nic", "none"]
    for p in PORTS:
        args += ["-serial", f"tcp:127.0.0.1:{p},server=on,wait=off"]
    return subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)

def connect(port):
    for _ in range(100):
        try:
            s = socket.create_connection(("127.0.0.1", port)); s.setblocking(False); return s
        except OSError: time.sleep(0.05)
    raise RuntimeError(f"port {port} injoignable")

log = []
def reader(sock, sink):
    buf = b''
    while running:
        try:
            d = sock.recv(4096)
            if d: sink(d)
        except BlockingIOError: time.sleep(0.005)
        except OSError: break

HERE = os.path.dirname(os.path.abspath(__file__))
def start_radio():
    return subprocess.Popen(["lua5.2", os.path.join(HERE, "..", "lua", "radio_bridge.lua"),
                             os.path.join(HERE, "..", "..", "lua", "trmbox.lua")],
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)

def run(duration, pose_at=None, lua=False):
    global running
    q = start_qemu()
    time.sleep(0.3)
    s0, s1 = (connect(p) for p in PORTS)
    running = True
    text = bytearray(); acks = [0]; raw0 = bytearray()

    def on_uart0(d):
        # Flux mixte : texte du journal + trames UBX émises vers le GNSS.
        raw0.extend(d)
        while True:
            i = raw0.find(b'\xb5\x62')
            if i < 0:
                keep = 1 if raw0.endswith(b'\xb5') else 0
                text.extend(raw0[:len(raw0)-keep]); del raw0[:len(raw0)-keep]; break
            text.extend(raw0[:i]); del raw0[:i]
            if len(raw0) < 8: break
            ln = struct.unpack_from('<H', raw0, 4)[0]
            if len(raw0) < 8 + ln: break
            cls, idd = raw0[2], raw0[3]
            del raw0[:8+ln]
            if cls == 0x06 and idd == 0x8A:
                s0.send(ubx(0x05, 0x01, bytes([0x06, 0x8A]))); acks[0] += 1
    threading.Thread(target=reader, args=(s0, on_uart0), daemon=True).start()

    # Faux ExpressLRS : comme le vrai récepteur, il ne garde que le DERNIER
    # message « mode de vol » reçu et n'en transmet à la radio qu'un toutes
    # les 330 ms (débit de télémétrie limité). Un message écrasé avant son
    # tour est perdu : c'est ce que le firmware doit éviter.
    fm = []; gps = [0]; crx = bytearray(); fm_slot = [None]
    radio = start_radio() if lua else None
    gv9 = [0]; says = []; screens = []
    if radio:
        def radio_out():
            for line in radio.stdout:
                if line.startswith("GV "): gv9[0] = int(line[3:])
                elif line.startswith("SAY "): says.append(int(line[4:]))
                elif line.startswith("SCR "): screens.append(line[4:].strip())
        threading.Thread(target=radio_out, daemon=True).start()
    def elrs_downlink():
        while running:
            time.sleep(0.33)
            if fm_slot[0] is not None:
                fm.append(fm_slot[0])
                if radio: radio.stdin.write("FM " + fm_slot[0] + "\n")
                fm_slot[0] = None
    def on_crsf(d):
        crx.extend(d)
        while len(crx) >= 4:
            if crx[0] != 0xC8: del crx[0]; continue
            ln = crx[1]
            if len(crx) < ln + 2: break
            body = bytes(crx[2:ln+1]); ck = crx[ln+1]; del crx[:ln+2]
            if crc8(body) != ck: continue
            if body[0] == 0x21: fm_slot[0] = body[1:].split(b'\0')[0].decode()
            if body[0] == 0x02: gps[0] += 1
    threading.Thread(target=reader, args=(s1, on_crsf), daemon=True).start()
    threading.Thread(target=elrs_downlink, daemon=True).start()

    t0 = time.time(); next_pvt = next_rc = t0; itow0 = 300000000
    stream_from = None
    while time.time() - t0 < duration:
        now = time.time()
        if stream_from is None and acks[0] >= 5:
            stream_from = now; next_pvt = now
        if stream_from and now >= next_pvt:
            k = round((now - stream_from) / 0.04)
            s0.send(nav_pvt(itow0 + k*40, k*0.04))
            next_pvt += 0.04
        if now >= next_rc:
            ch = [992]*16
            if radio:
                # Mixage de la MT12 : CH8 = MAX × GV9 % (GV9 = 100 → +100 %)
                ch[7] = 992 + round(gv9[0] * 8.19)
                radio.stdin.write("T 2\n")
                if pose_at and stream_from and now - stream_from >= pose_at and not getattr(run, 'posed', False):
                    run.posed = True
                    for ev in ("NEXT", "NEXT", "ENTER_LONG"):   # page Lignes, « Poser DEPART », appui long
                        radio.stdin.write("EV " + ev + "\n")
            elif pose_at and stream_from and pose_at <= now - stream_from < pose_at + 0.7: ch[7] = 1811
            s1.send(crsf_channels(ch)); next_rc += 0.02
        time.sleep(0.002)
    running = False
    q.terminate(); q.wait(5)
    if radio:
        radio.stdin.write("Q\n"); radio.stdin.close(); radio.wait(5)
        run.lua_result = (says, screens)
    return text.decode('utf-8', 'replace'), fm, gps[0], acks[0]

if __name__ == "__main__":
    reboot = "--reboot" in sys.argv
    lua = "--lua" in sys.argv
    if not reboot:
        txt, fm, ngps, acks = run(STILL_S + 52, pose_at=STILL_S + 4.0, lua=lua)
    else:
        txt, fm, ngps, acks = run(14)
    print(txt)
    print("=== télémétrie radio : %d trames GPS, %d VALSET acquittés" % (ngps, acks))
    seen = []
    for m in fm:
        if not seen or seen[-1] != m: seen.append(m)
    print("=== messages texte reçus (dédoublonnés) :", seen)
    laps = [m for m in seen if re.match(r'^L\d+ ', m)]
    # « L2 12.000+0.00 » : temps du tour puis écart au meilleur
    print("=== tours :", laps)
    ok = True
    if not reboot:
        ok &= any(m == "K DEPART OK" for m in seen)
        times = [float(re.match(r'^L\d+ (\d+\.\d{3})', m).group(1)) for m in laps]
        nums = sorted({int(re.match(r'^L(\d+)', m).group(1)) for m in laps})
        print("=== tours reçus par la radio :", nums, "(aucun ne doit manquer)")
        ok &= len(nums) >= 2 and nums == list(range(1, nums[-1] + 1))
        ok &= all(abs(x - LAP_S) <= 0.005 for x in times)
        on = "[wifi] allumé (arrêt prolongé)" in txt
        off = "[wifi] éteint (la voiture roule)" in txt
        print("=== Wi-Fi : allumé à l'arrêt :", on, "| coupé en roulant :", off,
              "| radio :", [m for m in seen if m.startswith('W ')])
        ok &= on and off and "W WIFI ON" in seen and "W WIFI OFF" in seen
    else:
        ok &= "session précédente interrompue" in txt and "départ posé" in txt
    if lua and not reboot:
        says, screens = run.lua_result
        print("=== script Lua : annonces", says, "| écran :", screens[-1] if screens else "—")
        ok &= "[ligne] départ posée" in txt
        ok &= len(says) >= 2 and all(v == round(LAP_S * 100) for v in says)
    print("RESULTAT :", "OK" if ok else "ECHEC")
    sys.exit(0 if ok else 1)
