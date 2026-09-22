# TrimBox DIY S3 — firmware 2.0-a4

Première version du firmware ESP32-S3. Spécification : `CAHIER-DES-CHARGES.md`
(v1) et `CAHIER-DES-CHARGES-V2.md`.

## Ce qui est fait, ce qui ne l'est pas

| Fonction | État |
|---|---|
| GNSS M10 : détection de vitesse, configuration, NAV-PVT 25 Hz | ✅ testé sous émulateur |
| Enregistrement en flash (1 h 43 à 25 Hz), filtres arrêt / sans fix / extinction | ✅ testé (natif + émulateur) |
| Coupure d'alimentation : config A/B, pas de reprise, journal intact | ✅ testé sous émulateur |
| Bluetooth, protocole console v1 (état, config, téléchargement, effacement, FF F0) | ⚠️ compilé, **non testé** (pas de Bluetooth dans l'émulateur) |
| IMU LSM6DS3 / LSM6DS3TR-C, ±16 g, moyenne par période GNSS | ⚠️ compilé, non testé |
| CRSF : télémétrie GPS + messages texte vers la MT12 | ✅ testé sous émulateur |
| Pose de ligne par la voie 8 (inter ou script Lua) | ✅ testé sous émulateur |
| Chronométrage embarqué (tours, dragster, chronos intermédiaires) | ✅ identique à la console à 1 ms (banc n°7) |
| Point d'accès Wi-Fi automatique (arrêt 30 s → allumé, roule → coupé) | ✅ logique testée sous émulateur ; radio Wi-Fi non testée |
| Console embarquée (v1.7.3, thèmes et couleur d'accent compris) + WebSocket | ✅ testée dans Chromium avec le vrai serveur du firmware |
| Mise à jour du firmware par Wi-Fi (OTA) + retour arrière automatique | ✅ envoi testé dans Chromium ; retour arrière testé sous émulateur avec le vrai chargeur de démarrage ; radio Wi-Fi non testée |
| ESC XC-E8 (X-Bus) | ❌ en attente des captures |
| Script Lua MT12 (`lua/trmbox.lua`) | ✅ testé avec l'API EdgeTX simulée, et en chaîne complète avec le firmware sous émulateur ; pas encore sur la vraie radio |

## Câblage de banc (alimentation par l'USB de la carte)

| Module | Broche module | ESP32-S3-DevKitC-1 |
|---|---|---|
| GPS M100 Mini | VCC | **5V** |
| | GND | GND |
| | TX | **GPIO 18** |
| | RX | **GPIO 17** |
| IMU LSM6DS3 | VCC | **3V3** |
| | GND | GND |
| | SDA | **GPIO 8** |
| | SCL | **GPIO 9** |
| Récepteur ER5C-i | sortie 2 (Serial TX) | **GPIO 16** |
| | sortie 3 (Serial RX) | **GPIO 15** |
| | − | GND |

Le fil + du récepteur reste sur le BEC de l'ESC (ou sur le 5V de la carte au
banc). **Rien au-dessus de 3,3 V sur une broche GPIO.**

`GPIO 10` (GPS_EN) ne sert que si un transistor coupe l'alimentation du GPS.
Sans lui, le GPS reste alimenté en veille : c'est sans conséquence.

Branchez l'USB sur le port marqué **USB** (et non **UART**) : le journal du
firmware passe par là.

## Premier flashage

Il se fait **une fois, depuis un ordinateur** :

1. Télécharger l'artefact `firmware-s3-complet` de la compilation GitHub.
2. Ouvrir <https://espressif.github.io/esptool-js/> dans Chrome ou Edge sur
   ordinateur, brancher la carte (port **USB**), *Connect*.
3. Adresse **0x0**, fichier `trimbox_s3-complet.bin`, *Program*.

Si la carte n'est pas détectée : maintenir **BOOT**, appuyer sur **RESET**,
relâcher **BOOT**, puis *Connect*.

⚠️ Le fichier complet **efface la mémoire d'enregistrement**. Ensuite, toutes
les mises à jour se font **par Wi-Fi, depuis le téléphone** (ci-dessous).

## Mettre à jour le firmware par Wi-Fi (sans PC)

1. Sur le téléphone : onglet **Actions** du dépôt GitHub → dernière
   compilation réussie → artefact **firmware-s3-app** → `trimbox_s3-app.bin`.
2. Voiture **arrêtée**, enregistrement **arrêté**. Attendre le Wi-Fi du
   module (30 s) et ouvrir la console (http://192.168.4.1/).
3. Section **Mise à jour du firmware** → choisir le fichier → **Envoyer au
   module**. Environ 10 s. La radio affiche `U REDEMARRAGE`.
4. Le module redémarre, la console se reconnecte seule et affiche
   « ✓ Nouvelle version active : …, compilée le … ». Comparez avec l'heure de
   la compilation GitHub.
5. La nouvelle version se **valide seule après 15 s** de fonctionnement
   (`U MAJ OK` sur la radio).

**Filets de sécurité :**
- Le module refuse, avec un message clair : un fichier qui n'est pas un
  firmware TrimBox S3 (le « complet », un .uf2 de la v1, un autre projet…),
  une image corrompue, un envoi pendant un enregistrement ou en roulant.
- Tant que rien n'est accepté, **rien n'est modifié** : la mise à jour
  s'écrit dans la seconde moitié de la mémoire programme, l'ancienne version
  reste intacte.
- Si la nouvelle version plante, se fige plus de 5 s ou ne lit plus la
  mémoire avant sa validation, **le module revient tout seul à la version
  précédente** au redémarrage suivant. La console le signale : « ✗ Le module
  tourne toujours sur la version compilée le … ».
- Page de secours sans la console : **http://192.168.4.1/update**.
- Ne **jamais** modifier `partitions.csv` dans une mise à jour Wi-Fi : la
  table de partitions ne se change que par câble.

## Console par Wi-Fi (sans Bluetooth, fonctionne aussi sur iPhone)

1. Voiture **arrêtée depuis 30 s** (ou juste allumée) : le module ouvre le
   réseau **`TrimBox-Buggy-1`**, mot de passe **`trimbox-rc`** (à changer dans
   `src/config.h`, `WIFI_PASS`). La MT12 affiche `W WIFI ON`.
2. Connectez le téléphone à ce réseau. Il propose en général d'ouvrir la page
   tout seul (portail captif) ; sinon, ouvrez **http://192.168.4.1/**.
3. La console se connecte seule, source **Wi-Fi**. Téléchargement nettement
   plus rapide qu'en Bluetooth.
4. Dès que la voiture roule (plus de 7 km/h), le point d'accès se **coupe**
   (`W WIFI OFF`) pour ne pas gêner la radio 2,4 GHz. Quand elle s'arrête à
   nouveau 30 s, il revient, et la console se **reconnecte d'elle-même**.

Forcer : **appui long (3 s) sur BOOT** ou touche `w` du moniteur série
(marche / arrêt). Le téléphone dira « pas d'accès Internet » : c'est normal,
restez connecté. Le fond satellite n'est pas disponible dans ce mode.

Si la console affiche une ancienne version : la page n'est pas mise en cache
(`Cache-Control: no-cache`), rechargez simplement.

## Réglages radio

**Récepteur ER5C-i** (interface web du récepteur, onglet *Model*) :
sorties 2 et 3 en **Serial TX / Serial RX**, protocole **CRSF** ;
direction sur la sortie 1 (CH1), ESC sur la sortie 4 (**CH2**).

**MT12** : installer le script `lua/trmbox.lua` et le mixage de CH8
(voir `lua/LISEZMOI.md`). Sans le script, un inter sur **CH8** suffit
(3 positions de préférence) :
Haut 0,5 s = ligne de départ. Bas 0,5 à 3 s = ligne d'arrivée.
Bas 3 s = effacement. La voiture doit rouler à plus de 7 km/h.
Puis *Télémétrie → Découvrir les capteurs* : GPS, vitesse, satellites et
**FM** (messages du chrono) apparaissent.

## Moniteur série (115 200 bauds)

| Touche | Effet |
|---|---|
| `s` | arrêt d'urgence de l'enregistrement |
| `r` | démarrer l'enregistrement (essai sans téléphone) |
| `w` | point d'accès Wi-Fi : marche / arrêt |

`b` indique aussi la partition en cours (`app0` / `app1`) et « EN VALIDATION » juste après une mise à jour.
| `i` | état : mémoire, enregistrement, configuration, lignes |
| `b` | **banc d'essai** : cadence GNSS, IMU, CRSF, Bluetooth |
| `z` | configuration par défaut (données conservées) |
| `?` | aide |

## Bancs d'essai à faire en recevant le matériel

Dans l'ordre (v2 §9). Envoyez-moi la sortie de `b` à chaque étape.

1. **Carte seule** : le journal affiche la version, `mémoire : 0 / 154333`.
   Le GPS et l'IMU sont signalés absents : normal.
2. **+ GPS**, dehors : `[gnss] module trouvé`, 4 × `ACK`, puis `b` affiche
   **25,0 Hz**. Si Galileo est coupé automatiquement, le noter.
3. **+ IMU** : `[imu] 0x6A à l'adresse 0x6A… OK` (ou 0x69 / 0x6B).
4. **+ récepteur** : `b` affiche `CRSF 150 trames/s` (ou le débit choisi),
   `liaison OK`, puis la MT12 découvre les capteurs.
5. **Console** (téléphone, <https://…github.io/…/>) : connexion, état,
   démarrage, arrêt, téléchargement, effacement.
6. **Roulage** : poser la ligne à l'inter, vérifier les tours sur la radio,
   puis comparer avec la console après téléchargement.

## Tests sans matériel

```bash
make -C tests/native                      # logique : 900+ vérifications
node tests/native/console_check.js index.html tests/native/lap_cases.json
python3 tools/embed_console.py             # après toute modification d'index.html
g++ -std=c++17 -o tests/web/fakedev tests/web/fakedev.cpp trimbox_s3/src/core/*.cpp -lm
python3 tests/web/console_web_test.py tests/web/fakedev   # console dans Chromium
python3 tests/qemu/sim.py flash.bin       # émulateur (voir le workflow)
python3 tests/qemu/ota_rollback.py merged.bin app.bin app_qui_plante.bin
```

La compilation GitHub (`.github/workflows/build-s3.yml`) enchaîne le tout.
