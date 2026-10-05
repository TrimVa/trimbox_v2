# TrimBox — Cahier des charges

**Version du document :** 3.0 — 4 octobre 2026
**Cible :** firmware **2.0-a14** (ESP32-S3), console web **1.7.17**, script radio
`trmbox.lua` **2.3**.
**Remplace :** les anciens `CAHIER-DES-CHARGES.md` (v1, carte XIAO nRF52840) et
`CAHIER-DES-CHARGES-V2.md` (v2). Ce document est **autonome** : il ne décrit que
la version ESP32-S3 et se suffit à lui-même. La carte XIAO n'est plus suivie.
Les anciens renvois « v1 §x » et « v2 §y » encore présents dans des historiques
se convertissent avec l'annexe A.

---

## 0. Comment utiliser ce document

Ordre de lecture pour un assistant de code (ou un humain qui reprend le projet) :

1. **§1 à §3** : contexte, matériel, protocole. Tout en dépend.
2. **§12 (pièges connus)** : à fournir **systématiquement**, quel que soit le
   module travaillé. Chaque point a coûté au moins un cycle de débogage.
3. Le chapitre du module concerné : §4 firmware, §5 radio CRSF, §6 Wi-Fi et
   Bluetooth, §7 mise à jour, §8 console, §9 script Lua, §10 compilation.

> ⚠️ **Les valeurs numériques ne sont pas indicatives.** Chaque seuil, offset et
> constante a été fixé par mesure ou par contrainte matérielle. Les modifier
> sans raison casse le système, souvent sans bruit (données fausses plutôt que
> plantage visible).

**Règles dures** (rappelées en §8.13, §9 et §12) :

- Console : ne **jamais** modifier `distM`, `feed`, `anal.pts` / `anal.scr`, ni
  la chaîne de filtrage (`despike`, `despikeSpeed`, `plausible`, `gStats`,
  `pathLength`). Valider toute modification en **exécutant** la page
  (`tests/web`), pas par une simple vérification de syntaxe.
- Console : incrémenter `CONSOLE_VER` à **chaque** modification, puis
  `python3 tools/embed_console.py`.
- Script Lua : **jamais** `table.*` ni `ipairs` (§12.22).
- Firmware : `trimbox_s3/partitions.csv` ne change **jamais** dans une mise à
  jour par Wi-Fi (§7).
- Le module ne pilote **jamais** la voiture (§12.9).
- Langue du projet : français, commentaires du code compris.

Les éléments marqués **[À VALIDER]** n'ont pas encore été vérifiés sur le
matériel : le banc correspondant (§11.2) doit passer avant de bâtir dessus.

---

## 1. Contexte et objectifs

### 1.1 Objet

TrimBox est un **enregistreur de télémétrie GPS (25 Hz) et inertielle
pour voitures radiocommandées**, construit autour d'un **ESP32-S3**. Il :

- enregistre de façon autonome sur sa flash interne de 16 Mo, sans téléphone ;
- **chronomètre les tours dans le module**, à 25 Hz, avec des lignes posées
  **depuis la radio** ;
- envoie le chrono, l'état et la position à la radio **RadioMaster MT12**
  (télémétrie CRSF / ExpressLRS), qui affiche et **annonce les temps à voix
  haute** grâce à un script Lua ;
- se pilote et s'analyse depuis une **console web**, par Bluetooth (page
  GitHub) ou par le **Wi-Fi du module** (console intégrée au firmware, iPhone
  compris, sans Internet) ;
- se met à jour **par Wi-Fi depuis un téléphone**, avec retour automatique à la
  version précédente si la nouvelle échoue.

### 1.2 Livrables

| Livrable | Emplacement | Rôle |
|---|---|---|
| Firmware | `trimbox_s3/` (croquis Arduino) | Acquisition, stockage, chrono, CRSF, Bluetooth, Wi-Fi, OTA |
| Console web | `index.html` | Pilotage, téléchargement, analyse, mise à jour |
| Script radio | `lua/trmbox.lua` | Écran de télémétrie de la MT12, annonces, pose de ligne |
| Chaîne de compilation | `.github/workflows/build-s3.yml` | Tests et compilation en ligne, sans PC |
| Documentation matérielle | `docs/` | Schéma de câblage, boîtier imprimé 3D |
| Outils | `tools/` | Intégration de la console, génération du schéma et du boîtier, voix, sondes |
| Bancs de test | `tests/` | Natif (g++), navigateur, Lua, émulateur QEMU |

Fichiers annexes : `trimbox-sw.js` (service worker, optionnel),
`trimbox-diy-console-demo.html` (redirection des anciens liens vers la démo),
`trimbox-etat-projet.json` (état du projet pour reprendre avec une IA).

### 1.3 Contraintes fondamentales

- **Console sans dépendance** : un seul fichier HTML, sans CDN, sans framework.
  Elle doit fonctionner au bord d'une piste sans réseau. Seule exception
  assumée : les tuiles satellite.
- **Développement sans PC** : édition, compilation et récupération du firmware
  depuis un téléphone (GitHub Actions). Le PC ne sert qu'au **premier** flash.
- **Robustesse à la coupure d'alimentation** : une batterie arrachée en pleine
  session ne corrompt ni les données ni la configuration (§4.5).
- **La radio de commande passe avant tout** : le module ne pilote rien, et ses
  émetteurs 2,4 GHz (Wi-Fi, Bluetooth) se taisent quand la voiture roule (§6).
- **Pas de compatibilité RaceBox** : le *format de trame* en est hérité (§3.1),
  aucune contrainte applicative n'est maintenue.

### 1.4 Hors périmètre

- Pilotage des servos ou de l'ESC par le module : **interdit** (§12.9).
- Télémétrie de l'ESC XC-E8 : impossible, sa prise X-Bus est une entrée (§13).
- Application mobile native, cloud, comptes utilisateurs.

### 1.5 État d'avancement (4 octobre 2026)

| Sujet | État |
|---|---|
| Firmware 2.0-a14 | Écrit, testé sur PC, dans un navigateur et sous émulateur ; compile (1,26 Mo, 62 % de la partition) |
| GPS sur la carte | ✅ fix 3D, 13 satellites, ~20 Hz |
| IMU | ✅ capteur validé avec `tools/imu_test` (\|a\| = 0,99 g) ; à revérifier dans le firmware complet |
| Script Lua sur la MT12 | ✅ affichage, tours, annonces vocales, accusés de pose ; ✅ mixage CH8 configuré |
| Bluetooth et Wi-Fi réels, roulage | **[À VALIDER]** bancs §11.2 |
| Télémétrie de l'ESC | ❌ abandonnée (§13) |

---

## 2. Matériel

### 2.1 Nomenclature

| Élément | Référence | Remarque |
|---|---|---|
| Carte | **ESP32-S3-DevKitC-1, module WROOM-1-N16R8** | 16 Mo de flash, 8 Mo de PSRAM octale. Deux prises USB-C : **« USB »** (natif, à utiliser) et « UART » (pont série) |
| Centrale inertielle | **LSM6DS3 / LSM6DS3TR-C**, module générique I2C | Adresse **0x6A ou 0x6B** (broche SDO/SA0), détection automatique. Identifiants acceptés : 0x69, 0x6A, 0x6B, 0x6C. **±16 g** obligatoire (§2.5). Alternative ±32 g : LSM6DSO32 (§12.17) |
| GNSS | **HGLRC M100 Mini** (u-blox M10) | Alimenté en 5 V, signaux 3,3 V. Ne garde pas ses réglages : configuration **renvoyée à chaque démarrage** (§4.8) |
| Récepteur | **RadioMaster ER5C-i** (ExpressLRS 2,4 GHz) | Sorties 2 et 3 en CRSF série (§5.1) |
| Radio | **RadioMaster MT12** (EdgeTX) | Script `trmbox.lua` (§9) |
| ESC | **XC-E8** | **Non relié** au module (§13). Son BEC alimente le récepteur et le régulateur |
| Alimentation | Régulateur abaisseur **5 V, ≥ 1 A** (ex. Pololu D24V10F5) | Alimenté par le BEC (6,0 à 8,4 V) |

### 2.2 Alimentation

- Le module est alimenté par le **BEC de l'ESC** (6,0 à 8,4 V) via un
  **régulateur abaisseur 5 V**, qui alimente la carte (broche 5V) et le GNSS.
  Le régulateur 3,3 V de la carte alimente l'IMU.
