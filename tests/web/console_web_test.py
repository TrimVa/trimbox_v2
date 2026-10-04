#!/usr/bin/env python3
"""
Banc de la console embarquée : navigateur Chromium sans tête + fakedev.

Vérifie, sur la VRAIE page intégrée au firmware :
  1. servie compressée par le serveur du firmware, sans erreur JavaScript ;
  2. source « Wi-Fi » choisie et connexion WebSocket automatique ;
  3. identification, état mémoire, données en direct à ~25 Hz ;
  4. téléchargement → session → analyse avec des tours de 15,00 s ;
     lignes du module (FF F1) reprises d'office, chronos du module (0x29)
     affichés et comparés au calcul de la console ;
  5. coupure du point d'accès → reconnexion automatique ;
  6. choix de la couleur d'accent conservé (localStorage, origine du module) ;
  7. mise à jour du firmware : fichier quelconque refusé avec un message clair,
     vrai trimbox_s3-app.bin accepté, reconnexion et nouvelle version annoncée.
Usage : console_web_test.py <chemin fakedev> [port] [trimbox_s3-app.bin]
"""
import subprocess, sys, time, os, re
from playwright.sync_api import sync_playwright

FAKEDEV = sys.argv[1] if len(sys.argv) > 1 else './fakedev'
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8088
URL = f'http://127.0.0.1:{PORT}/'
FW_BIN = sys.argv[3] if len(sys.argv) > 3 else os.environ.get('FW_BIN', '')
ok = True
def check(c, msg):
    global ok
    print(('OK    ' if c else 'ECHEC ') + msg); ok = ok and bool(c)

