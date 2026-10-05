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
     comparaison : portion choisie en glissant sur le profil vitesse/distance ;
     réglages : nom du véhicule et mot de passe Wi-Fi (FF F3), redémarrage ;
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
        # Pas d'Internet au bord de la piste : toute requête hors du module
        # (fond satellite, polices…) est refusée. Indispensable sur GitHub, où
        # le runner a Internet : l'ancien filtre « **/arcgisonline.com/** » ne
        # reconnaissait pas « server.arcgisonline.com », les tuiles arrivaient
        # et le message « pas d'Internet » attendu n'apparaissait jamais.
        offline = []
        def no_net(route):
            offline.append(route.request.url); route.abort()
        pg.route(re.compile(r'^https?://(?!127\.0\.0\.1[:/])'), no_net)
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
        # fonctions S3 : décidées par la version (modèle « TrimBox » depuis 2.0-a14)
        s3 = pg.evaluate("""(() => { const keep = [fwModel, fwVerAnn], out = [];
            for(const [m, v] of [['TrimBox', '2.0-a14'], ['TrimBox DIY S3', '2.0-a13'], ['TrimBox DIY', '1.4']]){
              fwModel = m; fwVerAnn = v; out.push(isS3()); }
            [fwModel, fwVerAnn] = keep; return out; })()""")
        check(s3 == [True, True, False], f'détection du firmware S3 (2.0-a14, ancien nom, v1) : {s3}')
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
        check(any('arcgisonline.com' in u for u in offline), f'tuiles satellite bien interceptées ({len(offline)} requêtes refusées)')
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

        # comparaison : glisser sur le profil vitesse/distance → portion sélectionnée
        pg.evaluate("""(() => { sessions.push(sessions[0].slice()); sessionNames.push('copie');
            cmpSel.clear(); cmpSel.add(0); cmpSel.add(1); paintSessions(); paintCompare(); })()""")
        pg.locator('#cmpChart').scroll_into_view_if_needed()
        bb = pg.locator('#cmpChart').bounding_box(); y = bb['y'] + bb['height'] / 2
        pg.mouse.move(bb['x'] + bb['width'] * 0.2, y); pg.mouse.down()
        pg.mouse.move(bb['x'] + bb['width'] * 0.6, y, steps=8); pg.mouse.up()
        pg.wait_for_timeout(150)
        sel = pg.evaluate("cmpView.sel && cmpView.sel.map(Math.round)")
        rows = pg.evaluate("[...document.querySelectorAll('#cmpSecTable tbody tr')].map(tr => [...tr.cells].map(c => c.textContent))")
        dur = [r for r in rows if r[0] == 'Durée']
        check(sel and sel[1] > sel[0] and pg.is_visible('#cmpSecBox') and dur and dur[0][1] == dur[0][2]
              and '+' not in dur[0][2], f'comparaison : portion {sel} m, durées {dur}')
        check('→' in pg.inner_text('#cmpSecRange'), 'portion affichée : ' + pg.inner_text('#cmpSecRange'))
        bb2 = pg.locator('#cmpChart').bounding_box()
        check(abs(bb2['y'] - bb['y']) < 2, f'profil resté en place pendant la sélection ({bb["y"]:.0f} → {bb2["y"]:.0f} px)')
        pg.mouse.click(bb2['x'] + bb2['width'] * 0.5, bb2['y'] + bb2['height'] / 2); pg.wait_for_timeout(150)
        check(pg.evaluate("cmpView.sel === null") and not pg.is_visible('#cmpSecBox'), 'appui sans glisser : portion effacée')

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
        # véhicule et point d'accès (FF F3) : lecture, contrôles, envoi, redémarrage
        pg.wait_for_function("document.getElementById('connText').textContent.includes('Wi-Fi')", timeout=8000)
        pg.wait_for_function("document.getElementById('fVeh').value === 'Banc PC'", timeout=5000)
        check(pg.inner_text('#secIdent h2').startswith('Véhicule') and 'origine' in pg.inner_text('#passInfo'),
              'réglages : nom du véhicule lu (FF F3) : ' + pg.input_value('#fVeh'))
        lab = pg.evaluate("document.getElementById('dNick').previousElementSibling.textContent")
        check(lab == 'Véhicule', f'fiche appareil : « {lab} » au lieu de « Pseudo »')
        check(pg.is_disabled('#btnIdent'), 'bouton inactif tant que rien ne change')
        pg.fill('#fVeh', 'Buggé'); check(pg.is_disabled('#btnIdent') and 'bad' in pg.get_attribute('#vehNames', 'class'), 'nom accentué refusé')
        pg.fill('#fVeh', 'Truggy 2'); pg.fill('#fWifiPass', 'court')
        check(pg.is_disabled('#btnIdent') and 'Trop court' in pg.inner_text('#passInfo'), 'mot de passe trop court refusé')
        pg.fill('#fWifiPass', 'piste-2026!')
        check(not pg.is_disabled('#btnIdent') and 'TrimBox-Truggy-2' in pg.inner_text('#vehNames')
              and 'TrimBox Truggy 2' in pg.inner_text('#vehNames'), 'aperçu : ' + pg.inner_text('#vehNames'))
        dlg = []
        pg.once('dialog', lambda d: (dlg.append(d.message), d.accept()))
        pg.click('#btnIdent')
        pg.wait_for_function("document.getElementById('identMsg').textContent.includes('redémarre')", timeout=5000)
        check(dlg and 'TrimBox-Truggy-2' in dlg[0], 'confirmation avant redémarrage')
        check('TrimBox-Truggy-2' in pg.inner_text('#identMsg'), 'message : ' + pg.inner_text('#identMsg')[:90])
        pg.wait_for_function("document.getElementById('connText').textContent.includes('hors ligne')", timeout=8000)
        pg.wait_for_function("document.getElementById('connText').textContent.includes('Wi-Fi')", timeout=15000)
        pg.wait_for_function("document.getElementById('dNick').textContent === 'Truggy 2' && document.getElementById('fVeh').value === 'Truggy 2' && document.getElementById('passInfo').textContent.includes('personnalisé')", timeout=8000)
        check('personnalisé' in pg.inner_text('#passInfo') and pg.input_value('#fWifiPass') == '',
              'après redémarrage : nouveau nom et mot de passe personnalisé (FF F0 / F3)')
        # mise à jour du firmware
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
        # ordinateur : onglets ; chaque onglet n'affiche que ses panneaux
        check(pg.is_visible('#tabs'), '1440 px : barre d\'onglets visible')
        vis = lambda: pg.evaluate("['secConnect','anEmpty','secImport','secRec'].map(i => document.getElementById(i).offsetParent !== null)")
        pg.click('.tab[data-tab="appareil"]'); a = vis()
        pg.click('.tab[data-tab="analyse"]');  b2 = vis()
        pg.click('.tab[data-tab="sessions"]'); c = vis()
        pg.click('.tab[data-tab="reglages"]'); d2 = vis()
        check(a == [True, False, False, False] and b2 == [False, True, False, False]
              and c == [False, False, True, False] and d2 == [False, False, False, True],
              f'onglets Appareil / Analyse / Sessions / Réglages : {a} {b2} {c} {d2}')
        for w in (1920, 2560):
            pg.set_viewport_size({'width': w, 'height': 1100}); pg.wait_for_timeout(250)
            sw = pg.evaluate('document.documentElement.scrollWidth')
            check(sw <= w, f'{w} px : toute la largeur, sans défilement horizontal ({sw})')
        pg.click('.tab[data-tab="appareil"]')
        pg.set_viewport_size({'width': 412, 'height': 900}); pg.wait_for_timeout(250)
        pg.screenshot(path=os.environ.get('SHOT', '/tmp/console_wifi.png'), full_page=False)
        check(not errs, f'aucune erreur JavaScript {errs}')
        b.close()
finally:
    dev.terminate()
print('RESULTAT :', 'OK' if ok else 'ECHEC')
sys.exit(0 if ok else 1)
