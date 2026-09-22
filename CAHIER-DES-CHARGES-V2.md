# TrimBox DIY v2 — Cahier des charges

**Version du document :** 2.1 — 20 septembre 2026
**Statut :** firmware 2.0-a4 (console embarquée, Wi-Fi automatique, mise à jour par Wi-Fi) et script Lua écrits, testé sur PC, dans un navigateur et sous émulateur ; pas encore sur matériel.
Les choix faits pendant l'implémentation sont signalés par **[2.1]**.
**Prérequis :** `CAHIER-DES-CHARGES.md` (v1). Ce document **ne le remplace pas** :
il décrit uniquement ce qui change ou s'ajoute. Tout point non traité ici
reste régi par la v1.

---

## Comment utiliser ce document

Ordre de fourniture à un assistant de code :

1. **v1 §1 à §3** (contexte, protocole BLE), puis **v1 §9**.
2. **v2 §0** (ce qui ne change pas) et **v2 §10** (nouveaux pièges).
3. Le chapitre du module travaillé (§2 à §8).

Les éléments marqués **[À VALIDER]** n'ont pas été vérifiés sur matériel. Ce
ne sont pas des certitudes : le banc d'essai correspondant (§9) doit passer
avant de bâtir dessus.

---

## 0. Ce qui ne change pas

C'est la règle la plus importante de la v2 : **le protocole et la console
restent ceux de la v1.**

- Trame `B5 62 | classe | id | longueur LE | charge | Fletcher-8` : inchangée.
- Messages `FF 01` à `FF 30` et `FF F0` : même sens, mêmes longueurs, mêmes
  offsets. Les nouveaux messages s'ajoutent (§6.3), aucun n'est modifié.
- Service Bluetooth Nordic UART, mêmes UUID, nom annoncé commençant par
  `TrimBox`.
- Emplacements mémoire de 81 octets (`[type][80 octets]`), types `0x21` et
  `0x26` inchangés.
- Configuration en double exemplaire, pas de reprise automatique
  (`AUTO_RESUME_RECORDING 0`), validation des enregistrements à la lecture :
  les quatre mécanismes de v1 §4.5 restent **obligatoires**.
- Fonctions protégées de la console (`distM`, `feed`, `anal.pts`/`anal.scr`,
  chaîne de filtrage) : toujours interdites de modification.

---

## 1. Objectifs de la v2

| # | Objectif | Motif |
|---|---|---|
| 1 | Passer du XIAO nRF52840 à un **ESP32-S3** | 3 ports série, 16 Mo de flash, Wi-Fi |
| 2 | **Télémétrie radio** CRSF (Crossfire ou ExpressLRS) vers une RadioMaster MT12 | Chrono et état visibles sur la radio, en roulant |
| 3 | **Ligne virtuelle posée depuis la radio**, chronométrage calculé dans le module | Précision 25 Hz, indépendante du débit radio |
| 4 | **Script Lua EdgeTX** pour l'écran 128×64 de la MT12 | Affichage, annonces vocales, pose de ligne |
| 5 | Lecture de l'**ESC XC-E8** par son port X-Bus | Régime, tension, courant, températures |
| 6 | Mise à jour **par Wi-Fi** depuis un téléphone | Remplace l'OTA non réalisé de la v1 |
| 7 | Console servie **par le module lui-même** en Wi-Fi (facultatif) | Fonctionne sur iPhone, sans réseau au bord de la piste |

### Hors périmètre v2

- Pilotage des servos ou de l'ESC par le module : **interdit** (§10.1).
- Compatibilité binaire des firmwares v1 et v2 : ce sont deux cibles distinctes.
- Application mobile native, cloud : inchangé depuis la v1.

---

## 2. Matériel

### 2.1 Nomenclature

| Élément | Référence | Remarque |
|---|---|---|
| MCU | **ESP32-S3**, module **N16R8** | 16 Mo de flash, 8 Mo de PSRAM octale |
| Carte | **ESP32-S3-DevKitC-1, WROOM-1-N16R8** (retenue) | Deux ports USB-C : « USB » (natif, à utiliser) et « UART » (pont série) |
| Centrale inertielle | **LSM6DS3TR-C**, module générique I2C (retenu) — alternative ±32 g : LSM6DSO32 | Même puce que le XIAO Sense : pilote et réglages v1 réutilisables. **±16 g** (v1 §9.11). Adresse I2C **0x6A ou 0x6B** selon la broche SDO/SA0 : détection automatique au démarrage. Vérifier que le module accepte 3,3 V |
| GNSS | **HGLRC M100 Mini** (puce u-blox M10) (retenu) | Alimentation 5 V, UART. Vitesse d'usine à vérifier au premier démarrage (détection automatique 38 400 / 115 200). Configuration **renvoyée à chaque démarrage** : ce type de module ne garde pas ses réglages sans pile de sauvegarde |
| Liaison radio | **RadioMaster ER5C-i** (ExpressLRS 2,4 GHz, 5 sorties PWM) (retenu) | Une paire de sorties en CRSF série (§4.1) |
| ESC | **XC-E8** | Port X-Bus, prise servo 3 fils |
| Alimentation | Régulateur abaisseur 5 V, ≥ 1 A | Alimenté par le BEC de l'ESC (6,0 à 8,4 V) |

### 2.2 Alimentation

Le module est alimenté par le **BEC de l'ESC**, réglable entre 6,0 et 8,4 V.
Cette tension ne doit **jamais** atteindre directement la carte ESP32 : un
régulateur abaisseur 5 V alimente la carte et le GNSS (le M100 Mini
s'alimente en 5 V, ses signaux UART sont en 3,3 V — **[À VALIDER]** au
multimètre sur sa broche TX). Le régulateur 3,3 V de la carte alimente l'IMU.
GPS_EN commande le 5 V du GNSS par un interrupteur de charge ou un MOSFET
canal P.

La LiPo 1S dédiée de la v1 disparaît. Conséquence acceptée : le module ne
fonctionne que lorsque la batterie de la voiture est branchée.