- Le BEC ne va **jamais** directement sur la broche 5V de la carte (§12.18).
- Au banc, l'USB de la carte alimente tout ; ne pas cumuler USB et régulateur
  sans vérifier le schéma de la carte (retour de courant vers l'USB).
- Conséquence acceptée : le module ne fonctionne que batterie de la voiture
  branchée. L'octet batterie du message de données (offset 67) vaut **toujours
  0** ; la tension de la batterie de propulsion s'affiche **sur la radio**,
  par le capteur de tension du récepteur (§9).
- `GPS_EN` (GPIO 10) peut commander le 5 V du GNSS par un interrupteur de
  charge ; sans lui, le GPS reste alimenté, sans conséquence.

### 2.3 Brochage

```
Fonction        GPIO   Périphérique       Remarque
GNSS RX         18     UART1              ← TX du module GNSS
GNSS TX         17     UART1              → RX du module GNSS
GPS_EN          10     sortie             Commande d'alimentation du GNSS (facultative)
CRSF RX         16     UART2  420000 b    ← TX du récepteur (voies)
CRSF TX         15     UART2  420000 b    → RX du récepteur (télémétrie)
IMU SDA          8     I2C0
IMU SCL          9     I2C0
IMU INT1         7     entrée             (réservée)
LED          48 / 38   sortie             DEL RGB : GPIO 48 sur DevKitC-1 v1.0, 38 sur v1.1
BOUTON           0     entrée             Bouton BOOT (appui long : Wi-Fi, §6.2)
(libres)       5, 6    —                  ex-X-Bus, objectif abandonné (§13)
Journal série   USB    CDC natif          115 200 bauds, prise « USB »
```

**Broches interdites** (§12.11) : 35, 36, 37 (PSRAM octale), 19 et 20 (USB),
0, 3, 45, 46 en sortie (lues au démarrage). Entrées analogiques :
**exclusivement l'ADC1** (GPIO 1 à 10), l'ADC2 étant inutilisable Wi-Fi actif.

Tout signal de niveau inconnu est présumé à 5 V : l'ESP32-S3 n'est **pas**
tolérant au 5 V (§12.14). TX et RX se **croisent** (TX du module → RX de la
carte). Masse commune entre tous les éléments.

Schéma : `docs/cablage-trimbox-s3.svg` / `.pdf`, régénéré par
`tools/gen_cablage.py`.

### 2.4 Mémoire flash et partitions

Table de partitions `trimbox_s3/partitions.csv` (**ne jamais la modifier** dans
une mise à jour Wi-Fi : elle n'est réécrite que par un flash câblé) :

```
# Nom     Type  Sous-type  Adresse    Taille
nvs       data  nvs        0x9000     0x5000
otadata   data  ota        0xE000     0x2000
app0      app   ota_0      0x10000    0x200000    2 Mo
app1      app   ota_1      0x210000   0x200000    2 Mo
cfg       data  0x40       0x410000   0x4000      config A/B + lignes A/B (4 × 4 Ko)
log       data  0x41       0x414000   0xBEC000    journal, 12 500 992 octets
```

- Capacité : `12 500 992 / 81` = **154 333 emplacements**.
- Autonomie : **01h42min à 25 Hz**, 02h08min à 20 Hz, 04h17min à 10 Hz,
  08h34min à 5 Hz, 42h52min à 1 Hz (durées arrondies à la minute inférieure,
  format `00h00min`).
- Les événements de chrono (type `0x29`) prennent un emplacement par
  franchissement : négligeable.
- Firmware actuel : 1,26 Mo sur 2 Mo. Au-delà de **1,8 Mo**, le signaler plutôt
  que de réduire le journal en silence.

### 2.5 Limites physiques

- **Accéléromètre : ±16 g obligatoire.** À ±8 g, les atterrissages de saut
  saturent les trois axes à 7,99 g en même temps, avec des valeurs qui
  *semblent* plausibles (§12.8).
- **Gyroscope** : ±2000 °/s.
- **Précision GPS** : 30 cm à 1 m. Suffisant pour le chrono et les vitesses,
  insuffisant pour comparer des trajectoires au centimètre.
- **Bruit à l'arrêt** : une voiture posée « dérive » de 1 à 3 m (§4.10).

---

## 3. Protocole console

### 3.1 Origine et statut

Le format de trame dérive de la documentation du protocole BLE RaceBox
(révision 8). Il est **conservé tel quel** car la console s'appuie dessus, avec
des extensions maison (`FF F0` à `FF F2`, `FF 28` / `FF 29`). Aucune
compatibilité avec l'application RaceBox officielle n'est recherchée.

### 3.2 Transports

| Transport | Disponibilité | Notes |
|---|---|---|
| **Bluetooth** (service Nordic UART) | Voiture à l'arrêt (§6.3) | Console GitHub Pages, Chrome Android ou ordinateur |
| **WebSocket** `ws://192.168.4.1/ws` | Point d'accès Wi-Fi allumé (§6.2) | **Mêmes trames binaires** `B5 62…`, une trame ou un fragment par message. Prioritaire sur le Bluetooth |

Service Nordic UART :

```
Service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX       6E400002-B5A3-F393-E0A9-E50E24DCCA9E   écriture       (console → module)
TX       6E400003-B5A3-F393-E0A9-E50E24DCCA9E   notification   (module → console)
```

Service Device Information (`0x180A`) : modèle `2A24` (`TrimBox`), série
`2A25` (dérivée de l'adresse MAC), firmware `2A26`, matériel `2A27`, fabricant
`2A29`. **Le nom annoncé commence par `TrimBox`** : c'est le filtre de la
console. MTU demandé : 247. Écritures acceptées jusqu'à 512 octets (§12.2).

**Une seule console à la fois** reçoit le flux : quand un WebSocket est ouvert,
la file d'émission unique (§4.2) part vers lui ; un nouveau WebSocket remplace
l'ancien.

### 3.3 Structure de trame

```
 Octet  0     1     2       3     4-5            6...      n-2   n-1
       0xB5  0x62  classe  id    longueur LE    charge    ckA   ckB
```

Classe : toujours `0xFF`. Somme de contrôle **Fletcher-8** sur classe, id,
longueur et charge (pas sur les octets de synchronisation) :

```c
ckA = ckB = 0;
pour chaque octet o de [classe, id, len_lo, len_hi, charge...] :
    ckA = (ckA + o) & 0xFF;
    ckB = (ckB + ckA) & 0xFF;
```

Tous les entiers du protocole sont en **little-endian** (à l'inverse du CRSF,
§12.12).

### 3.4 Table des messages

| Classe/ID | Sens | Long. | Rôle |
|---|---|---|---|
| `FF 01` | → console | 80 | Données en direct (§3.5) |
| `FF 02` | → console | 2 | ACK : classe, id acquittés |
| `FF 03` | → console | 2 | NACK |
| `FF 21` | → console | 80 | Enregistrement mémoire (même format que `FF 01`) |
| `FF 22` | ↔ | 0 / 12 | État mémoire (§3.7) |
| `FF 23` | ↔ | 0 / 1 / 4 | Téléchargement (§3.12) |
| `FF 24` | ↔ | 0 / 1 | Effacement : démarrer / progression en % |
| `FF 25` | ↔ | 0 / 12 | Configuration d'enregistrement (§3.6) |
| `FF 26` | → console | 12 | Changement d'état d'enregistrement |
| `FF 27` | ↔ | 0 / 3 | Configuration du récepteur GNSS |
| `FF 28` | → console | 80 | **Réservé** (lot ESC, §13) : jamais émis |
| `FF 29` | → console | 80 | Événement de chrono (§3.11), en téléchargement seulement |
| `FF 30` | ↔ | 4 / 0 | Déverrouillage mémoire |
| `FF F0` | ↔ | 0 / var. | Identification du firmware (§3.8) |
| `FF F1` | ↔ | 0 / 28 | Lignes du module (§3.9) |
| `FF F2` | ↔ | 0 / 1 → 4 | Wi-Fi (§3.10) |

Longueurs attendues par la console (table `EXPECT_LEN`, §8.3) :
`{0x01:80, 0x02:2, 0x03:2, 0x21:80, 0x22:12, 0x26:12, 0x28:80, 0x29:80}`.

### 3.5 Message de données (80 octets)

```
 0  u32  iTOW (ms)              40  u32  précision horizontale (mm)
 4  u16  année                  44  u32  précision verticale (mm)
 6  u8   mois                   48  i32  vitesse sol (mm/s)
 7  u8   jour                   52  i32  cap (deg × 1e5)
 8  u8   heure                  56  u32  précision vitesse (mm/s)
 9  u8   minute                 60  u32  précision cap
10  u8   seconde                64  u16  pDOP (× 100)
11  u8   drapeaux validité      66  u8   drapeaux lat/lon
12  u32  précision temps (ns)   67  u8   batterie : toujours 0 (§2.2)
16  i32  nanosecondes           68  i16  accél. X (milli-g)
20  u8   type de fix            70  i16  accél. Y (milli-g)
21  u8   drapeaux de fix        72  i16  accél. Z (milli-g)
22  u8   drapeaux date/heure    74  i16  gyro X (centi-deg/s)
23  u8   nombre de satellites   76  i16  gyro Y (centi-deg/s)
24  i32  longitude (deg × 1e7)  78  i16  gyro Z (centi-deg/s)
28  i32  latitude (deg × 1e7)
32  i32  altitude WGS84 (mm)
36  i32  altitude MSL (mm)
```

### 3.6 Configuration d'enregistrement `FF 25` (12 octets)

```
0  u8   activation (0 = arrêt, 1 = marche)
1  u8   cadence : 0=25 Hz  1=10 Hz  2=5 Hz  3=1 Hz  4=20 Hz
2  u8   drapeaux de filtre
3  u8   réservé
4  u16  seuil de vitesse « à l'arrêt » (mm/s)
6  u16  délai avant pause / fermeture à l'arrêt (s)
8  u16  délai avant pause sans fix (s)
10 u16  délai avant extinction automatique (s)
```

| Bit | Valeur | Filtre |
|---|---|---|
| 0 | `0x01` | Attendre un fix 3D avant de stocker |
| 1 | `0x02` | Suspendre à l'arrêt |
| 2 | `0x04` | Suspendre sans signal GPS |
| 3 | `0x08` | Éteindre après inactivité |
| 4 | `0x10` | Ne pas éteindre avant d'avoir enregistré au moins un point |
| 5 | `0x20` | **Démarrer et arrêter tout seul** (au roulage, §4.6) |

Valeurs par défaut : 25 Hz, `flags = 0x3F` (tout actif, automatique compris),
seuil 1389 mm/s (5 km/h), 30 s, 30 s, 300 s.

### 3.7 État mémoire `FF 22` (réponse, 12 octets)

```
0  u8   enregistrement en cours (0/1)
1  u8   taux de remplissage (%)
2  u8   drapeaux de sécurité (bit 0 = mémoire verrouillée)
3  u8   réservé
4  u32  emplacements utilisés
8  u32  capacité totale (154 333)
```

La console calcule les durées affichées à partir de cette capacité **réelle**,
jamais de valeurs écrites en dur.

### 3.8 Identification `FF F0`

Requête vide, réponse ASCII, champs séparés par `|` :

```
MODÈLE|VERSION|DATE_COMPILATION|PLAGE_G|PSEUDO
TrimBox|2.0-a14|Oct  4 2026 23:30:12|16|Buggy 1
```

- La **version** décide côté console des fonctions du firmware S3 : à partir
  de la version 2 (ou si le modèle contient `S3`, nom annoncé par les versions
  2.0-a1 à 2.0-a13 : « TrimBox DIY S3 »), le téléchargement demande les
  emplacements `0x29` et la console lit les lignes (`FF F1`).
- L'**empreinte de compilation** permet à la console de vérifier, après une
  mise à jour, quelle version tourne réellement (§7).
- Un appareil tiers répond NACK : la console traite ce cas sans erreur.

### 3.9 Lignes du module `FF F1`

Requête vide → réponse de 28 octets ; écriture de 28 octets → ACK.

```
0  u8   version (1)
1  u8   mode : 0 aucune, 1 circuit, 2 dragster (déduit des lignes posées)
2  u8   voie de commande CRSF (1 à 16, défaut 8)
3  u8   réservé
4  i32  départ : latitude (deg × 1e7)
8  i32  départ : longitude (deg × 1e7)
12 i32  départ : cap (deg × 1e5, 0 = nord, sens horaire)
16 i32  arrivée : latitude
20 i32  arrivée : longitude
24 i32  arrivée : cap
```

Une ligne absente a latitude et longitude à 0. Le centre de la ligne est le
point donné ; la ligne est perpendiculaire au cap, demi-longueur 4 m.

### 3.10 Wi-Fi `FF F2`

- Requête vide → 4 octets : `[allumé, automatique, nb d'appareils, console WebSocket connectée]`.
- `0` : couper et désactiver l'automatique ; `1` : allumer maintenant (NACK si
  la voiture roule) ; `2` : automatique seul.

### 3.11 Emplacements mémoire

Chaque emplacement fait **81 octets** : `[type][80 octets]`.

| Type | Contenu |
|---|---|
| `0x21` | Données (format §3.5) |
| `0x26` | Changement d'état : `[0]` état, `[1]` raison, `[4]` u32 iTOW, `[8]` u32 nb de points |
| `0x28` | Réservé (lot ESC, §13) : jamais écrit |
| `0x29` | Événement de chrono, écrit **seulement en enregistrement** : `[0]` u32 iTOW (interpolé pour un passage), `[4]` u8 ligne (0 départ, 1 arrivée), `[5]` u8 nature (0 passage, 1 tour, 2 parcours, 3 chrono intermédiaire), `[6]` u16 n°, `[8]` u32 temps (ms ; pour un chrono intermédiaire : depuis le départ), `[12]` u8 type de chrono (0 vitesse, 1 distance), `[14]` u16 valeur (km/h ou m) ; reste à zéro |
| `0x00` | Bourrage (emplacement douteux après un effacement interrompu, §4.3) |
| `0xFF` | Libre |

### 3.12 Séquences

**Téléchargement.** La console envoie `FF 23` → le module répond `FF 23`
(4 octets : nombre d'emplacements à parcourir, progression indicative) → flux
de `FF 21` et `FF 26` (et `FF 29` si demandé) → `FF 02` (ACK) final. Les
données en direct sont suspendues pendant l'opération.

| Charge de `FF 23` | Sens |
|---|---|
| vide | téléchargement des types `0x21` et `0x26` seulement |
| 1 octet, bit 1 (`0x02`), aucun téléchargement en cours | téléchargement **avec** les emplacements v2 (`0x28`, `0x29`) |
| 1 octet `0x00`, téléchargement en cours | annulation |

Les emplacements `0x21` invalides (§4.5) sont écartés et comptés (journal série
en fin de téléchargement).

**Effacement.** La console envoie `FF 24` → notifications `FF 24` (1 octet : %)
→ `FF 02` final. Refusé pendant un enregistrement ou un téléchargement ; une
fois lancé, **non annulable** (§12.4).

---

## 4. Firmware

### 4.1 Architecture

- Croquis Arduino `trimbox_s3/` ; `trimbox_s3.ino` n'appelle que
  `app::setup()` et `app::loop()`.
- `src/core/` : logique **sans matériel**, compilée et testée sur PC
  (protocoles TrimBox, UBX, CRSF ; chrono ; automate d'enregistrement ; pose de
  ligne ; configuration A/B ; maintien à l'arrêt ; radios de la console ;
  serveur HTTP/WebSocket ; contrôles de mise à jour).
- `src/hw/` : pilotes (GNSS, IMU, mémoire, Bluetooth, Wi-Fi, radio, OTA).
- `src/config.h` : réglages et brochage (§4.12).
- `src/console_gz.h` : console compressée, **générée** par
  `tools/embed_console.py` (ne pas éditer).

**Boucle unique, non bloquante**, sur le cœur 1, protocole d'abord :

```
1  commandes série
2  réception (Bluetooth, WebSocket) et exécution des commandes
3  effacement progressif
4  téléchargement (alimente la file d'émission)
5  vidange de la file vers la console
6  (extinction : décidée par l'automate, à chaque époque GNSS)
7  IMU, GNSS (une époque → enregistrement, chrono, télémétrie), voie de commande CRSF
   radios de la console (§6), validation OTA, bouton, DEL
```

La pile NimBLE tourne sur le cœur 0 ; ses rappels se contentent de déposer les
octets reçus dans un tampon protégé. Un découpage en tâches n'apporterait rien :
une écriture en flash gèle **les deux cœurs** (§4.3).

Chien de garde de boucle (`enableLoopWDT`, 5 s) : la carte redémarre si la
boucle se fige ; aucune opération ne dure plus de 3 s (reconfiguration GNSS).

### 4.2 File d'émission unique — exigence critique

**Toutes** les trames sortantes, données en direct comprises, quel que soit le
transport (Bluetooth ou WebSocket), passent par **une seule file d'octets**
(`TXQ_SIZE` 4096 octets, tampon circulaire).

> Deux chemins de sortie concurrents permettent à un paquet de s'insérer au
> milieu d'une réponse fragmentée : le flux côté console est corrompu et la
> réponse rejetée (§12.2).

- Les données en direct ne sont empilées que s'il reste au moins
  `LIVE_RESERVE = 88 + 256` octets libres : une réponse de commande n'est
  jamais perdue faute de place. Le direct est **sacrifiable**, les réponses non.
- Vidange Bluetooth : jusqu'à 12 notifications par tour, de `MTU − 3` octets
  (plafond 244). Un `notify()` refusé laisse les octets dans la file, sans perte
  ni réordonnancement. Vidange WebSocket : jusqu'à 8 messages de 1024 octets.

### 4.3 Écriture en flash interne

L'écriture ou l'effacement de la flash interne **suspend le cache
d'instructions** : tout code hors IRAM est gelé pendant l'opération.

- Écriture d'un emplacement (81 octets) : ~1 ms. Les FIFO matériels UART de 128
  octets couvrent ce délai, même à 420 000 bauds. Tampons logiciels : 4096 (GNSS),
  2048 (CRSF).
- Journal en **ajout linéaire** dans la partition `log`.
- **Aucun effacement pendant un enregistrement** : l'effacement porte sur la
  zone utilisée, d'un seul tenant, à la demande de la console.
- **Effacement à rebours**, du dernier secteur utilisé vers le premier :
  interrompu par une coupure, le journal reste contigu depuis l'adresse 0, et la
  recherche de fin par dichotomie reste valable.
- Au démarrage, la zone qui suit la fin du journal est contrôlée : si elle n'est
  pas vierge (effacement interrompu en plein secteur), les emplacements douteux
  sont marqués « bourrage » (`0x00`, obtenu en programmant des bits à 0, sans
  effacement) et le secteur suivant est effacé.

### 4.4 Configuration persistante — double exemplaire

Partition `cfg` : `RecConfig` A à l'offset 0, B à 4096 ; `LineConfig` A à 8192,
B à 12288. Écriture **en alternance** A puis B : une coupure pendant
l'effacement-réécriture d'un secteur laisse toujours un exemplaire valide.
Sérialisation **octet par octet** (jamais de `memcpy` de structure), CRC32 IEEE.
La NVS de l'ESP-IDF n'est **pas** utilisée pour ces structures.

`RecConfig` (28 octets) :

```
0  u32  magie 0x534D4252        12 u16  noFixInterval (s)
4  u8   version 2               14 u16  autoOffInterval (s)
5  u8   enabled                 16 u8   gnssDynModel (7)
6  u8   dataRate                17 u8   gnss3dSpeed
7  u8   flags (0x3F)            18 u8   gnssMinAcc (m, 0 = sans limite)
8  u16  statSpeed (mm/s)        19 u8   réservé
10 u16  statInterval (s)        20 u32  seq       24 u32  CRC32 des octets 0-23
```

`LineConfig` (40 octets) : magie `0x4C494E45` (« LINE »), puis la charge de
28 octets de §3.9 (version 1 en tête), `seq` à l'offset 32, CRC32 à l'offset 36.

Au démarrage : lire les deux exemplaires, valider magie + version + CRC, retenir
le plus récent par comparaison de `seq` tolérante au rebouclage
(`(int32_t)(b.seq − a.seq) > 0`). Relecture de contrôle après écriture.

### 4.5 Sécurité à la coupure d'alimentation

Quatre mécanismes, tous **obligatoires** :

1. **Double exemplaire** de la configuration (§4.4).
2. **Pas de reprise automatique** (`AUTO_RESUME_RECORDING 0`) : si la
   configuration indique « actif » au démarrage, la session précédente s'est mal
   terminée. Le module repart **à l'arrêt** (journal :
   « session précédente interrompue »), données téléchargeables.
   L'enregistrement automatique (§4.6), lui, reprend normalement au prochain
   roulage.
3. **Pas d'extinction trop tôt** : jamais avant `MIN_UPTIME_BEFORE_SLEEP_S`
   (120 s) après le démarrage, ni avant un premier point enregistré (`0x10`).
4. **Validation à la lecture** : un emplacement interrompu contient des octets
   restés à `0xFF`. Avant émission : `fixType ≤ 5`, `numSV ≤ 60`,
   `|lat| ≤ 900000000`, `|lon| ≤ 1800000000`, `0 ≤ gSpeed ≤ 140000000`.

Le GNSS reste allumé dès le démarrage (chrono radio et pose de ligne doivent
fonctionner sans enregistrement, §12.20).

### 4.6 Automate d'enregistrement

`core/recorder`, à chaque époque GNSS :

- **Attente de fix** (`0x01`) : rien n'est stocké avant un fix 3D.
- **Arrêt** (`0x02`) : vitesse sous `statSpeed` pendant `statInterval` →
  pause (seulement avec un fix valide).
- **Sans fix** (`0x04`) : absence de fix pendant `noFixInterval` → pause.
- **Reprise** : le passage pause → actif est notifié mais **pas stocké**
  (le passage actif → pause, lui, l'est).
- **Extinction** (`0x08`) : pause prolongée au-delà de `autoOffInterval` →
  veille profonde, réveil par le bouton BOOT.
- **Automatique** (`0x20`, actif par défaut) : l'enregistrement **s'ouvre**
  après 3 époques de suite au-dessus de `statSpeed` avec fix, et la session se
  **ferme** (état STOPPED, pas PAUSED) après `statInterval` à l'arrêt.
  **Un roulage = une session.** Les boutons de la console restent disponibles.

### 4.7 Commandes série (prise « USB », 115 200 bauds)

| Touche | Effet |
|---|---|
| `s` | Arrêt d'urgence de l'enregistrement |
| `r` | Démarrer l'enregistrement (essai sans téléphone) |
| `w` | Wi-Fi marche / arrêt (arrêt manuel : automatique désactivé, §6.2) |
| `i` | État : mémoire, enregistrement, configuration, lignes |
| `b` | Banc d'essai : cadence GNSS, IMU, CRSF (LQ, RSSI), Bluetooth (actif / coupé), Wi-Fi, partition du firmware (« EN VALIDATION » après une mise à jour) |
| `m` | Mesures de l'IMU en direct (10 lignes, mg et c°/s) |
| `z` | Configuration par défaut (données conservées) |
| `?` | Aide |

### 4.8 GNSS

- Recherche de la vitesse d'origine (38 400, 9 600, 115 200, 57 600, 230 400
  bauds), passage à **115 200**, configuration **renvoyée à chaque démarrage**.
- NMEA coupé, un **NAV-PVT** par solution.
- Cadence selon `dataRate`, **25 Hz** par défaut (`MAX_NAVIGATION_RATE 25`).
  Constellations : GPS + Galileo ; BeiDou, GLONASS, QZSS, SBAS coupés. Si la
  cadence n'est pas tenue avec Galileo, repli automatique sur le GPS seul
  **[À VALIDER]**.
- Modèle dynamique **7 (Airborne 2 g)** : une voiture RC dépasse 1 g en
  freinage et en virage, un modèle automobile lisserait ces valeurs.
- Filtres passe-bas de vitesse et de cap **désactivés** (`CFG-ODO-OUTLPVEL`,
  `CFG-ODO-OUTLPCOG`), maintien statique désactivé
  (`CFG-MOT-GNSSSPEED_THRS = 0`).

### 4.9 IMU

- Pilote maison (`hw/imu`) : sonde à 100 kHz puis 400 kHz, 5 essais,
  réinitialisation logicielle, relecture des registres, nouvel essai toutes les
  2 s si absente.
- ±16 g, ±2000 °/s, 416 Hz. `IMU_AVERAGE 1` : moyenne des échantillons sur la
  période GNSS (anti-repliement des vibrations) **[À VALIDER]** : comparer la
  détection du temps en l'air avec `IMU_AVERAGE 0`.
- Orientation selon le montage : `AXIS_*` dans `config.h`. L'IMU se visse
  **rigidement** au châssis, axes alignés sur la voiture.
- IMU absente : la console affiche des tirets (« IMU absente ») plutôt que des
  0,00 g.

### 4.10 Maintien à l'arrêt

`core/stillhold` (`STILL_HOLD 1`) : position figée (moyenne de 2 s) et vitesse
à 0 quand la voiture est immobile, pour supprimer la dérive du GPS à l'arrêt.

- Entrée : vitesse sous ~1,1 km/h pendant 0,2 s, IMU calme
  (`|‖a‖ − 1 g| < 80 mg`, `‖ω‖ < 15 °/s`).
- Sortie : vitesse au-dessus de ~2,2 km/h, mouvement vu par l'IMU, ou
  éloignement de plus de 4 m.
- `STILL_HOLD 0` pour enregistrer le GNSS brut.

### 4.11 DEL et bouton

| Couleur | Sens |
|---|---|
| rouge clignotant | GPS ou mémoire absents (normal au banc sans GPS) |
| rouge fixe | enregistrement en cours |
| orange | enregistrement en pause |
| vert clignotant | GPS calé, prêt |
| bleu fixe | console connectée |
| bleu clignotant | Wi-Fi ouvert, en attente du téléphone |

Les couleurs se superposent (rouge + bleu = violet). **Bouton BOOT, appui long
3 s** : Wi-Fi marche / arrêt.

### 4.12 Personnalisation (`config.h`)

| Réglage | Défaut | Rôle |
|---|---|---|
| `DEVICE_NICKNAME` | `"Buggy 1"` | ≤ 16 caractères (§12.1) : `TrimBox Buggy 1` en Bluetooth, `TrimBox-Buggy-1` en Wi-Fi |
| `FIRMWARE_VER` | `"2.0-a14"` | Version annoncée (`FF F0`, journal) |
| `WIFI_PASS` | `"trimbox-rc"` | Mot de passe WPA2 du point d'accès (8 caractères min.) — **à personnaliser** |
| `WIFI_AUTO_DEFAULT` | `1` | Wi-Fi automatique à l'arrêt |
| `WIFI_AUTO_ON_S` | `30` | Arrêt continu avant allumage du Wi-Fi |
| `BLE_AUTO_OFF` / `BLE_AUTO_ON_S` | `1` / `30` | Bluetooth coupé au roulage / rallumé après 30 s (§6.3) |
| `CRSF_LINE_CHANNEL_DEFAULT` | `8` | Voie de pose de ligne |
| `LINE_MIN_SPEED_MMS` / `LINE_ARM_WINDOW_MS` | `2000` / `10000` | Pose lancée au-dessus de 2 m/s ; sinon ligne armée 10 s (§5.5) |
| `PIN_LED_RGB` | `48` | **38** sur une DevKitC-1 v1.1 |
| `AXIS_*` | à plat, x vers l'avant | Orientation de l'IMU |
| `IMU_AVERAGE`, `STILL_HOLD` | `1`, `1` | §4.9, §4.10 |

Plusieurs voitures : un pseudo différent par module suffit à les distinguer.

---

## 5. Télémétrie radio CRSF

### 5.1 Liaison

- UART2, **420 000 bauds, 8N1, non inversé**, RX et TX séparés ; le module se
  comporte comme un **contrôleur de vol** vis-à-vis du récepteur.
- Récepteur ER5C-i (interface web, onglet *Model*) : sorties **2 et 3** en
  **Serial TX / Serial RX**, protocole CRSF ; direction sur la sortie 1 (CH1),
  ESC sur la sortie 4 (**CH2**), sortie 5 libre. Liaison validée sur la MT12.
- Croisement : TX du récepteur → GPIO 16, RX du récepteur ← GPIO 15. Masse
  commune. Aucun mixage de la MT12 n'est modifié, sauf CH8 (§9).

### 5.2 Format

```
[0xC8] [longueur] [type] [charge ...] [CRC8]
longueur = 1 (type) + taille(charge) + 1 (CRC)
CRC8 : polynôme 0xD5 (DVB-S2), sur type + charge
```

Entiers CRSF en **big-endian** : conversions par fonctions dédiées
(`put_be16`, `put_be32`…), jamais de `memcpy` de structure (§12.12).

### 5.3 Trames exploitées

**Reçues :** `0x16` voies RC (16 × 11 bits, lecture de la voie de commande) ;
`0x14` statistiques de liaison (RSSI, LQ : commande série `b`, non
enregistrées).

**Émises** — une trame juste après chaque trame de voies reçue, au plus une
toutes les 10 ms, en tourniquet :

| Type | Nom | Cadence | Contenu |
|---|---|---|---|
| `0x02` | GPS | 5 Hz | lat, lon (deg × 1e7), vitesse (km/h × 10), cap (deg × 100), altitude (m + 1000), satellites |
| `0x03` | Date et heure GPS | 1 Hz, dès que NAV-PVT les déclare valides et résolues (`valid & 0x07`) | int16 année, mois, jour, heure, minute, seconde (UTC), uint16 ms. Relayée par **ExpressLRS ≥ 4.1** (émetteur et récepteur), capteur `Date` sur EdgeTX |
| `0x21` | Mode de vol (texte) | à chaque événement, puis rappel d'état 1 Hz | Messages §5.4 |

`0x08` (batterie) n'est pas émis : la tension vient du récepteur. `0x0C` /
`0x0D` (régime, température) : sans objet tant qu'aucune mesure moteur n'existe.

### 5.4 Messages texte (trame `0x21`, capteur `FM`)

Canal de référence du chrono : transmis par tous les systèmes CRSF. **15
caractères ASCII au plus**, terminés par un octet nul. Temps formatés **dans le
module**, en millisecondes entières (jamais de virgule flottante dans la chaîne).

| Préfixe | Exemple | Sens |
|---|---|---|
| `L` | `L12 21.345+0.35` | Tour (ou parcours) 12 en 21,345 s, 0,35 s de plus que le meilleur **précédent** (négatif = nouveau meilleur). Écart omis au premier tour ou si le texte dépasse 15 caractères |
| `R` | `R 0-50 2.184` / `R 50m 3.012` | Dragster : chrono intermédiaire |
| `S` | `S REC C` | État (`REC` / `PAUSE` / `STOP` / `NOFIX`) puis lignes : `C` circuit, `D` dragster, `-` aucune |
| `K` | `K DEPART OK` / `K ARRIVEE OK` / `K ARME DEPART` / `K ARME ARRIVEE` / `K DELAI` / `K PAS DE FIX` / `K EFFACE` | Accusés de pose de ligne (§5.5). `K VIT FAIBLE` n'est plus émis ; le script le reconnaît encore |
| `W` | `W WIFI ON` / `W WIFI OFF` | Point d'accès de la console allumé / coupé |
| `U` | `U REDEMARRAGE` / `U VALIDATION` / `U MAJ OK` | Étapes d'une mise à jour (§7) |

**Un message à la fois.** Le récepteur ExpressLRS ne garde que le **dernier**
message en attente : deux messages envoyés coup sur coup, le premier est perdu.
D'où une **file** côté module : chaque message est tenu **seul 0,7 s**
(`CRSF_EVENT_HOLD_MS`), renvoyé toutes les 150 ms ; le rappel d'état `S` n'est
émis que file vide ; **un seul message par tour** (le script déduit le
meilleur tour) ; le script ne traite un message **qu'à son changement**. Réglage
ExpressLRS : *Telem Ratio* **1:16** ou plus fréquent.

### 5.5 Pose de ligne par la voie de commande

Mécanisme de référence : les voies passent toujours, quel que soit le système
radio. Voie `LineConfig.crsfChannel`, **CH8** par défaut.

- Neutre entre −30 % et +30 % ; **retour au neutre obligatoire** entre deux
  commandes (anti-rebond).
- **Haut (> +60 %) maintenu 0,5 s** : ligne de **départ** (ou ligne unique en
  circuit).
- **Bas (< −60 %) maintenu 0,5 s** : ligne d'**arrivée** (dragster).
- **Bas maintenu 3 s** : efface toutes les lignes (`K EFFACE`).
- **Mode déduit** : une ligne = circuit, départ + arrivée = dragster. Aucune
  commande de mode (l'état décide, pas le clic : §12.7).

**Voiture lancée** (≥ 2 m/s) : position **interpolée** entre les deux points
GNSS qui encadrent la commande, ligne **perpendiculaire au cap**, `K DEPART OK`.

**Voiture arrêtée** (`core/linearm`) : la position est mémorisée et la ligne
**armée** (`K ARME DEPART`). Le pilote démarre dans les **10 s**. Le cap de la
ligne est celui du départ : cap GNSS dès 3 m parcourus au-dessus de 7,2 km/h,
sinon direction ancrage → position après 6 m. La ligne est posée **0,5 m
devant** l'ancrage, puis les points du départ sont **rejoués** dans le chrono :
le départ arrêté compte (dragster : temps depuis l'arrêt ; circuit : début du
premier tour). Pas de départ dans le délai : `K DELAI`, rien n'est posé. Pas de
fix : `K PAS DE FIX`.

Dans les deux cas : demi-longueur **4 m** (comme la console), sauvegarde
immédiate dans `LineConfig` (survit à une coupure), lignes **gardées d'une
session à l'autre**.

### 5.6 Chronométrage embarqué

`core/lapcore` — **portage à l'identique** de l'algorithme de la console (§8.6),
mêmes constantes, **aucune « optimisation »** :

1. intersection segment/segment ;
2. proximité de l'ancrage : `halfM × 1,15` ;
3. sens : produit scalaire avec le cap de référence > 0,3 ;
4. déduplication : 1,5 s ;
5. temps interpolé entre les deux échantillons qui encadrent la traversée.

Modes **circuit** (un temps par tour) et **dragster** (un temps par parcours
départ → arrivée, plus chronos intermédiaires 0-30 / 0-50 / 0-80 km/h et
25 / 50 / 100 m, interpolés). Chaque événement est envoyé à la radio (§5.4) et,
en enregistrement, **stocké** (type `0x29`, §3.11). Firmware et console doivent
donner les mêmes temps **à 1 ms près** (§12.16, banc `console_check.js`).

---

## 6. Radios de la console : Wi-Fi et Bluetooth

### 6.1 Règle commune : coupées au roulage, rallumées à l'arrêt

`core/airgate` (testé sur PC), partagé par le Wi-Fi et le Bluetooth :

| Règle | Valeur |
|---|---|
| « Roule » | **3 solutions GNSS de suite** avec fix au-dessus de **7,2 km/h** (`WIFI_MOVE_MMS` 2000, `WIFI_MOVE_EPOCHS` 3 : 120 ms à 25 Hz) |
| « À l'arrêt » | sous **5 km/h** (`WIFI_STILL_MMS` 1389), **ou sans fix**, ou sans solution GNSS depuis 2 s |
| Coupure | **immédiate** dès que la voiture roule, **sans condition** (même forcée à la main) |
| Rallumage | après **30 s d'arrêt continu**, si l'automatique est actif |
| Entre 5 et 7,2 km/h | rien n'est coupé ; le compteur d'arrêt repart de zéro |

Cas limites :

- Wi-Fi coupé **à la main** (bouton, touche `w`, `FF F2` 0) : il ne revient plus
  seul jusqu'au prochain forçage ou redémarrage — mais il est toujours coupé en
  roulant.
- **Perte du GPS en roulant** : la voiture est réputée à l'arrêt (on ne roule
  pas en course sans GNSS) ; les radios reviennent après 30 s.

Motif : un émetteur 2,4 GHz à quelques centimètres du récepteur ExpressLRS peut
dégrader la liaison de commande (§12.10). La règle s'applique quel que soit le
système radio.

### 6.2 Point d'accès Wi-Fi

- SSID `TrimBox-<pseudo>` (espaces → `-`), WPA2 `WIFI_PASS`, canal 6,
  **2 appareils** au plus, puissance **8,5 dBm** (portée d'un stand), adresse
  `192.168.4.1`.
- Juste allumé, le module est réputé à l'arrêt : le point d'accès apparaît
  **30 s** après la mise sous tension.
- **Portail captif** : un serveur DNS répond `192.168.4.1` à toute requête et
  toute autre adresse renvoie `302` vers la console ; le téléphone propose
  d'ouvrir la page.
- Forçage : appui long 3 s sur BOOT, touche `w`, ou `FF F2`. La radio est
  prévenue (`W WIFI ON` / `W WIFI OFF`).
- Pas de coupure « au démarrage d'un enregistrement » ni « sans client » :
  démarrer un enregistrement depuis la console Wi-Fi couperait sa propre
  réponse. Le mouvement suffit.

### 6.3 Bluetooth

- Actif **dès la mise sous tension**.
- Dès que la voiture roule : annonces arrêtées, console connectée
  **déconnectée**. Retour après `BLE_AUTO_ON_S` (30 s) d'arrêt.
- Pas de message radio (la file `FM` est réservée au chrono) ; état visible par
  la commande série `b` (« BLE actif » / « COUPÉ (la voiture roule) »).
- `BLE_AUTO_OFF 0` : Bluetooth permanent (comportement 2.0-a12).
- Conséquence acceptée : pas de données en direct par Bluetooth pendant le
  roulage ; la session se télécharge au stand.
- **[À VALIDER]** sur la carte (le Bluetooth n'est pas émulé).

### 6.4 Console embarquée

- `tools/embed_console.py` compresse `index.html` (gzip, horodatage nul :
  résultat reproductible) en `trimbox_s3/src/console_gz.h` (~56 Ko). La CI le
  régénère avant chaque compilation.
- Serveur HTTP + WebSocket **écrit pour le projet** (`core/httpws`), sans
  bibliothèque : compilé et testé sur PC avec de vraies sockets et un vrai
  navigateur (`tests/web`).
- `Cache-Control: no-cache` : une nouvelle console arrive avec le firmware.
- Pas de service worker (origine non HTTPS) ; `localStorage` fonctionne
  (thème et accent mémorisés **pour l'origine du module**).

---

## 7. Mise à jour du firmware par Wi-Fi (OTA)

- Voie normale : section **Mise à jour du firmware** de la console (visible en
  Wi-Fi uniquement). Secours : page minimale `GET /update`.
- `POST /update`, corps = le `.bin` applicatif brut (`Content-Length`
  obligatoire, `Expect: 100-continue` géré), transmis **au fil de l'eau** à la
  partition OTA inactive (effacement progressif) : aucune pause longue, aucune
  copie en RAM.
- **Conditions** (sinon refus, message renvoyé tel quel) : enregistrement
  arrêté, voiture à l'arrêt, ni téléchargement ni effacement en cours.
- **Contrôles** (`core/otacheck`) : octet magique `0xE9`, puce ESP32-S3
  (id 9), taille ≤ partition (le fichier « complet » de 16 Mo est refusé
  d'emblée), marque **`TRIMBOX-S3-FIRMWARE-MARK-v1`** présente dans l'image ;
  puis SHA-256 par `esp_ota_end`. En cas de refus, le corps est lu jusqu'au
  bout pour que le navigateur reçoive l'explication.
- Accepté : `esp_ota_set_boot_partition`, réponse `200`, redémarrage 1,5 s plus
  tard (`U REDEMARRAGE`).
- **Retour arrière** (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) : le core Arduino
  confirme d'office tout nouveau firmware ; `verifyRollbackLater()` est
  redéfinie pour reprendre la main. Confirmation après **autotest** (mémoire et
  configuration lues) **et 15 s** de fonctionnement (`OTA_VALIDATE_AFTER_S`) :
  `U VALIDATION` puis `U MAJ OK`. Plantage, gel de la boucle > 5 s ou coupure
  avant : retour automatique à la version précédente.
- La console compare l'empreinte de compilation (`FF F0`) avant et après :
  « nouvelle version active » ou « le module est revenu à la précédente ».
- **Premier flash** : toujours par câble, fichier complet à `0x0` (efface la
  mémoire d'enregistrement).
- `partitions.csv` ne change **jamais** par OTA.

---

## 8. Console web

### 8.1 Contraintes

- **Un seul fichier** `index.html`, autonome, sans bibliothèque : rendu en
  Canvas 2D natif.
- Bluetooth Web : origine **HTTPS** obligatoire (GitHub Pages). Console Wi-Fi :
  servie en `http:` par le module.
- Toutes les variables d'état sont déclarées **en tête de script** (§12.6).

### 8.2 Sources de données

Commutateur **Bluetooth / Wi-Fi / Démo**. Toutes les commandes passent par des
fonctions uniques (`doConnect`, `doDownload`…) qui se comportent selon la source
(`demoMode`, `wifiMode`). **Ne jamais réaffecter les gestionnaires
d'événements** pour changer de source.

- **Wi-Fi** : proposé seulement quand la page est servie par le module ; choisi
  et connecté automatiquement ; Bluetooth et Démo sont alors masqués ;
  reconnexion automatique toutes les 3 s tant que l'utilisateur n'a pas
  déconnecté (le point d'accès disparaît quand la voiture roule).
- **Démo** : simule un module S3 complet (direct 25 Hz, mémoire de 154 333
  points, trois sessions dont une ligne droite pour le dragster).

### 8.3 Réassemblage des trames (`feed`) — exigence critique

Le flux arrive fragmenté. Le réassembleur :

1. cherche `B5 62` ;
2. **valide la longueur annoncée** contre `EXPECT_LEN` (§3.4) : une longueur
   incohérente signale un faux départ → avancer de 2 octets ;
3. vérifie la somme de contrôle ;
4. **en cas d'échec, avance de 2 octets — jamais de la longueur annoncée.**

Le WebSocket réutilise `feed()` **sans modification**. Seule modification admise
de son entourage : la table `EXPECT_LEN`. C'est le bug le plus coûteux du projet
(§12.2) : 50 % de perte de données avant correction, 0,2 % après.

### 8.4 Filtrage des données

| Niveau | Règle | Motif |
|---|---|---|
| Plausibilité (`plausible`) | `|lat| ≤ 90`, `|lon| ≤ 180`, `0 ≤ vitesse < 500 km/h`, `fix ≤ 5`, `sats ≤ 60`, `|alt| < 10 000 m`, `|accél.| ≤ 20 g` | Une trame corrompue passe la somme de contrôle une fois sur 65 536 |
| Position (`despike`) | point à plus de **30 m** (`MAX_STEP_M`) du précédent **et** du suivant → supprimé | décrochages isolés du récepteur |
| Vitesse (`despikeSpeed`) | écart > **20 km/h** (`MAX_DV_KMH`) avec les deux voisins → moyenne des voisins | à 25 Hz, même 10 g ne font que 14 km/h en 40 ms |

Le seuil de 20 g est calibré pour ±16 g ; le passer à 35 g seulement si l'IMU
passe à ±32 g, et sur demande explicite (§12.17).

### 8.5 Analyse

- Statistiques de session : vitesses max / moyenne / min, distance, durée,
  accélération, freinage, latéral, vertical max / min, temps en l'air.
- **Temps en l'air** : `|az| < 0,35 g` pendant au moins **3 échantillons**
  (`MIN_AIR_SAMPLES`) ; sans ce seuil, les vibrations produisent des centaines
  de faux sauts. Ignoré sans IMU (0 g partout). Rapporté **au tour** quand une
  ligne est posée.
- **Survol du tracé** : agrégation de **tous les passages** au même endroit
  (nombre, vitesses, G) dans une infobulle.
- **Portion** : glisser sur le profil de vitesse isole une portion et ses
  statistiques.
- Session vide ou sans fix : explication affichée (nombre de points, de fix).
- **Comparaison** de sessions : tableau, vitesse en fonction de la distance,
  trajectoires superposées. Le profil est gradué en **mètres depuis le départ
  de chaque session** (une même abscisse = le même endroit du parcours ; la
  session la plus courte s'arrête avant). Le survol affiche la vitesse de
  chaque session au point visé. **Glisser** sur le profil sélectionne une
  portion [d0, d1] : tableau par session (durée avec écart au meilleur temps,
  distance, V max / moyenne / mini, accélération, freinage, latéral, vertical,
  temps en l'air, points ; « — » si la session s'arrête avant) et portion
  surlignée sur les trajectoires. Un appui sans glisser ou « Effacer » retire
  la sélection ; elle est conservée au redimensionnement et tant que les
  sessions comparées ne changent pas. Distances cumulées avec la règle de
  `pathLength` (saut > `MAX_STEP_M` ignoré). Le profil reste à sa place à
  l'écran quand le tableau apparaît (`overflow-anchor:none` + correction).

### 8.6 Chronométrage de la console

Modes **Circuit** (une ligne, un temps par tour) et **Dragster** (départ +
arrivée ; chaque départ est associé au premier franchissement de l'arrivée qui
le suit). Détection : intersection, proximité de l'ancrage (`halfM × 1,15`),
sens (> 0,3), déduplication 1,5 s, temps interpolé ; demi-longueur **4 m**.
Chronos intermédiaires configurables (vitesses, distances), interpolés. **Même
algorithme que le firmware** (§5.6, §12.16). Changer de mode efface les lignes.

### 8.7 Lignes et chronos du module

- À la connexion d'un module **S3**, la console lit `FF F1` ; les lignes sont
  posées **d'office** dans l'analyse si elles sont à moins de **30 m** du
  tracé (mode circuit ou dragster déduit du module). Bouton **« Lignes du
  module »** pour les relire. Elles restent connues après la coupure du Wi-Fi
  (oubliées en passant en démo).
- Ligne reconstruite par `makeLineAt(lat, lon, cap)`, même géométrie que
  `makeLine` (fonction commune `lineFrom`).
- Téléchargement depuis un module S3 : `FF 23` + `0x02`. Les emplacements
  `0x29` sont rattachés à leur session (celle du point qui les précède) et
  affichés dans l'encart **« Chronos du module »** : tours ou parcours au
  millième, chronos intermédiaires, et **écart** avec le calcul de la console
  sur le même tracé (quelques ms au plus).

### 8.8 Cartographie

- Projection **Web Mercator** normalisée [0, 1].
- **Zoom continu** ×1 à ×60 (`ZOOM_MAX`), ancré sous le curseur ; pincement au
  doigt ; cadrage minimal de 25 m (le bruit GPS d'une voiture posée n'est pas
  agrandi).
- Tuiles Esri `…/World_Imagery/MapServer/tile/{z}/{y}/{x}` (ordre **z/y/x**),
  niveau **plafonné à 19** (`MAX_TILE_Z`, §12.5). Attribution : « Esri, Maxar,
  Earthstar Geographics ».
- Sans Internet (Wi-Fi du module) : le fond satellite reste actif, un message
  l'explique et la console réessaie toutes les 10 s (les images passent par la
  4G du téléphone si Android y bascule ; ouvrir la console dans Chrome, pas dans
  la fenêtre de portail captif).

### 8.9 Exports et imports

| Format | Export | Import | Usage |
|---|---|---|---|
| **VBO** | ✅ | ✅ | RaceChrono Pro, Circuit Tools |
| **CSV** | ✅ | ✅ | Tableur |
| **GPX** | ✅ | ✅ | Cartographie |

Pièges du VBO : latitude / longitude en **minutes d'arc**, longitude **positive
vers l'ouest**, section `[column names]` en jetons courts
(`sats time lat long velocity heading height vert-vel LongAcc LatAcc`), temps
`HHMMSS.SS`. Un GPX ne transporte ni vitesse ni accélérations : reconstituées à
partir des positions. Aller-retour vérifié < 5 cm.

### 8.10 Interface

- **Thèmes** sombre (noir intégral `#000000`, économie OLED) et clair (`#F3EFFB`),
  bascule par bouton, préférence mémorisée (`localStorage`), choix initial :
  préférence enregistrée → système → sombre.
- **Couleur d'accent** au choix (bouton palette : 5 préréglages, couleur libre,
  « Par défaut »), mémorisée (`trimbox-accent`). Variantes dérivées
  (`--violet-glow`, `--violet-dim`, `--violet-rgb`) ; texte des boutons en noir
  si le blanc n'atteint pas 4,5:1. Couleurs d'état (ok / rec / warn) jamais
  modifiées. Variantes de texte assombries en thème clair (`--ok-text`…).
- Les canevas ne lisent pas les variables CSS : cache `THEME` rafraîchi à chaque
  changement de thème (`refreshTheme`), puis **redessin**.
- **Mise en page adaptative** :
  - téléphone (< 700 px) : une colonne ; sous 440 px, l'état de connexion passe
    sous le titre et les valeurs longues sont réduites ;
  - tablette (700 à 1099 px) : une colonne plus large ;
  - ordinateur (≥ 1100 px, depuis 1.7.14) : **toute la largeur de l'écran** et
    une **barre d'onglets** — *Appareil* (connexion, direct, mémoire),
    *Analyse* (carte et profil à gauche, chiffres et tours à droite ; message
    d'attente sans session), *Sessions* (ouverture de fichiers, liste en
    grille ; le bouton « Choisir des fichiers… » est aligné sur la ligne « Tout
    exporter », comparaison), *Réglages* (enregistrement autonome, mise à jour,
    journal ouvert) — à côté du choix de la source. Dans un onglet, les
    panneaux sont des tuiles qui se partagent la ligne (`flex-wrap`) : la ligne
    est toujours remplie. Chaque panneau porte `data-pane`, `body[data-tab]`
    choisit l'onglet ; l'analyse d'une session ouvre l'onglet *Analyse*. Onglet
    mémorisé (sauf *Analyse*), flèches gauche / droite au clavier. La carte
    n'est jamais plus haute que l'écran.
  Les regroupements (`.colA`, `.colB`, `.anMain`, `.anStats`, `.anBoxes`) sont
  en `display:contents` sous 1100 px, l'ordre étant fixé par `order` : le
  téléphone et la tablette affichent tout, sans onglets, comme avant. Aucun
  défilement horizontal de 320 à 2560 px (vérifié par `tests/web`, qui vérifie
  aussi le contenu de chaque onglet).
- Les courbes (`#chart`, `#cmpChart`) adaptent leur résolution interne à leur
  taille affichée et à la densité de l'écran (`fitCanvas`, plafond ×2) ; textes
  et traits sont mis à l'échelle. Les conversions souris → canevas passent par
  le rapport largeur interne / largeur affichée.
- **Incrustations de la carte** (infobulle, échelle, crédit) : fond sombre
  **fixe** et couleurs **fixes**, indépendantes du thème et de l'accent
  (§12.24).
- En-tête : version de la console et version du firmware connecté.

### 8.11 PWA

Manifeste et icônes **en data URI** dans le HTML (installation manuelle sans
fichier annexe). Service worker `trimbox-sw.js` **optionnel**, à côté de la page
(GitHub Pages) : hors ligne et invite d'installation ; son gestionnaire `fetch`
doit être réellement fonctionnel.

### 8.12 Versionnage

Constante `CONSOLE_VER` (actuellement **1.7.17**), affichée dans l'en-tête,
incrémentée à **chaque** modification ; après `x.y.9`, passer à `x.y.10` (puis
selon la convention en cours). Sert d'indicateur de cache. Après toute
modification : `python3 tools/embed_console.py`.

### 8.13 Fonctions protégées

Trois corruptions par un LLM local, toutes présentées comme des
« optimisations », syntaxiquement plausibles, invisibles sans exécution.
**Interdiction de modifier** :

- `distM()` — projection équirectangulaire : les écarts sont en **degrés**,
  111320 / 110540 sont des mètres **par degré** ; ne jamais convertir en radians
  avant ; le `cos(latitude)` est indispensable.
- `anal.pts` et `anal.scr` — ne jamais les supprimer « pour libérer la
  mémoire » : carte, survol, tours et exports les relisent en continu.
- `despike()`, `despikeSpeed()`, `plausible()`, `gStats()`, `pathLength()` —
  seuils déterminés par mesure.
- `feed()` — en cas d'échec de somme de contrôle, avancer de 2 octets, jamais de
  la longueur annoncée.

---

## 9. Script Lua EdgeTX (MT12)

Fichier `lua/trmbox.lua` → carte SD `SCRIPTS/TELEMETRY/trmbox.lua` (**6
caractères** au plus : limite d'EdgeTX pour les écrans monochromes).
Écran 128 × 64. Mode d'emploi détaillé : `lua/LISEZMOI.md`.

- **Lecture** : capteur `FM` (`getValue`) et, si EdgeTX y dépose ces trames,
  file brute CRSF (`crossfireTelemetryPop`, trames `0x21`). `run()` et
  `background()` lisent tous les deux. Traitement **au changement** de message.
  Plus de 3 s sans message : « PAS DE LIAISON ».
- **Pages** : *Chrono* (dernier tour en grand, n°, écart, meilleur, tours
  précédents), *Machine* (vitesse filtrée sous 3 km/h, Vmax, satellites, LQ,
  RSSI, altitude, tension de la batterie lue sur le récepteur — capteurs
  `Batt`, `RxBt`, `VFAS`, `A1` — avec tension par élément, date et heure GPS en
  heure locale), *Lignes* (mode, actions, valeur réelle de CH8).
- **Touches** (événements virtuels) : molette = pages (sur *Lignes* : choix de
  l'action) ; ENT court = page suivante (sur *Lignes* : exécuter ; second appui
  pour confirmer l'effacement) ; ENT long sur *Chrono* = remise à zéro.
- **Pose de ligne** : le script écrit **GV9** pour les 9 modes de vol ; mixage
  **CH8 = source MAX, poids GV9, mode Ajouter** (`GV9 = 100` → CH8 = +100 %).
  Départ +100 pendant 0,6 s ; arrivée −100 pendant 0,6 s ; effacement −100
  pendant 3,2 s ; une commande à la fois. Si CH8 ne bouge pas pendant un envoi :
  « CH8 immobile : mixage ? ». Sans le script : un inter 3 positions sur CH8.
- **Voix** : à chaque tour, « meilleur tour » s'il y a lieu, le temps
  (`playNumber`, centièmes), puis l'écart avec le tour **précédent** (« plus »,
  « moins », « égal ») ; accusés de pose dits à voix haute. Les nombres viennent
  du pack vocal de la radio ; les **12 mots** (`meilleur`, `plus`, `moins`,
  `egal`, `depart`, `arrivee`, `efface`, `arme`, `delai`, `vitfaib`, `pasgps`,
  `refus`) de `SOUNDS/trimbox/` (ou `trmbox/`, `fr/trimbox/`), WAV 16 bits mono
  16 kHz, fournis dans `lua/SOUNDS/trimbox/` ; sans eux, des bips. Génération :
  `tools/sons/generer-voix.bat` (voix Azure du pack EdgeTX) ; mots
  supplémentaires à la demande : `tools/sons/generer-mot.bat`.
- **Heure** : capteur `Date` (trame `0x03`) convertie en heure locale
  (`TZ_OFFSET` 1, heure d'été européenne `TZ_EU_DST`).
- ⚠️ **Règle dure** : un script de télémétrie n'a **pas** la bibliothèque
  `table` (§12.22). Ni `table.*` ni `ipairs`. Restent sûrs : `string.*`,
  `math.*`, `type`, `tonumber`, `pairs` sur tables littérales locales.

Validé sur la MT12 : lecture de `FM`, touches, lisibilité, annonces vocales,
accusés de pose, mixage CH8. **[À VALIDER]** : pose de ligne à l'arrêt sur la
voiture, enregistrement automatique en roulage.

---

## 10. Compilation, tests et déploiement

### 10.1 Chaîne de compilation (GitHub Actions)

`.github/workflows/build-s3.yml`, déclenché à chaque envoi :

- arduino-cli, core **`esp32:esp32@3.3.12`** et **`NimBLE-Arduino@2.5.1`**,
  versions **figées**. Pas d'autre bibliothèque : GNSS et IMU ont des pilotes
  maison.
- FQBN `esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=qio,USBMode=hwcdc,CDCOnBoot=cdc,PartitionScheme=no_fs`
  (un `partitions.csv` dans le dossier du croquis est prioritaire).
- Un seul `.ino` dans `trimbox_s3/` (vérifié par la CI).
- `tools/embed_console.py` avant chaque compilation.
- Artefacts : `firmware-s3-complet` (`trimbox_s3-complet.bin` à `0x0` +
  fichiers séparés), `firmware-s3-app` (`trimbox_s3-app.bin`, OTA, non
  compressé), `capture-console-wifi`.
- La compilation échoue si un test échoue.

Compilation hors CI : mêmes versions. Sans accès à `downloads.arduino.cc`,
l'outil `ctags` peut être remplacé par un script vide (le `.ino` ne déclare que
`setup()` et `loop()`) : `--build-property runtime.tools.ctags.path=<dossier>`.

### 10.2 Bancs automatisés

| Banc | Commande | Vérifie |
|---|---|---|
| Logique (g++) | `make -C tests/native` | protocoles, chrono (5 tours de 20,00 s, ligne en 4 endroits ; dragster à 6 m/s²), automate, pose de ligne, config A/B, maintien à l'arrêt, radios de la console, serveur web, contrôles OTA — ~2 500 vérifications |
| Chrono firmware = console | `node tests/native/console_check.js index.html tests/native/lap_cases.json` | mêmes trajets dans les deux implémentations, écart ≤ 1 ms ; ligne relue dans le module = ligne posée sur le tracé |
| Script Lua | `lua5.2 tests/lua/test_trmbox.lua lua/trmbox.lua` | messages, sons, impulsions GV9, affichage, file CRSF brute, dossiers de voix ; `table = nil` avant chargement |
| Console | `python3 tests/web/console_web_test.py tests/web/fakedev 8088 trimbox_s3-app.bin` | page servie gzip par le vrai serveur, Wi-Fi auto, direct 25 Hz, téléchargement + chronos du module, lignes `FF F1`, reconnexion, accent, mise à jour avec le vrai `.bin`, mise en page de 320 à 1440 px |
| Émulateur | `python3 tests/qemu/sim.py flash.bin` (`--lua`, `--arret`, `--reboot`) | GNSS 25 Hz, enregistrement, pose de ligne CH8 (par le vrai script), tours de 12,000 s sans perte côté radio, Wi-Fi coupé au roulage et rallumé à l'arrêt, coupure d'alimentation |
| Retour arrière | `python3 tests/qemu/ota_rollback.py merged.bin app.bin app_qui_plante.bin` | version saine conservée, version qui plante → retour sur `app0` (vrai chargeur) |

Pièges de l'émulateur : §12.19 (variante `-DTRIMBOX_QEMU`, jamais flashée).

### 10.3 Déploiement

1. **Dépôt GitHub** : tout le contenu, dossier `.github/` compris. L'onglet
   *Actions* compile et teste (10 à 15 min la première fois).
2. **GitHub Pages** (*Settings → Pages → `main`, `/ (root)`*) : console
   Bluetooth à `https://<pseudo>.github.io/<dépôt>/`. Noms de fichiers
   sensibles à la casse.
3. **Premier flash**, une fois, par câble, depuis un ordinateur :
   `trimbox_s3-complet.bin` à `0x0` avec https://espressif.github.io/esptool-js/,
   prise **USB** (si rien : BOOT maintenu + RST, relâcher BOOT).
4. **Ensuite** : `trimbox_s3-app.bin` par la console Wi-Fi (§7).
5. **Radio** : `trmbox.lua` dans `SCRIPTS/TELEMETRY/`, mots dans
   `SOUNDS/trimbox/`, mixage CH8, *Découvrir les capteurs*, écran de
   télémétrie → script `trmbox`.

---

## 11. Recette et bancs matériels

### 11.1 Critères d'acceptation

Firmware :

- [ ] Au démarrage, le journal affiche marque, pseudo, version (2.0-a14),
      empreinte de compilation, `mémoire : n / 154333`.
- [ ] `b` : GNSS 25 Hz dehors, IMU présente, CRSF « liaison OK », BLE actif.
- [ ] Un roulage ouvre une session et la ferme 30 s après l'arrêt.
- [ ] **Arrachement** : couper l'alimentation en pleine session. Au
      redémarrage, le module est joignable, à l'arrêt, données intactes.
- [ ] Le téléchargement signale les emplacements corrompus écartés.
- [ ] Wi-Fi et Bluetooth coupés en roulant, revenus 30 s après l'arrêt.
- [ ] Mise à jour Wi-Fi acceptée ; une version qui plante revient seule à la
      précédente.

Console :

- [ ] Aucune trame rejetée lors d'un téléchargement complet.
- [ ] Aucune vitesse > 150 km/h, aucun « aller-retour » sur le tracé.
- [ ] Export VBO lu par Circuit Tools au bon endroit du globe ; aller-retour
      export → import < 5 cm.
- [ ] Lignes du module posées d'office ; chronos du module = chronos console
      à quelques ms.
- [ ] Les deux thèmes lisibles, infobulle comprise ; pas de défilement
      horizontal sur téléphone ; onglets sur ordinateur, sur toute la largeur.

Radio :

- [ ] Tous les tours annoncés, aucun manquant (*Telem Ratio* ≥ 1:16).
- [ ] Pose de ligne à l'arrêt (ligne armée, départ compté) et lancée.

### 11.2 Bancs matériels (à faire dans l'ordre)

| # | Banc | Critère | État |
|---|---|---|---|
| 1 | GNSS 25 Hz + écriture continue en flash 30 min | aucune trame GNSS perdue pendant les écritures | à faire |
| 2 | Trame GPS `0x02` vers la MT12 | capteur GPS découvert, coordonnées exactes | à faire |
| 3 | Trame `0x21` « L12 21.345 » | texte affiché sur la MT12 | ✅ |
| 4 | Script lisant `getValue("FM")` | chaîne lue telle quelle | ✅ |
| 5 | Lecture de CH8 (trame `0x16`) | seuils ±60 % franchis de façon fiable (inter et GV9) | à faire (mixage ✅) |
| 6 | Trames `0x0C` / `0x0D` | sans objet tant qu'aucune mesure moteur n'existe | — |
| 7 | Chrono firmware compilé sur PC | mêmes résultats que la console à 1 ms | ✅ (CI) |
| 8 | Wi-Fi actif près d'une radio ELRS 2,4 GHz | aucune baisse mesurable de LQ | à faire |

Bancs à la réception (`b` à chaque étape) : carte seule → + GPS dehors →
+ IMU → + récepteur → console (connexion, enregistrement, téléchargement,
effacement) → roulage (pose de ligne, tours radio, comparaison console).

---

## 12. Pièges connus — chapitre prioritaire

> Chacun de ces points vient d'une contrainte documentée de la plateforme ou
> d'un bug réellement rencontré. Un assistant qui repart de zéro les
> reproduira.

### 12.1 Longueur du nom Bluetooth

Pseudo **≤ 16 caractères** : au-delà, la trame d'annonce déborde et le nom est
tronqué (et la console ne le trouve plus si le préfixe saute).

### 12.2 Réassemblage et longueur des écritures

- **Resynchronisation** (§8.3) : avancer de la longueur annoncée par une trame
  invalide fait sauter le début de la suivante. Symptôme : valeurs délirantes
  intermittentes (32,77 g = 32767, 7 670 544 km/h). Toujours avancer de 2.
- **Longueur des écritures** : la pile Bluetooth doit accepter les commandes
  de plus de 20 octets. NimBLE accepte 512 octets par défaut ; une pile qui
  plafonne à 20 rejette les écritures plus longues **sans aucun rappel** (les
  commandes disparaissent sans trace).

### 12.3 Ligne trop longue

Une ligne de 18 m recoupe le tracé à l'opposé d'un circuit RC : 10 traversées
pour 5 tours, temps faux. Remède : ligne courte (4 m de demi-longueur) **et**
proximité de l'ancrage **et** sens de passage.

### 12.4 Effacement non annulable

Journal en ajout linéaire : s'arrêter à mi-chemin laisserait des secteurs non
effacés devant le pointeur d'écriture. L'effacement va à son terme (il ne porte
que sur la zone utilisée, il est court). Il se fait **à rebours** (§4.3).

### 12.5 Tuiles satellite : l'erreur qui n'en est pas une

Au-delà de la couverture, Esri renvoie une tuile grise « Map data not yet
available » **avec un code HTTP 200**. Indétectable : plafonner à 19.

### 12.6 Ordre de déclaration en JavaScript

Une variable `let` lue avant sa déclaration lève « Cannot access 'X' before
initialization » (zone morte temporelle) : toute la page devient inerte.
Déclarer **toutes** les variables d'état en tête de script. Une vérification de
syntaxe ne le voit pas : **exécuter** la page (`tests/web`).

### 12.7 L'état décide, pas le clic

Réagir au *clic* sur un mode plutôt qu'à l'*état* du mode produit des
incohérences dès que l'ordre des actions change. Toujours recalculer depuis
l'état (même principe pour le mode circuit / dragster déduit des lignes posées).

### 12.8 Saturation de l'accéléromètre

À ±8 g, les trois axes plafonnent à 7,99 g aux atterrissages, avec des valeurs
qui semblent plausibles. Régler sur **±16 g** (`ACCEL_RANGE_G 16`). À surveiller
aux premiers roulages : des plateaux à 15,99 g imposeraient ±32 g (§12.17).

### 12.9 Le module ne pilote rien

Direction et ESC restent sur les sorties PWM du récepteur. Si le firmware
plante, la voiture reste pilotable. Aucune sortie de voie par le module, même
« pour simplifier le câblage ».

### 12.10 Émetteurs 2,4 GHz et radio de commande

Un Wi-Fi ou un Bluetooth actif à quelques centimètres d'un récepteur
ExpressLRS peut dégrader la liaison de commande : coupure **sans condition** au
roulage (§6.1). Un seul comportement, quel que soit le système radio.

### 12.11 Broches de l'ESP32-S3 N16R8

GPIO 35-37 (PSRAM octale) : les toucher fige ou redémarre la carte. GPIO 19-20 :
USB (plus de port de programmation). GPIO 0, 3, 45, 46 : lues au démarrage, un
niveau imposé peut empêcher le démarrage. ADC2 inutilisable Wi-Fi actif.

### 12.12 Boutisme CRSF

CRSF en **big-endian**, protocole TrimBox en **little-endian**, dans le même
firmware. Conversions par fonctions dédiées, jamais de `memcpy`. Symptôme : la
position GPS sur la radio à l'autre bout du monde, la console correcte.

### 12.13 Gel du cache pendant l'écriture en flash

§4.3. Symptôme d'un tampon trop petit : trames CRSF ou GNSS perdues de façon
intermittente, uniquement pendant un enregistrement, jamais au repos.

### 12.14 L'ESP32-S3 n'accepte pas le 5 V

Ses entrées ne sont **pas** tolérantes au 5 V. Un signal de niveau inconnu est
présumé à 5 V et passe par un pont diviseur. (Le TX du M100 Mini est à 3,3 V.)

### 12.15 La télémétrie radio sert à afficher, pas à calculer

Lente et avec pertes (quelques trames par seconde et par type). Les calculs se
font dans le module ; la radio n'affiche que des résultats.

### 12.16 Même algorithme, deux implémentations

Le chrono existe en JavaScript (console) et en C++ (firmware). Toute
modification de l'un **doit** être répercutée dans l'autre et validée par
`console_check.js`. Des résultats différents entre radio et console sur la même
session sont un bug.

### 12.17 Plage de l'accéléromètre et seuil de la console

Le seuil de plausibilité (20 g) est calibré pour ±16 g. Passer l'IMU à ±32 g
impose de le relever **en même temps** (35 g) : c'est la **seule** modification
admise dans `plausible()`, sur demande explicite. La plage réelle est transmise
par `FF F0` (champ `PLAGE_G`).

### 12.18 Alimentation de la DevKitC-1

Jamais le BEC (6 à 8,4 V) sur la broche 5V de la carte : régulateur 5 V en
amont obligatoire. Ne pas cumuler USB et régulateur sans vérifier le schéma
(retour de courant vers l'USB).

### 12.19 Émulateur QEMU : trois limites qui ne sont pas des bugs

La variante `-DTRIMBOX_QEMU` (jamais flashée sur la carte) contourne :

- flash en **QIO** : lectures décalées de 2 octets (mémoire « pleine »,
  configuration jamais relue) → **DIO** pour l'émulateur seulement ;
- **UART2 non émulé** : GNSS sur UART0 (partagé avec le journal, trames UBX
  extraites du flux texte), CRSF sur UART1 ;
- **RMT non émulé** : `rgbLedWrite()` attend indéfiniment → DEL désactivée.

Ni Bluetooth ni radio Wi-Fi dans l'émulateur : à valider sur la carte. Sans
`libslirp` sur la machine, QEMU ne démarre pas ; lancé avec `-nic none`, une
bibliothèque factice suffit.

### 12.20 GNSS allumé en permanence

Le GNSS est allumé dès le démarrage : chrono radio et pose de ligne doivent
fonctionner sans enregistrement. L'enregistrement, lui, repart toujours à
l'arrêt après une coupure (§4.5).

### 12.21 Fonctions appelées mais jamais définies

`refreshStatus()` / `refreshConfig()` étaient appelées mais définies nulle part
(console 1.7.1) : invisible en démo, état mémoire à zéro et téléchargement
refusé avec un vrai module. Même famille que §12.6 : seule l'exécution du vrai
parcours (`tests/web`) le révèle.

### 12.22 Pas de bibliothèque `table` dans les scripts de télémétrie EdgeTX

Constaté sur la MT12 : au premier tour reçu, « attempt to index a nil value
(field 'table') ». Le script se chargeait normalement : le plantage n'arrivait
qu'au premier message de chrono. `luac -p` ne voit rien. Garde-fou :
`tests/lua/test_trmbox.lua` et `tests/lua/radio_bridge.lua` posent `table = nil`
avant de charger le script. **À conserver.**

### 12.23 Les « optimisations » d'assistant

Trois corruptions de la console par un LLM local, présentées comme des
optimisations : `distM` réécrite en radians (distances divisées par ~57,
tracé invisible), `anal.pts` / `anal.scr` supprimés « pour libérer la mémoire »
(carte, tours et exports cassés), appel à `refreshTheme()` remplacé par un
commentaire « Optimisation » (textes des canevas invisibles en thème sombre).
D'où la liste des fonctions protégées (§8.13) et la validation par exécution.

### 12.24 Incrustations sur fond fixe, couleurs fixes

Une incrustation à fond sombre **fixe** (infobulle de la carte) qui utilise les
couleurs du thème (`var(--ink)`) devient illisible en thème clair (texte sombre
sur sombre). Les incrustations ont leurs propres couleurs (texte `#E6E1F2`,
valeurs blanches, étiquettes `#ADA5C8` : contraste ≥ 7:1), indépendantes du
thème et de l'accent.

### 12.25 Canevas à taille fixe étirés

Un canevas de 900 px de large affiché sur 340 ou 950 px avec une hauteur CSS
fixe déforme tracés et textes. La résolution interne suit la taille affichée
(§8.10) ; tout calcul de position souris passe par le rapport largeur interne /
largeur affichée, jamais par une largeur écrite en dur.

---

## 13. ESC XC-E8 (X-Bus) — piste close

**Conclusion du 22 septembre 2026 : la prise X-Bus du variateur ne fournit
aucune donnée.**

| Essai | Résultat |
|---|---|
| Analyseur logique, 4 MHz, 1,25 s | ligne **plate**, aucune transition |
| Sonde ESP32, test de tirage | la ligne remonte en 2 µs : résistance de tirage côté ESC, le fil est bien relié |
| Sonde ESP32, écoute passive 9 600 à 1 Mbaud | rien |
| Poignée de main SRXL2, 11 adresses, 115 200 et 400 000 bauds | aucune réponse |
| Application : *PLine* | X.BUS, Max.Brake, Max.Rev, Acc, DragBrake — que des **entrées** |
| Application : *ProtocolId* | 0 à 15 : un numéro d'appareil sur le bus |

Conséquences : régime, tension, courant et températures **inaccessibles** par
cette prise ; la tension de batterie vient du récepteur ; GPIO 5 et 6 libres ;
type `0x28` et message `FF 28` réservés. Pistes en réserve si le besoin
revient : régime par un fil Hall de la nappe de capteurs du moteur (4 fronts par
tour avec 2 paires de pôles), température par sa thermistance, tension par pont
diviseur sur l'ADC1. Outil conservé : `tools/xbus_probe/`.

---

## Annexe A — Correspondance des anciens renvois

Les anciens documents numérotaient « v1 §x » (carte XIAO) et « v2 §y ». Les
renvois du code ont été convertis ; cette table sert pour les historiques.

| Ancien | Nouveau | | Ancien | Nouveau |
|---|---|---|---|---|
| v1 §1 | §1 | | v2 §0 | §0 |
| v1 §3, §3.x | §3, §3.x | | v2 §2.x | §2.x |
| v1 §4.1 | §4.1 | | v2 §3.2 | §4.1 |
| v1 §4.2 | §4.2 | | v2 §3.3 | §4.3 |
| v1 §4.3 | §2.4, §3.11 | | v2 §3.4 | §4.4 |
| v1 §4.4 | §4.4 | | v2 §4, §4.1-4.3 | §5, §5.1-5.3 |
| v1 §4.5 | §4.5 | | v2 §4.4 | §5.4 |
| v1 §4.6 | §4.6 | | v2 §4.5 | §5.5 |
| v1 §4.7 | §4.7 | | v2 §4.6 | §5.6 |
| v1 §4.8 | §4.8 | | v2 §5 | §13 |
| v1 §4.9 | §4.12 | | v2 §6, §6.3 | §3, §3.4 |
| v1 §5.2 | §8.3 | | v2 §6.1 | §3.2 |
| v1 §5.3 | §8.4 | | v2 §6.2 | §3.11 |
| v1 §5.4 | §8.5, §8.6 | | v2 §6.4 | §8.7 |
| v1 §5.5 à §5.10 | §8.8 à §8.12 | | v2 §7, §7.1 | §6, §6.1 |
| v1 §6 | §10.1 | | v2 §7.2 | §7 |
| v1 §8 | §11 | | v2 §7.3 | §6.4 |
| v1 §8.3 | §10.2 | | v2 §8 | §9 |
| v1 §9 | §12 | | v2 §9 | §11.2 |
| v1 §9.4 | §12.1 | | v2 §9.1 | §10.1 |
| v1 §9.5, §9.5a, §9.5b | §12.2 | | v2 §10 | §12 |
| v1 §9.6 | §12.3 | | v2 §10.1 à §10.14 | §12.9 à §12.22 |
| v1 §9.7 | §12.4 | | | |
| v1 §9.8 | §12.5 | | | |
| v1 §9.9 | §12.6 | | | |
| v1 §9.10 | §12.7 | | | |
| v1 §9.11 | §12.8 | | | |

Supprimés (propres à la carte XIAO) : variante mbed, conflit de type `File`,
`adafruit-nrfutil`, erreur de copie UF2, flash QSPI P25Q16H, compilation
Seeeduino / PlatformIO.

## Annexe B — Points ouverts

| Point | Bloquant pour | Résolution |
|---|---|---|
| Révision de la DevKitC-1 (DEL sur GPIO 48 ou 38) | §4.11 | Sérigraphie de la carte |
| Plage accéléromètre suffisante à ±16 g | §2.5, §12.17 | Premiers roulages : plateaux à 15,99 g ? |
| `IMU_AVERAGE` 1 ou 0 | §4.9 | Même roulage analysé dans les deux modes |
| Repli GPS seul si Galileo empêche les 25 Hz | §4.8 | Mesure dehors (`b`) |
| Pose de ligne à l'arrêt, enregistrement automatique | §5.5, §4.6 | Roulage |
| Bluetooth coupé au roulage | §6.3 | `b` en roulant |
| Bancs 1, 2, 5, 8 | §11.2 | Matériel |
| `FF 29` en direct | §3.4 | Le firmware ne l'émet qu'au téléchargement ; la console sait déjà le journaliser |
| Nettoyage console : commentaires « Optimisation » orphelins, `addSession()` / `MAX_SESSIONS` jamais appelés, contraste du texte « démonstration » en thème clair, durées absentes de la liste des cadences en démo | §8 | Développement console |

## Annexe C — Glossaire

| Terme | Définition |
|---|---|
| **Fix** | Verrouillage du récepteur GNSS. Type 3 = position 3D valide |
| **iTOW** | *Time of Week*, horodatage GNSS en millisecondes |
| **CRSF** | Protocole série Crossfire, repris par ExpressLRS, entre récepteur et contrôleur |
| **ELRS** | ExpressLRS, système radio 2,4 GHz |
| **FM** | Capteur « mode de vol » d'EdgeTX, alimenté par la trame CRSF `0x21` |
| **GV9** | Variable globale n° 9 du modèle EdgeTX |
| **OTA** | *Over The Air* : mise à jour du firmware sans câble |
| **MTU** | Taille maximale d'un paquet Bluetooth négociée à la connexion |
| **NUS** | *Nordic UART Service*, service Bluetooth série |
| **Portail captif** | Mécanisme qui fait ouvrir une page au téléphone à la connexion Wi-Fi |
| **VBO** | Format de télémétrie Racelogic (RaceChrono, Circuit Tools) |
| **Tuile** | Image carrée de 256 px composant un fond cartographique |