dev = subprocess.Popen([FAKEDEV, str(PORT)], stdout=subprocess.PIPE, text=True)
time.sleep(0.4)
try:
    with sync_playwright() as p:
        b = p.chromium.launch()
        ctx = b.new_context(viewport={'width': 412, 'height': 900})
        pg = ctx.new_page()
        pg.route('**/arcgisonline.com/**', lambda r: r.abort())   # pas d'Internet au bord de la piste
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        resp = pg.goto(URL, wait_until='domcontentloaded')
        check(resp.status == 200 and resp.headers.get('content-encoding') == 'gzip', 'page servie compressée (gzip)')
        pg.wait_for_function("document.getElementById('connText').textContent.includes('Wi-Fi')", timeout=8000)
        check(True, 'connexion Wi-Fi automatique : ' + pg.inner_text('#connText').replace('\n', ' '))
        check(pg.evaluate("wifiMode && !demoMode"), 'source Wi-Fi sélectionnée')
        check(not pg.is_visible('#srcReal') and not pg.is_visible('#srcDemo'),
              'point d\'accès : ni Bluetooth ni démo proposés')
        pg.wait_for_timeout(2500)
        check('Banc PC' in pg.inner_text('#dNick'), 'identification FF F0 : ' + pg.inner_text('#dNick'))
        fwv = pg.inner_text('#fwVer')
        check(fwv.startswith('firmware 2.0-a'), f'version du firmware dans l\'en-tête : « {fwv} »')
        used = pg.inner_text('#mUsed').replace(' ', '').replace(' ', '')
        check(used == '1127', f'état mémoire : {used} points')
        # mode automatique : case présente, cochée, et bit 0x20 dans la trame
        check(pg.is_checked('#fAuto'), 'case « démarrer et arrêter tout seul » cochée')
        flags = pg.evaluate("buildConfigPayload(true)[2]")
        check(flags & 0x20, f'drapeau automatique dans la configuration (0x{flags:02X})')
        pg.uncheck('#fAuto')
        check(not (pg.evaluate("buildConfigPayload(true)[2]") & 0x20), 'case décochée : drapeau retiré')
        pg.check('#fAuto')
        opts = pg.evaluate("[...document.getElementById('fRate').options].map(o => o.textContent)")
        check(all(re.search(r'— \d{2}h\d{2}min$', o) for o in opts), f'durées au format 00h00min : {opts}')
        hz = float(re.sub(r'[^0-9.]', '', pg.inner_text('#lHz').replace('Hz', '')) or 0)
        check(20 <= hz <= 30, f'données en direct : {hz} Hz')
        spd = float(pg.inner_text('#lSpeed'))
        check(abs(spd - 44.4) < 1.0, f'vitesse affichée : {spd} km/h')

        # téléchargement
        pg.click('#btnDownload')
        pg.wait_for_function("sessions.length > 0", timeout=15000)
        n = pg.evaluate("sessions.map(s => s.length)")
        check(n and max(n) == 1125, f'téléchargement : sessions {n}')
        check(pg.evaluate("badFrames") == 0, f'aucune trame rejetée (badFrames = {pg.evaluate("badFrames")})')
        # firmware S3 : lignes posées depuis la radio, lues à la connexion (FF F1)
        ml = pg.evaluate("moduleLines && [moduleLines.mode, moduleLines.channel, +moduleLines.start.head.toFixed(1)]")
        check(ml == [1, 8, 90.0], f'lignes du module lues (FF F1) : {ml}')
        auto = pg.evaluate("[analMode, !!anal.lineB, !anal.lineA, (anal.laps || []).map(l => +l.time.toFixed(3))]")
        check(auto[0] == 'circuit' and auto[1] and auto[2] and len(auto[3]) == 2 and all(abs(t - 15) < 0.01 for t in auto[3]),
              f'ligne du module posée d\'office dans l\'analyse : {auto}')
        mod = pg.evaluate("(sessions[0].moduleEvents || []).filter(e => e.kind === 1).map(e => [e.n, e.ms])")
        check(mod == [[1, 15000], [2, 15000]], f'téléchargement avec 0x02 : tours du module {mod}')
        check(pg.is_visible('#modBox') and pg.locator('#modList .lapRow').count() == 2, 'encart « Chronos du module » affiché')
        ecarts = [int(x) for x in re.findall(r'écart ([+-]?\d+) ms', pg.inner_text('#modList'))]
        check(len(ecarts) == 2 and all(abs(x) <= 5 for x in ecarts), f'module et console d\'accord : écarts {ecarts} ms')
        # bouton : relecture explicite, puis « Réinitialiser » puis de nouveau le bouton
        pg.click('#btnClearSel')
        check(pg.evaluate("!anal.lineB"), 'lignes retirées par « Réinitialiser »')
        pg.click('#btnModLines')
        pg.wait_for_function("anal.lineB && (anal.laps || []).length === 2", timeout=5000)
        check('radio' in pg.inner_text('#anHelp'), 'bouton « Lignes du module » : ' + pg.inner_text('#anHelp'))
        # ligne posée par programme au milieu de la ligne droite → tours de 15 s
        pg.evaluate("""(() => {
            const i = anal.pts.findIndex((p, k) => k > 5 && Math.abs(p.speed - anal.pts[0].speed) < 1) + 10;
            anal.lineA = makeLine(i); anal.lineB = null; computeRuns(); })()""")
        laps = pg.evaluate("(anal.laps || []).map(l => l.time)")
        check(len(laps) >= 2 and all(abs(t - 15.0) < 0.01 for t in laps), f'tours dans la console : {laps}')

        # fond satellite sans Internet : repli sur le schéma, avec explication
        pg.evaluate("document.getElementById('btnSat').scrollIntoView()")
        pg.click('#btnSat')
        pg.wait_for_function("satOn && !document.getElementById('satNote').classList.contains('hide')", timeout=8000)
        check('Internet' in pg.inner_text('#satNote'), 'satellite sans Internet : reste actif, message affiché')
        # Internet revient (4G du téléphone) : au nouvel essai, les tuiles arrivent
        # (fournisseur remplacé par une image locale : le bac à sable n'a pas Internet).
        pg.evaluate("tileUrl = () => 'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg=='")
        pg.wait_for_function("satEverOk && document.getElementById('satNote').classList.contains('hide')", timeout=15000)
        check(True, 'satellite : tuiles reçues au nouvel essai, message retiré')
        pg.click('#btnSat')                          # retour à la vue schématique
        # pastilles lisibles quel que soit le thème : texte blanc sur fond sombre
        col = pg.evaluate("getComputedStyle(document.getElementById('tapeCap')).color")
        check(col == 'rgb(255, 255, 255)', f'« % occupé » en blanc ({col})')
        # voiture posée : cadrage d'au moins 25 m (le bruit GPS n'est pas agrandi)
        span = pg.evaluate("""(() => {
            const pts = []; for(let i=0;i<200;i++) pts.push({lat:45.919278+Math.sin(i)*1e-5, lon:-1.335968+Math.cos(i*1.3)*1e-5});
            const v = fitView(pts, 900, 620);
            return Math.min(900, 620) / v.K * EARTH_C * Math.cos(45.92*Math.PI/180); })()""")
        check(span >= 25, f'cadrage minimal : {span:.1f} m')
        # sans IMU (0 g partout), pas de « temps en l'air »
        air = pg.evaluate("gStats(Array.from({length:500}, (_, i) => ({lat:45.9, lon:-1.3, speed:0.2, alt:10, ax:0, ay:0, az:0, fix:3, sats:12, t:i*0.04}))).air")
        check(air == 0, f'temps en l\'air sans IMU : {air} s')
        air2 = pg.evaluate("gStats(Array.from({length:500}, (_, i) => ({lat:45.9, lon:-1.3, speed:0.2, alt:10, ax:0.01, ay:0, az:0, fix:3, sats:12, t:i*0.04}))).air")
        check(air2 > 19, f'… mais chute libre réelle toujours comptée : {air2:.2f} s')

        # coupure du point d'accès puis retour
        open(f'/tmp/fakedev_drop_{PORT}', 'w').close()
        pg.wait_for_function("document.getElementById('connText').textContent.includes('hors ligne')", timeout=5000)
        check(True, 'coupure détectée')
        check(pg.inner_text('#fwVer') == '', 'version du firmware effacée hors connexion')
        pg.wait_for_function("document.getElementById('connText').textContent.includes('Wi-Fi')", timeout=8000)
        check(True, 'reconnexion automatique')

        # couleur d'accent : mémorisée pour l'origine du module
        pg.click('#accentToggle'); pg.click('.swatch[data-hex="#00b4d8"]')
        pg.reload(wait_until='domcontentloaded'); pg.wait_for_timeout(800)
        check(pg.evaluate("getComputedStyle(document.documentElement).getPropertyValue('--violet').trim()") == '#00b4d8',
              'couleur d\'accent conservée après rechargement')
        # mise à jour du firmware
        pg.wait_for_function("document.getElementById('connText').textContent.includes('Wi-Fi')", timeout=8000)
        check(pg.is_visible('#secFw'), 'section « Mise à jour du firmware » visible en Wi-Fi')
        junk = '/tmp/pas_un_firmware.bin'; open(junk, 'wb').write(os.urandom(300000))
        pg.set_input_files('#fwFile', junk); pg.click('#btnFw')
        pg.wait_for_function("document.getElementById('fwMsg').textContent.startsWith('Refusé')", timeout=15000)
        check(True, 'fichier quelconque refusé : ' + pg.inner_text('#fwMsg'))
        if FW_BIN:
            pg.set_input_files('#fwFile', FW_BIN); pg.click('#btnFw')
            pg.wait_for_function("document.getElementById('fwMsg').textContent.includes('Nouvelle version active')", timeout=30000)
            check(True, 'vrai firmware accepté, module revenu : ' + pg.inner_text('#fwMsg')[:80])
        else:
            print('(FW_BIN absent : envoi d\'un vrai firmware non testé)')
        r = pg.request.get(URL + 'update')
        check(r.status == 200 and 'Mise à jour du firmware' in r.text(), 'page de secours /update')
        # mise en page adaptative (1.7.13) : aucun défilement horizontal du
        # petit téléphone au grand écran, deux colonnes sur ordinateur
        for w in (320, 360, 412, 768, 1024, 1440):
            pg.set_viewport_size({'width': w, 'height': 900}); pg.wait_for_timeout(250)
            sw = pg.evaluate('document.documentElement.scrollWidth')
            check(sw <= w, f'{w} px : pas de défilement horizontal (largeur {sw})')
        cols = pg.evaluate("getComputedStyle(document.querySelector('.wrap')).gridTemplateColumns.split(' ').length")
        check(cols == 2, f'1440 px : deux colonnes ({cols})')
        pg.set_viewport_size({'width': 412, 'height': 900}); pg.wait_for_timeout(250)
        pg.screenshot(path=os.environ.get('SHOT', '/tmp/console_wifi.png'), full_page=False)
        check(not errs, f'aucune erreur JavaScript {errs}')
        b.close()
finally:
    dev.terminate()
print('RESULTAT :', 'OK' if ok else 'ECHEC')
sys.exit(0 if ok else 1)