L'octet 67 du message de données (batterie, v1 §3.5) contient désormais le
**niveau de la batterie de propulsion**, estimé à partir de la tension
fournie par l'ESC (§5). Le nombre d'éléments est déterminé au démarrage. Le
bit 7 (en charge) est toujours à 0.

### 2.3 Brochage (ESP32-S3 N16R8)

```
Fonction        GPIO   Périphérique       Remarque
GNSS RX         18     UART1              ← TX du module GNSS
GNSS TX         17     UART1              → RX du module GNSS
GPS_EN          10     sortie             Commande d'alimentation du GNSS
CRSF RX         16     UART2  420000 b    ← TX du récepteur (voies)
CRSF TX         15     UART2  420000 b    → RX du récepteur (télémétrie)
XBUS RX          5     UART0 (remappé)    ← fil X-Bus, via adaptation de niveau (§5.2)
XBUS TX          6     UART0 (remappé)    Réservé, si l'ESC doit être interrogé
IMU SDA          8     I2C0
IMU SCL          9     I2C0
IMU INT1         7     entrée
VMES             1     ADC1               Tension 5 V interne (surveillance)
LED          48 / 38   sortie             DEL RGB de la carte : GPIO 48 sur DevKitC-1 v1.0,
                                          GPIO 38 sur v1.1 (lire la sérigraphie de la carte)
BOUTON           0     entrée             Bouton BOOT, réutilisé (§7.1)
Journal série   USB    CDC natif          UART0 libéré pour l'ESC
```

**Broches interdites** (§10.3) : 35, 36, 37 (PSRAM octale), 19 et 20 (USB),
3, 45, 46 (configuration au démarrage). Les entrées analogiques utilisent
**exclusivement l'ADC1** (GPIO 1 à 10) : l'ADC2 est inutilisable quand le
Wi-Fi est actif.

### 2.4 Capacité mémoire

Table de partitions (`partitions.csv`, dans le dossier du croquis) :

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
- GNSS seul à 25 Hz : **≈ 1 h 43**.
- GNSS 25 Hz + ESC à 25 Hz regroupé par 5 (§5.4) : 30 emplacements/s,
  soit **≈ 1 h 26**.

Deux partitions applicatives de 2 Mo : un firmware avec NimBLE, Wi-Fi et
serveur web tient dans cette taille. Si la compilation dépasse **1,8 Mo**, le
signaler plutôt que de réduire le journal en silence.

---

## 3. Portage du firmware

### 3.1 Correspondances

| v1 (nRF52840) | v2 (ESP32-S3) |
|---|---|
| Bluefruit | **NimBLE-Arduino** |
| Flash QSPI P25Q16H, pilote SPIFlash | Partitions `cfg` et `log`, API `esp_partition_*` |
| `Serial1` GNSS | `HardwareSerial(1)` sur GPIO 18/17 |
| IMU intégrée (Wire interne) | LSM6DSO sur I2C0, GPIO 8/9 |
| Veille système nRF | `esp_light_sleep` ; pas de veille profonde en enregistrement |
| UF2 par glisser-déposer | **OTA Wi-Fi** (§7.2) ; USB en secours |
| `__DATE__ __TIME__` | Inchangé (empreinte `FF F0`) |

### 3.2 Architecture **[2.1]**

