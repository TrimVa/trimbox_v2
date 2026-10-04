#!/usr/bin/env python3
"""
Banc du retour arrière après mise à jour (v2 §7.2), sous émulateur.

On fabrique l'état exact que laisse une mise à jour Wi-Fi réussie :
  - version actuelle dans app0 ;
  - nouvelle version écrite dans app1 ;
  - otadata : « démarrer sur app1, image NEUVE » (ce qu'écrit
    esp_ota_set_boot_partition).
Puis on démarre l'émulateur et on observe le chargeur de démarrage :

  A. nouvelle version saine   → démarre sur app1, se valide après 15 s,
                                 et reste sur app1 au redémarrage suivant ;
  B. nouvelle version qui plante au démarrage
                               → le chargeur revient tout seul sur app0.

Usage : ota_rollback.py <merged.bin (version A dans app0)> <app saine.bin> <app qui plante.bin>
"""
import sys, os, struct, zlib, subprocess, time, socket, threading, shutil

QEMU = os.environ.get("QEMU", "/opt/esp/qemu/qemu/bin/qemu-system-xtensa")
MERGED, GOOD, CRASH = sys.argv[1:4]
APP1, OTADATA = 0x210000, 0xE000

def make_flash(path, candidate):
    img = bytearray(open(MERGED, 'rb').read())
    app = open(candidate, 'rb').read()
    img[APP1:APP1 + len(app)] = app
    # esp_ota_select_entry_t : ota_seq, seq_label[20], ota_state, crc
    seq = 2                                  # (2 - 1) % 2 = 1 → app1
    crc = zlib.crc32(struct.pack('<I', seq), 0xFFFFFFFF) & 0xFFFFFFFF   # esp_rom_crc32_le(UINT32_MAX, …)
    entry = struct.pack('<I20sII', seq, b'\xff' * 20, 0x0, crc)          # état 0x0 = ESP_OTA_IMG_NEW
    img[OTADATA:OTADATA + 0x2000] = b'\xff' * 0x2000
    img[OTADATA:OTADATA + len(entry)] = entry
    open(path, 'wb').write(img)

def boot(path, seconds):
    args = [QEMU, "-nographic", "-machine", "esp32s3", "-m", "4M", "-drive", f"file={path},if=mtd,format=raw",
            "-nic", "none", "-serial", "tcp:127.0.0.1:5580,server=on,wait=off", "-serial", "null"]
    q = subprocess.Popen(args, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)
    s = None
    for _ in range(100):
        try: s = socket.create_connection(("127.0.0.1", 5580)); break
        except OSError: time.sleep(0.05)
    s.settimeout(0.2); out = b''; t0 = time.time()
    while time.time() - t0 < seconds:
        try:
            d = s.recv(65536)
            if d: out += d
        except socket.timeout: pass
    q.terminate(); q.wait(5)
    return out.decode('utf-8', 'replace')

ok = True
def check(c, msg):
    global ok
    print(('OK    ' if c else 'ECHEC ') + msg); ok = ok and bool(c)

# --- A : version saine --------------------------------------------------------
make_flash('/tmp/ota_a.bin', GOOD)
t = boot('/tmp/ota_a.bin', 22)
check('partition app1' in t, "A : la nouvelle version démarre sur app1")
check('NOUVEAU firmware sur app1' in t, "A : elle se sait en attente de validation")
check('nouveau firmware VALIDÉ' in t, "A : validée après 15 s")
t = boot('/tmp/ota_a.bin', 6)
check('partition app1' in t and 'NOUVEAU firmware' not in t, "A : au redémarrage suivant, reste sur app1, validée")

# --- B : version qui plante ---------------------------------------------------
make_flash('/tmp/ota_b.bin', CRASH)
t = boot('/tmp/ota_b.bin', 14)
check('plantage volontaire' in t, "B : la nouvelle version plante au démarrage")
check('partition app0' in t, "B : le chargeur revient tout seul sur app0 (version précédente)")
tail = t[t.rfind('plantage volontaire'):]
check('partition app0' in tail, "B : app0 démarre APRÈS le plantage")
t = boot('/tmp/ota_b.bin', 6)
check('partition app0' in t and 'plantage' not in t, "B : et y reste aux démarrages suivants")

print('RESULTAT :', 'OK' if ok else 'ECHEC')
sys.exit(0 if ok else 1)