La boucle unique et non bloquante de la v1 est **conservée**, dans le même
ordre (protocole d'abord), sur le cœur 1. La pile Bluetooth NimBLE tourne sur
le cœur 0 ; ses rappels se contentent de déposer les octets reçus dans un
tampon protégé.

Le découpage en tâches envisagé en 2.0 a été abandonné : une écriture en
flash gèle **les deux cœurs** de toute façon (cache désactivé), des tâches
n'auraient donc rien fait gagner, et la boucle unique a fait ses preuves sur
la v1.

L'exigence v1 §4.2 reste entière : **une seule file d'émission** pour toutes
les trames sortantes vers la console, données en direct comprises, sur
quelque transport que ce soit (BLE ou WebSocket).

### 3.3 Écriture en flash interne **[2.1]**

L'écriture ou l'effacement de la flash interne **suspend le cache
d'instructions** : tout code hors IRAM est gelé pendant l'opération.

- Écriture d'un emplacement (81 octets, 1 ou 2 pages) : environ 1 ms.
  Les interruptions UART ne sont **pas** en IRAM dans le core Arduino
  précompilé ; le FIFO matériel de 128 octets couvre ce délai, y compris à
  420 000 bauds (≈ 3 ms de marge).
- **Aucun effacement pendant un enregistrement** : l'effacement porte sur
  toute la zone utilisée, d'un seul tenant, à la demande de la console ;
  tout ce qui suit le pointeur d'écriture est donc déjà vierge.
  L'« effacement anticipé » de la 2.0 est inutile.
- **Effacement à rebours**, du dernier secteur utilisé vers le premier :
  interrompu par une coupure, le journal reste contigu depuis l'adresse 0 et
  la recherche de fin par dichotomie reste valable.
- Au démarrage, la zone qui suit la fin du journal est contrôlée. Si elle
  n'est pas vierge (effacement interrompu en plein secteur), les
  emplacements douteux sont marqués « bourrage » (type `0x00`, obtenu en
  programmant des bits à 0, sans effacement) et le secteur suivant est
  effacé.
- Tampons de réception : 4096 octets (GNSS), 2048 (CRSF).

### 3.4 Configuration persistante

La structure `RecConfig` et le double exemplaire A/B de la v1 sont conservés
**à l'identique**, dans la partition `cfg` : A à l'offset 0, B à l'offset
4096. `LineConfig` : A à 8192, B à 12288. Sérialisation octet par octet
(jamais de `memcpy` de structure), CRC32 IEEE. Le mécanisme de NVS de l'ESP-IDF n'est **pas** utilisé pour cette
structure : on garde un mécanisme déjà éprouvé en conditions d'arrachement.

Ajouts, dans une seconde structure `LineConfig` sauvegardée selon le même
schéma A/B (partition `cfg`, secteurs supplémentaires si nécessaire) :

```c
struct LineConfig {
  uint32_t magic;          // 0x4C494E45 ("LINE")
  uint8_t  version;        // 1
  uint8_t  mode;           // 0 = aucune, 1 = circuit, 2 = dragster
  uint8_t  crsfChannel;    // voie de commande, 1 à 16 (défaut 8)
  uint8_t  reserved;
  int32_t  startLat, startLon;   // deg × 1e7
  int32_t  startHeading;         // deg × 1e5
  int32_t  finishLat, finishLon; // dragster uniquement
  int32_t  finishHeading;
  uint32_t seq;
  uint32_t crc;
};
```

---

## 4. Télémétrie radio CRSF

### 4.1 Liaison physique

- UART2, **420 000 bauds, 8N1, non inversé**, deux fils (RX et TX séparés).
- Le module se comporte comme un **contrôleur de vol** vis-à-vis du récepteur.
- Récepteur **Crossfire Nano RX** : configurer deux sorties en CRSF TX/RX,
  les deux autres restent en PWM (direction, ESC).
- Récepteur retenu : **RadioMaster ER5C-i** (ExpressLRS 3.3 d'usine).
  Configuration dans l'interface web du récepteur (mode Wi-Fi), onglet
  *Model* :
  - sorties **2 et 3** en **Serial TX / Serial RX** (protocole CRSF,
    420 000 bauds) — affectation par défaut des récepteurs PWM ELRS,
    **[À VALIDER]** sur l'onglet *Model* de ce récepteur ;
  - direction : sortie 1 → voie CH1 ;
  - gaz (ESC) : sortie 4 → voie **CH2** (réaffectée, puisque la sortie 2 sert
    au série) ;
  - sortie 5 libre (par exemple CH3 pour un servo annexe).
  - Croisement obligatoire : **TX du récepteur → RX de l'ESP32 (GPIO 16)**,
    **RX du récepteur ← TX de l'ESP32 (GPIO 15)**. Masse commune.
- Aucune modification des mixages de la MT12 : seule la correspondance
  sortie ↔ voie change, dans le récepteur.

### 4.2 Format de trame CRSF

```
[0xC8] [longueur] [type] [charge ...] [CRC8]
longueur = 1 (type) + taille(charge) + 1 (CRC)
CRC8     : polynôme 0xD5 (DVB-S2), calculé sur type + charge
```

⚠️ **Les entiers CRSF sont en BIG-ENDIAN**, à l'inverse du protocole TrimBox
(little-endian). Voir §10.4.

### 4.3 Trames exploitées

**Reçues du récepteur :**

| Type | Nom | Usage |
|---|---|---|
| `0x16` | Voies RC (16 × 11 bits) | Lecture de la voie de commande de ligne (§4.5) |
| `0x14` | Statistiques de liaison | RSSI et qualité de liaison, enregistrés (§5.4, octet `flags`) |

**Émises vers le récepteur** (ordonnancées en tourniquet, **une trame émise
juste après chaque trame de voies reçue**, pour ne pas saturer la liaison) :

| Type | Nom | Cadence cible | Contenu |
|---|---|---|---|
| `0x02` | GPS | 5 Hz | lat, lon (deg × 1e7), vitesse (km/h × 10), cap (deg × 100), altitude (m + 1000), satellites |
| `0x08` | Batterie | 2 Hz | tension ESC (dV), courant (dA), mAh consommés, % restant |
| `0x21` | Mode de vol (texte) | à chaque événement, puis rappel 1 Hz | Messages chrono et état (§4.4) |
| `0x0C` / `0x0D` | Régime / Température | 2 Hz | **[À VALIDER]** : prise en charge par EdgeTX et par le relais ELRS |

### 4.4 Messages texte vers la radio (trame `0x21`)

La trame « mode de vol » est transmise et affichée par tous les systèmes
CRSF. C'est donc le **canal de référence** pour le chrono. Le texte fait au
plus **15 caractères** ASCII, terminé par un octet nul.

| Préfixe | Exemple | Sens |
|---|---|---|
| `L` | `L12 21.345+0.35` | **[2.1]** Tour (ou parcours) 12 en 21,345 s, 0,35 s de plus que le meilleur **précédent** (négatif = nouveau meilleur). Écart omis au premier tour ou si le texte dépasse 15 caractères |
| `R` | `R 0-50 2.184` / `R 50m 3.012` | Dragster : chrono intermédiaire |
| `S` | `S REC C` | **[2.1]** État (`REC` / `PAUSE` / `STOP` / `NOFIX`) puis lignes posées : `C` circuit, `D` dragster, `-` aucune |
| `K` | `K DEPART OK` / `K ARRIVEE OK` / `K VIT FAIBLE` / `K PAS DE FIX` / `K EFFACE` | Accusé de pose de ligne |
| `W` | `W WIFI ON` / `W WIFI OFF` | **[2.1]** Point d'accès de la console allumé / coupé |
| `U` | `U REDEMARRAGE` / `U VALIDATION` / `U MAJ OK` | **[2.1]** Mise à jour du firmware |

**[2.1] Un message à la fois.** Le récepteur ExpressLRS ne garde que le
**dernier** message « mode de vol » en attente de transmission : deux
messages envoyés coup sur coup, le premier est perdu. D'où :

- **un seul message par tour** (les `B` et `D` séparés de la 2.0 sont
  supprimés ; le script Lua déduit le meilleur tour) ;
- une **file** côté module : chaque message est tenu **seul pendant 0,7 s**
  (renvoyé toutes les 150 ms), puis le suivant ; le rappel d'état `S` n'est
  émis que file vide ;
- le script Lua ne traite un message **qu'à son changement**.

Banc : `tests/qemu/sim.py` simule ce comportement du récepteur (dernier
message seulement, un toutes les 330 ms) et vérifie qu'**aucun tour ne
manque** côté radio.

Le temps de tour est formaté **dans le module**, en millisecondes entières :
jamais de virgule flottante dans la chaîne.

### 4.5 Pose de ligne par une voie

C'est le **mécanisme de référence**, car les voies passent toujours, quels que
soient le système radio et sa configuration.

- Voie de commande : `LineConfig.crsfChannel`, **CH8 par défaut**.
- Valeur neutre : entre −30 % et +30 %.
- **Haut (> +60 %) maintenu 0,5 s** : pose la ligne de **départ** (ou la
  ligne unique en circuit).
- **Bas (< −60 %) maintenu 0,5 s** : pose la ligne d'**arrivée** (dragster).
- **Bas maintenu 3 s** : efface toutes les lignes.
- Retour au neutre obligatoire avant une nouvelle commande (anti-rebond).

**Mode déduit automatiquement :** une seule ligne posée donne le mode
circuit, une ligne de départ et une ligne d'arrivée donnent le mode dragster.
Aucune commande de mode n'est nécessaire (v1 §9.10 : l'état décide, pas le
clic).

**Géométrie de la ligne posée :**

- Position : **interpolée** entre les deux points GNSS qui encadrent
  l'instant de la commande.
- Orientation : **perpendiculaire au cap** à cet instant.
- Refus si la vitesse est **inférieure à 2 m/s** : le cap n'est pas fiable.
  Réponse `K VIT FAIBLE`.
- Demi-longueur **4 m**, identique à la console.
- Sauvegarde immédiate dans `LineConfig`, qui survit à une coupure.

### 4.6 Chronométrage embarqué

**Portage à l'identique** de l'algorithme de la console (v1 §5.4) :

1. Intersection segment/segment.
2. Proximité de l'ancrage : `halfM × 1,15`.
3. Sens : produit scalaire avec le cap de référence > 0,3.
4. Déduplication : 1,5 s.
5. Temps interpolé entre les deux échantillons qui encadrent la traversée.

Mêmes constantes, **aucune « optimisation »**. Les bancs de test v1 §8.3 n°2
et n°3 doivent passer **sur le code C++ du firmware**, compilé sur PC (§8.2).

Les franchissements sont aussi **enregistrés en mémoire** (type
d'emplacement `0x29`, §6.2) : la console peut ainsi retrouver les tours
exacts calculés en course.

---

## 5. ESC XC-E8 (X-Bus)

### 5.1 État des connaissances

- Connecteur : prise servo 3 fils, **+ (BEC) / − / signal X-Bus**.
- Protocole : **inconnu**. Aucune documentation publique trouvée.
- Sens du dialogue (émission spontanée ou interrogation) : **inconnu**.
- Niveau électrique : **inconnu**. À mesurer avant tout branchement
  (annexe A, étape 0).

**Ce chapitre ne peut pas être implémenté avant l'analyse des captures
(annexe A).** Les sections §5.2 à §5.4 fixent le cadre, pas le décodage.

### 5.2 Interface électrique

- Le **fil + de la prise X-Bus ne sert pas** à l'alimentation du module
  (le BEC arrive déjà par le récepteur).
- Masse commune obligatoire.
- **L'ESP32-S3 n'accepte pas le 5 V en entrée.** Si le niveau mesuré dépasse
  3,6 V : pont diviseur **10 kΩ (série) / 20 kΩ (vers la masse)** sur XBUS RX.
- Si l'ESC doit être interrogé (fil unique bidirectionnel) : XBUS TX relié au
  fil par une **résistance de 1 kΩ**, et UART configuré en semi-duplex. Le
  montage définitif dépend des captures.

### 5.3 Données visées

Par ordre de priorité, sous réserve de ce que l'ESC transmet réellement :

1. Tension batterie
2. Régime moteur
3. Température ESC
4. Température moteur
5. Courant
6. Position des gaz vue par l'ESC

### 5.4 Stockage : emplacement `0x28` (lot ESC)

Taille d'emplacement inchangée (81 octets). Un emplacement `0x28` regroupe
**5 échantillons de 16 octets** :

```
Échantillon (16 octets, little-endian)
 0  u32  iTOW (ms)          horodatage GNSS au moment de la lecture
 4  u16  régime (tr/min ÷ 10)
 6  u16  tension (V × 100)
 8  i16  courant (A × 10)
10  u8   température ESC (°C + 40)
11  u8   température moteur (°C + 40)
12  u8   gaz (%)
13  u8   drapeaux : bit 0 ESC en défaut, bit 1 donnée ESC absente,
                    bit 2 liaison radio perdue
14  u8   qualité de liaison radio (LQ, %)
15  u8   RSSI radio (−dBm)
```

**Format provisoire** : champs à confirmer après analyse des captures. Un
champ non disponible est mis à `0` et signalé dans les drapeaux, jamais
laissé à `0xFF` (réservé à la détection des enregistrements interrompus).

Contrôles de validation à la lecture (v1 §4.5.4) : tension ≤ 100 V,
températures ≤ 200 °C (brutes ≤ 240), gaz ≤ 100.

---

## 6. Protocole console — ajouts

### 6.1 Transports

| Transport | Disponibilité | Notes |
|---|---|---|
| Bluetooth (NUS) | Toujours | Identique à la v1 |
| **WebSocket** `ws://192.168.4.1/ws` | Wi-Fi actif (§7) | **Mêmes trames binaires** `B5 62…`, une trame ou un fragment par message |

La console réutilise `feed()` **sans modification** pour le WebSocket : le
transport ne change pas la nature du flux d'octets.

### 6.2 Nouveaux types d'emplacement

| Type | Contenu |
|---|---|
| `0x28` | Lot ESC (§5.4) |
| `0x29` | **[2.1]** Événement de chrono, écrit seulement en enregistrement : `[0]` u32 iTOW (interpolé pour un passage), `[4]` u8 ligne (0 départ, 1 arrivée), `[5]` u8 nature (0 passage, 1 tour, 2 parcours, 3 chrono intermédiaire), `[6]` u16 n°, `[8]` u32 temps (ms), `[12]` u8 type de chrono (0 vitesse, 1 distance), `[14]` u16 valeur ; reste à zéro |

### 6.3 Nouveaux messages

| Classe/ID | Sens | Long. | Rôle |
|---|---|---|---|
| `FF 28` | → console | 80 | Lot ESC, en direct et en téléchargement |
| `FF 29` | → console | 80 | Franchissement, en direct et en téléchargement |
| `FF F1` | ↔ | 0 / 28 | Lecture / écriture de `LineConfig` sans `magic`, `seq`, `crc` (4 octets d'en-tête + 6 × i32) |
| `FF F2` | ↔ | 0 / 1 → 4 | **[2.1]** Wi-Fi. Requête vide → `[allumé, auto, nb appareils, console WebSocket connectée]`. `0` : couper et désactiver l'automatique ; `1` : allumer maintenant (NACK si la voiture roule) ; `2` : automatique seul |

**[2.1] Compatibilité du téléchargement.** Une console v1 ne connaît pas
`0x28` / `0x29` et journaliserait « message inconnu » pour chacun. Ils ne
sont donc envoyés que sur demande : `FF 23` avec **1 octet** :

| Octet | Sens |
|---|---|
| `0x00`, téléchargement en cours | annulation (v1) |
| bit 1 (`0x02`), aucun téléchargement en cours | téléchargement **avec** les emplacements v2 |
| charge vide | téléchargement v1 : `0x21` et `0x26` seulement |

Le nombre annoncé est celui des **emplacements** à parcourir (progression
indicative). Les emplacements `0x21` invalides sont écartés et comptés
(journal série en fin de téléchargement).

La table `EXPECT_LEN` de la console reçoit `0x28:80, 0x29:80`. Elle
**n'accepte aucune autre modification** de `feed()`.

`FF F0` : format inchangé, `MODÈLE` devient `TrimBox DIY S3` et `VERSION` `2.0`.

### 6.4 Console

- Nouveau sélecteur de source : **Bluetooth / Wi-Fi / Démo**.
- Affichage des données ESC en direct et en analyse (courbes régime,
  tension, températures, superposables au profil de vitesse).
- Lignes : bouton « Récupérer les lignes du module » (`FF F1`), qui pose
  dans la console les lignes placées depuis la radio.
- Versionnage : convention inchangée (`CONSOLE_VER`).

---

## 7. Wi-Fi

### 7.1 Activation **[2.1] — automatique à l'arrêt**

Demande utilisateur : le point d'accès s'active **tout seul quand la voiture
est arrêtée**, pour que la console soit là sans rien toucher au stand.

| Règle | Valeur (`config.h`) |
|---|---|
| « À l'arrêt » : vitesse sous 5 km/h, **ou pas de fix**, ou pas de GNSS | `WIFI_STILL_MMS` 1389 mm/s |
| Allumage après un arrêt continu de | `WIFI_AUTO_ON_S` **30 s** |
| « Roule » : au-dessus de 7,2 km/h pendant 3 solutions GNSS de suite (120 ms à 25 Hz) | `WIFI_MOVE_MMS` 2000, `WIFI_MOVE_EPOCHS` 3 |
| Entre 5 et 7,2 km/h | le compteur d'arrêt repart de zéro, rien n'est coupé |
| Puissance d'émission | `WIFI_TX_POWER` 8,5 dBm (portée d'un stand) |

- **La coupure en roulant est inconditionnelle**, y compris si le Wi-Fi a été
  forcé : la liaison de commande passe avant la console (§10.2).
- Juste allumé, le module est réputé à l'arrêt : le point d'accès apparaît
  30 s après la mise sous tension.
- Forçage : **appui long (3 s) sur BOOT**, touche `w` du port série, ou
  `FF F2`. Couper à la main désactive l'automatique jusqu'au prochain
  forçage ou redémarrage (`WIFI_AUTO_DEFAULT`).
- La radio est prévenue : messages `W WIFI ON` / `W WIFI OFF` (§4.4).
- Plus de coupure « 10 min sans client » ni « au démarrage d'un
  enregistrement » (2.0) : démarrer un enregistrement depuis la console Wi-Fi
  couperait sa propre réponse. Le mouvement suffit.

Mode point d'accès : SSID `TrimBox-<pseudo>` (espaces remplacés par `-`),
mot de passe WPA2 défini à la compilation (`WIFI_PASS`, 8 caractères minimum,
`trimbox-rc` par défaut — à personnaliser), canal 6, 2 appareils au plus.

**Portail captif** : un serveur DNS répond `192.168.4.1` à toute requête et
toute adresse autre que la console renvoie `302` vers elle ; le téléphone
propose alors d'ouvrir la page de lui-même.

**Une seule console à la fois** : quand un WebSocket est ouvert, la file
d'émission unique (v1 §4.2) part vers lui ; un nouveau WebSocket remplace
l'ancien.

### 7.2 Mise à jour du firmware **[2.1] — réalisée**

- Voie normale : section **Mise à jour du firmware** de la console 1.7.4
  (visible en Wi-Fi uniquement). Secours : page minimale `GET /update`.
- `POST /update`, corps = le `.bin` brut (`Content-Length` obligatoire,
  `Expect: 100-continue` géré). Transmis **au fil de l'eau** à la partition
  OTA inactive (`esp_ota_begin` avec effacement progressif) : aucune pause
  longue, aucune copie en RAM.
- **Conditions** (sinon refus, message renvoyé tel quel) : enregistrement
  arrêté, voiture à l'arrêt, ni téléchargement ni effacement en cours.
- **Contrôles** (`core/otacheck`, testés sur PC) : octet magique 0xE9, puce
  ESP32-S3 (id 9), taille ≤ partition (le fichier « complet » de 16 Mo est
  refusé d'emblée), **marque `TRIMBOX-S3-FIRMWARE-MARK-v1`** présente dans
  l'image ; puis `esp_ota_end` vérifie le SHA-256. En cas de refus, le corps
  est lu jusqu'au bout pour que le navigateur reçoive l'explication.
- Acceptée : `esp_ota_set_boot_partition`, réponse `200`, redémarrage 1,5 s
  plus tard. Radio : `U REDEMARRAGE`.
- **Retour arrière** (chargeur de démarrage, `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`
  actif dans le core) : le core Arduino confirme d'office tout nouveau
  firmware ; `verifyRollbackLater()` est redéfinie pour reprendre la main.
  Confirmation seulement après **autotest** (mémoire et configuration lues)
  **et 15 s** de fonctionnement (`OTA_VALIDATE_AFTER_S`). Plantage, gel de la
  boucle plus de 5 s (chien de garde `enableLoopWDT`) ou coupure avant :
  retour automatique à la version précédente. Radio : `U VALIDATION` puis
  `U MAJ OK`.
- La console compare l'empreinte de compilation (`FF F0`) avant et après :
  « nouvelle version active » ou « le module est revenu à la précédente ».
- **Premier flashage** : toujours par câble (fichier complet à 0x0).
- ⚠️ `partitions.csv` ne doit **jamais** changer d'une version à l'autre
  installée par Wi-Fi : la table n'est pas réécrite par une mise à jour OTA.

Bancs : `tests/native` (contrôles, envoi HTTP, refus, coupure),
`tests/web` (envoi du vrai `.bin` compilé depuis la console, dans Chromium),
`tests/qemu/ota_rollback.py` (état laissé par une mise à jour reconstitué en
flash ; version saine validée et conservée ; version qui plante → retour sur
`app0`, vérifié avec le vrai chargeur de démarrage).

### 7.3 Console embarquée **[2.1] — réalisée**

- `tools/embed_console.py` compresse `index.html` (gzip, horodatage nul :
  résultat reproductible) en `trimbox_s3/src/console_gz.h` (≈ 48 Ko).
  La CI le régénère avant chaque compilation.
- Serveur HTTP + WebSocket **écrit pour le projet** (`src/core/httpws`),
  sans bibliothèque : il est ainsi compilé et testé sur PC, avec de vraies
  sockets et un vrai navigateur (`tests/web`).
- Console **1.7.3** : source **Wi-Fi** (visible seulement quand la page est
  servie en `http:` par le module), choisie et connectée automatiquement ;
  reconnexion automatique toutes les 3 s tant que l'utilisateur n'a pas
  déconnecté (le point d'accès disparaît quand la voiture roule).
- Fond satellite indisponible (pas d'accès Internet) : désactivé par défaut,
  la console le signale.
- Pas de service worker (origine non HTTPS) ; `localStorage` fonctionne :
  thème et couleur d'accent sont mémorisés **pour l'origine du module**,
  indépendamment de ceux de la page GitHub.
- `Cache-Control: no-cache` : une nouvelle console arrive avec le firmware,
  sans cache à vider.

## 8. Script Lua EdgeTX (MT12) **[2.1] — réalisé**

Fichier `lua/trmbox.lua` → `SCRIPTS/TELEMETRY/trmbox.lua` (**6 caractères**
au plus : limite d'EdgeTX pour les scripts de télémétrie monochromes ; le nom
`trimbox.lua` de la 2.0 ne convient pas). Mode d'emploi : `lua/LISEZMOI.md`.

- **Lecture** : capteur `FM` (`getValue`), et file brute CRSF
  (`crossfireTelemetryPop`, trames `0x21`) si EdgeTX y dépose ces trames ;
  traitement à chaque **changement** de message (§4.4). `run()` et
  `background()` lisent tous les deux : rien n'est perdu quand l'écran n'est
  pas affiché. Plus de 3 s sans message : « PAS DE LIAISON ».
- **Pages** : *Chrono* (dernier tour en grand, n°, écart, meilleur, trois
  tours précédents), *Machine* (vitesse, Vmax, satellites, qualité de
  liaison, RSSI, altitude ; ESC à venir), *Lignes* (mode, actions).
- **Touches** : événements virtuels EdgeTX. Molette = pages ; ENT court =
  page suivante ou action suivante ; ENT long = exécuter (page Lignes) ou
  remise à zéro de l'affichage (page Chrono).
- **Pose de ligne** : `model.setGlobalVariable(8, 0, v)` (GV9), mixage CH8
  = source **MAX, poids GV9, ajouter** (le poids en pourcentage est la
  seule sémantique sûre : `GV9 = 100` → CH8 = +100 %). Départ : +100 pendant
  0,6 s ; arrivée : −100 pendant 0,6 s ; effacement : −100 pendant 3,2 s ;
  une commande à la fois.
- **Sons** : `playNumber(temps en centièmes, 0, PREC2)` à chaque tour ;
  double bip aigu avant l'annonce d'un meilleur tour ; bips d'accusé.
- **Réglage ExpressLRS** : *Telem Ratio* 1:16 ou plus fréquent.

Bancs : `tests/lua/test_trmbox.lua` (API EdgeTX simulée : analyse des
messages, doublons, sons, durées d'impulsion GV9, texte hors écran) ;
`tests/qemu/sim.py --lua` (**chaîne complète** : appui long dans le script →
GV9 → CH8 → firmware sous émulateur → `K DEPART OK` → tours annoncés
`12,00` par le script).

**[À VALIDER] sur la radio** : `getValue("FM")` renvoie une chaîne ; codes
de touches de la MT12 ; lisibilité réelle des polices.

## 9. Bancs d'essai de validation (avant implémentation complète)

À réaliser dans cet ordre. Chacun lève un **[À VALIDER]**.

| # | Banc | Critère de réussite |
|---|---|---|
| 1 | ESP32-S3 + GNSS 25 Hz + écriture continue en flash pendant 30 min | Aucune trame GNSS perdue pendant les effacements de secteur |
| 2 | ESP32-S3 relié au récepteur, émission d'une trame GPS `0x02` factice | Capteur GPS découvert sur la MT12, coordonnées exactes |
| 3 | Idem, trame `0x21` « L12 21.345 » | Texte affiché en télémétrie sur la MT12 |
| 4 | Script Lua minimal affichant `getValue("FM")` | Chaîne lue telle quelle |
| 5 | Lecture de CH8 depuis la trame `0x16` | Seuils ±60 % franchis de façon fiable avec l'inter et avec GV9 |
| 6 | Trames `0x0C` / `0x0D` | Capteurs découverts ; sinon, repli sur texte `0x21` |
| 7 | Chronométrage firmware, compilé sur PC, bancs v1 §8.3 n°2 et 3 | Mêmes résultats que la console, à 1 ms près |
| 8 | Wi-Fi actif près d'une radio ELRS 2,4 GHz | Aucune baisse de qualité de liaison mesurable |
| 9 | Décodeur X-Bus sur captures réelles | 100 % des trames des captures décodées, valeurs cohérentes avec le multimètre |

Le banc n°7 impose un environnement de test **natif** : la logique de
chronométrage vit dans un fichier C++ sans dépendance Arduino
(`lapcore.cpp` / `lapcore.h`), compilé et testé par la CI sur Linux.

### 9.1 Chaîne de compilation

- GitHub Actions et arduino-cli, comme en v1.
- Core `esp32:esp32` **figé à une version précise** (la dernière 3.x stable au
  moment de la mise en place), comme `Seeeduino:nrf52@1.1.13` l'était.
- FQBN : `esp32:esp32:esp32s3`, avec les options `FlashSize=16M`,
  `PSRAM=opi`, `USBMode=hwcdc`, `CDCOnBoot=cdc`,
  `PartitionScheme=custom` (utilise `partitions.csv` du dossier du croquis).
- Bibliothèques : `NimBLE-Arduino`, `SparkFun u-blox GNSS`, bibliothèque de
  l'IMU retenue. Toutes à **version figée**.
- Artefacts : `.bin` applicatif (OTA), `.bin` fusionné (premier flashage USB).
- Étape supplémentaire : tests natifs de `lapcore` (banc n°7) ; la
  compilation échoue si un test échoue.
- Étape supplémentaire : compression gzip d'`index.html` en en-tête C
  (`console_gz.h`) pour §7.3.

---

## 10. Nouveaux pièges connus (v2)

> Ces points sont **anticipés** : ils ne viennent pas encore de bugs
> rencontrés sur ce projet, mais de contraintes documentées de la plateforme.
> Ils doivent être fournis à tout assistant de code au même titre que v1 §9.

### 10.1 Le module ne pilote rien

Direction et ESC restent branchés sur les sorties PWM du récepteur. Si le
firmware plante, la voiture doit rester entièrement pilotable. Aucune sortie
de voie par le module, même « pour simplifier le câblage ».

### 10.2 Wi-Fi et radio 2,4 GHz

Un point d'accès Wi-Fi actif à quelques centimètres d'un récepteur ExpressLRS
en 2,4 GHz peut dégrader la liaison de commande. D'où la coupure automatique
au démarrage de l'enregistrement (§7.1). Le Crossfire (868/915 MHz) n'est pas
concerné, mais la règle s'applique quand même : un seul comportement,
quel que soit le système radio.

### 10.3 Broches de l'ESP32-S3 N16R8

- **GPIO 35, 36, 37** : utilisées par la PSRAM octale. Les toucher fige ou
  redémarre la carte.
- **GPIO 19, 20** : USB. Les réaffecter supprime le port de programmation.
- **GPIO 0, 3, 45, 46** : lues au démarrage. Un niveau imposé par un
  périphérique peut empêcher le démarrage.
- **ADC2** inutilisable Wi-Fi actif : n'utiliser que l'ADC1.

### 10.4 Boutisme CRSF

Le CRSF est en **big-endian**, le protocole TrimBox en **little-endian**. Les
deux coexistent dans le même firmware. Toute conversion passe par des
fonctions dédiées (`put_be16`, `put_be32`…), jamais par un `memcpy` d'une
structure. Symptôme d'une erreur : position GPS affichée à l'autre bout du
monde sur la radio alors que la console est correcte.

### 10.5 Gel du cache pendant l'écriture en flash

Voir §3.3. Symptôme d'un tampon trop petit : trames CRSF ou GNSS perdues de
façon intermittente, uniquement pendant un enregistrement, jamais au repos.

### 10.6 L'ESP32-S3 n'accepte pas le 5 V

Contrairement à certaines idées reçues, les entrées de l'ESP32-S3 ne sont
**pas** tolérantes au 5 V. Tout signal de l'ESC ou du récepteur dont le niveau
n'a pas été mesuré est présumé à 5 V et passe par un pont diviseur.

### 10.7 Débit de télémétrie radio

La télémétrie radio est **lente et avec pertes** (quelques trames par seconde
et par type). Ne jamais y faire transiter de données destinées au calcul :
elle sert à **afficher des résultats** calculés dans le module.

### 10.8 Même algorithme, deux implémentations

Le chronométrage existe désormais en JavaScript (console) et en C++
(firmware). Toute modification de l'un **doit** être répercutée dans l'autre
et validée par les mêmes bancs (§9, n°7). Des résultats différents entre la
radio et la console sur la même session constituent un bug.

### 10.9 Plage de l'accéléromètre et seuil de la console

Le seuil de plausibilité de la console (`|accél.| ≤ 20 g`, v1 §5.3) est
calibré pour ±16 g. Passer l'IMU à ±32 g impose de relever ce seuil **en
même temps** (35 g), faute de quoi les pics réels seraient rejetés comme
corrompus. C'est la **seule** modification admise dans `plausible()`, et elle
doit être explicitement demandée. La plage réelle est transmise par `FF F0`
(champ `PLAGE_G`).

### 10.10 Alimentation de la DevKitC-1

Le BEC de l'ESC (6,0 à 8,4 V) ne doit **pas** être relié à la broche 5V de
la DevKitC-1 : son régulateur 3,3 V n'est pas prévu pour cette tension. Un
régulateur abaisseur 5 V est obligatoire en amont (§2.2). Ne pas brancher
l'USB et le régulateur externe en même temps sans vérifier le schéma de la
carte (retour de courant possible vers le port USB).

---

### 10.11 Émulateur QEMU : trois pièges qui ne sont pas des bugs **[2.1]**

Le banc d'intégration (`tests/qemu/sim.py`) fait tourner le vrai firmware
dans QEMU (machine `esp32s3`). Trois limites de l'émulateur imposent une
variante de compilation (`-DTRIMBOX_QEMU`), qui ne doit **jamais** être
flashée sur la carte :

- **Flash en mode QIO : lectures décalées de 2 octets.** Symptôme trompeur :
  mémoire « pleine » et configuration jamais relue. Remède : mode **DIO**
  pour l'émulateur uniquement (la carte réelle reste en QIO).
- **UART2 non émulé.** Le GNSS passe sur UART0 (partagé avec le journal, les
  trames UBX sont extraites du flux texte), le CRSF sur UART1.
- **RMT non émulé.** `rgbLedWrite()` attend indéfiniment la fin d'une
  émission qui n'arrive jamais : la boucle se fige, processeur au repos.
  La DEL est désactivée dans la variante.

Le Bluetooth n'est pas émulé non plus : le protocole console doit être
validé sur la carte.

### 10.12 GNSS allumé en permanence **[2.1]**

Contrairement à la v1 (GPS éteint au redémarrage après coupure), le GNSS
reste allumé dès le démarrage : le chrono radio et la pose de ligne doivent
fonctionner sans enregistrement, et l'alimentation vient désormais de la
batterie de propulsion. L'enregistrement, lui, repart toujours **à l'arrêt**.

L'octet 67 (batterie) vaut 0 tant que l'ESC n'est pas lu.

---

### 10.13 Fonctions de la console disparues sans bruit **[2.1]**

`refreshStatus()` et `refreshConfig()` étaient **appelées** (connexion,
après configuration, après effacement, bouton *Actualiser*) mais **définies
nulle part** dans la console 1.7.1 reçue. En démo rien ne se voit ; avec un
vrai module, l'état mémoire restait à zéro et le téléchargement était refusé
(« la mémoire est vide »). Rétablies en 1.7.3. Même famille que v1 §9.9 :
invisible à la vérification de syntaxe, visible seulement en exécutant le
vrai parcours (ici `tests/web/console_web_test.py`).

---

## Annexe A — Capture du X-Bus du XC-E8

### A.1 Matériel

- Analyseur logique USB 8 voies 24 MHz (clone Saleae, 10 à 15 €).
- Logiciel **PulseView** (sigrok), gratuit.
- Multimètre.
- Câbles Dupont, rallonge servo mâle-femelle.

### A.2 Étape 0 — sécurité (obligatoire)

1. **Retirer le pignon moteur**, ou voiture sur cales, roues dans le vide.
2. Batterie branchée : mesurer la tension **fil X-Bus ↔ négatif** au repos.
   **Au-delà de 5,5 V, ne pas brancher l'analyseur.**
3. Ne **jamais** relier le fil + de la prise à l'analyseur (BEC jusqu'à 8,4 V).

### A.3 Branchement

- GND de l'analyseur → fil − de la prise X-Bus.
- CH0 → fil X-Bus.
- CH1 (facultatif) → fil signal des gaz entre récepteur et ESC.

Réglages PulseView : **4 MHz**, **10 s** par capture.

### A.4 Captures à réaliser

| Fichier | Situation |
|---|---|
| `01_repos.sr` | ESC allumé, rien ne bouge |
| `02_gaz_paliers.sr` | Gâchette : 0, ¼, ½, relâchée |
| `03_frein_marche_arriere.sr` | Frein puis marche arrière |
| `04_app.sr` | Application XC-Link connectée, modification d'un réglage |
| `05_ventilo.sr` | Moteur tournant jusqu'au démarrage du ventilateur |

Noter pour chaque capture la **tension batterie mesurée au multimètre**.

### A.5 Premier diagnostic

- **Signal actif au repos** (`01_repos.sr`) : l'ESC émet seul, on écoutera.
- **Fil plat** : l'ESC attend d'être interrogé. Il faudra capturer un appareil
  XC qui lui parle.

### A.6 Analyse (réalisée par l'assistant sur les fichiers `.sr`)

1. Niveau de repos (haut = UART classique, bas = UART inversé).
2. Durée du bit le plus court → vitesse de transmission.
3. Décodage UART (8N1, puis variantes), export hexadécimal.
4. Délimitation des trames : octet de début, longueur, intervalles.
5. Somme de contrôle : essais somme, XOR, CRC8 courants, Fletcher.
6. Correspondance des champs avec la tension mesurée, la gâchette (CH1) et
   la température (`05_ventilo.sr`).
7. Rédaction de la spécification du protocole, puis du décodeur, puis
   validation sur 100 % des trames capturées (§9, banc n°9).

---

## Annexe B — Points ouverts

| Point | Bloquant pour | Résolution |
|---|---|---|
| Protocole X-Bus XC | §5 | Captures, annexe A |
| Niveau électrique X-Bus | §5.2 | Mesure, annexe A étape 0 |
| Sorties série du ER5C-i (2 et 3 ?) | §4.1 | Onglet *Model* de l'interface web du récepteur |
| `getValue("FM")` renvoie une chaîne | §8.2 | Banc n°4 |
| Trames `0x0C` / `0x0D` relayées et reconnues | §4.3 | Banc n°6 |
| Révision de la DevKitC-1 (v1.0 ou v1.1 : DEL sur GPIO 48 ou 38) | §2.3 | Sérigraphie de la carte |
| Plage accéléromètre suffisante à ±16 g | §2.1, §10.9 | Premiers roulages, recherche de plateaux à 15,99 g |
