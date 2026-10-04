# TrimBox DIY — Cahier des charges

**Version du document :** 1.0 — 17 septembre 2026
**Cible :** reproduction complète du projet par un assistant de code (Qwen, DeepSeek, Codestral…)

---

## Comment utiliser ce document

Ce cahier des charges est conçu pour être fourni **en entier** à un LLM local, ou
découpé par chapitre si la fenêtre de contexte est limitée. Dans ce dernier cas,
l'ordre de priorité est :

1. **§1 à §3** (contexte, matériel, protocole) — indispensables, tout en dépend.
2. **§9 (Pièges connus)** — à fournir systématiquement, quel que soit le module
   travaillé. Ce chapitre condense des bugs qui ont coûté plusieurs cycles de
   débogage sur matériel réel ; un LLM qui repart de zéro les reproduira tous.
3. Le chapitre du module concerné (§4 firmware, §5 console, §6 CI).

> ⚠️ **Les valeurs numériques de ce document ne sont pas indicatives.** Chaque
> seuil, offset et constante a été déterminé par mesure ou par contrainte
> matérielle. Les modifier sans raison casse le système de façon souvent
> silencieuse (données corrompues plutôt que plantage visible).

---

## 1. Contexte et objectif

### 1.1 Objet

TrimBox DIY est un **enregistreur de télémétrie GPS et inertielle pour voitures
radiocommandées**. Il enregistre de façon autonome, sans téléphone à proximité,
sur sa mémoire flash interne. Une console web se connecte ensuite en Bluetooth
pour piloter l'enregistrement, récupérer les sessions et les analyser.

### 1.2 Périmètre

Le projet comporte trois livrables indépendants mais interdépendants :

| Livrable | Fichier | Rôle |
|---|---|---|
| Firmware | `trimbox_diy/trimbox_diy.ino` | Acquisition, stockage, service Bluetooth |
| Console web | `index.html` | Pilotage, téléchargement, analyse |
| Chaîne de compilation | `.github/workflows/build.yml` | Compilation en ligne, sans PC |

Fichiers annexes : `index.html` (page d'accueil), `trimbox-sw.js` (service worker
PWA, optionnel), `trimbox-diy-console-demo.html` (redirection de compatibilité).

### 1.3 Contraintes fondamentales

- **Aucune dépendance externe dans la console.** Un seul fichier HTML, sans CDN,
  sans framework, sans bibliothèque. Motif : la console doit fonctionner dans un
  paddock sans réseau. Seule exception assumée : les tuiles satellite.
- **Compilation sans PC.** Tout le cycle (édition, compilation, téléchargement du
  binaire) doit être réalisable depuis un téléphone Android.
- **Robustesse à la coupure d'alimentation.** Un fil de batterie arraché en pleine
  session ne doit ni corrompre les données, ni rendre l'appareil inaccessible.
- **Pas de compatibilité avec l'application RaceBox officielle.** Le *format de
  trame* en est hérité (voir §3), mais aucune contrainte applicative n'est
  maintenue : nom, modèle et numéro de série sont libres.

### 1.4 Hors périmètre

- Mise à jour du firmware par Bluetooth (OTA) : le bootloader le permet, le
  firmware ne déclenche pas encore la bascule en mode DFU.
- Application Android native.
- Synchronisation cloud, comptes utilisateurs, partage en ligne.

---

## 2. Matériel

### 2.1 Nomenclature

| Élément | Référence | Remarque |
|---|---|---|
| Carte | **Seeed XIAO nRF52840 Sense** | Variante **non-mbed** obligatoire (voir §9.1) |
| MCU | nRF52840 | Bluetooth 5, SoftDevice S140 |
| Mémoire flash | **P25Q16H**, 2 Mo QSPI | Intégrée à la carte, distincte de la flash programme |
| Centrale inertielle | **LSM6DS3**, intégrée | Accéléromètre + gyroscope |
| Récepteur GNSS | u-blox (SAM-M10Q ou équivalent) | Sur `Serial1`, UART |
| Alimentation | Batterie LiPo 1 cellule | Charge gérée par la carte |

### 2.2 Brochage

```
GPS_EN_PIN        D1     Alimentation du module GNSS (voir PCB_VERSION)
PIN_VBAT_ENABLE   14     Activation du pont diviseur de mesure batterie
PIN_HICHG         22     Courant de charge (LOW = 100 mA)
PIN_CHG           23     Indicateur de charge (LOW = en charge)
ACCEL_INT_PIN     PIN_LSM6DS3TR_C_INT1
PIN_QSPI_*        (définis par le core Seeed)
GNSS              Serial1, 115200 bauds
```

Le drapeau de compilation `PCB_VERSION` inverse la logique de `GPS_EN_PIN`
(LOW = allumé) pour la version sur circuit imprimé dédié.

### 2.3 Capacités et limites

- **Capacité mémoire :** 25 789 enregistrements
  (calcul : `(2 097 152 − 8 192) / 81`).
- **Autonomie d'enregistrement :** ~17 min à 25 Hz, 43 min à 10 Hz,
  86 min à 5 Hz, 7 h à 1 Hz.
- **Plage accéléromètre : ±16 g.** Impératif : à ±8 g, les chocs d'atterrissage
  de saut saturent et toutes les mesures plafonnent à 7,99 g simultanément sur
  les trois axes.
- **Précision GPS :** 30 cm à 1 m selon réception. Suffisant pour le
  chronométrage et les vitesses ; insuffisant pour comparer des trajectoires au
  centimètre près.

---

## 3. Protocole Bluetooth

### 3.1 Origine et statut

Le format de trame dérive de la documentation du protocole BLE RaceBox
(révision 8). Il est **conservé tel quel** car la console s'appuie dessus. Deux
extensions maison ont été ajoutées (`0xFF 0xF0`).

### 3.2 Service et caractéristiques

Service Nordic UART :

```
Service  6E400001-B5A3-F393-E0A9-E50E24DCCA9E
RX       6E400002-B5A3-F393-E0A9-E50E24DCCA9E   écriture   (console → appareil)
TX       6E400003-B5A3-F393-E0A9-E50E24DCCA9E   notification (appareil → console)
```

Service Device Information (`0x180A`) : modèle (`0x2A24`), numéro de série
(`0x2A25`), version firmware (`0x2A26`), version matérielle (`0x2A27`),
fabricant (`0x2A29`).

**Le nom annoncé doit commencer par `TrimBox`** : c'est le filtre de recherche de
la console.

### 3.3 Structure de trame

```
 Octet  0     1     2      3     4-5           6...        n-2   n-1
       0xB5  0x62  classe  id    longueur LE   charge      ckA   ckB
```

Somme de contrôle : algorithme **Fletcher-8**, calculée sur classe, id, longueur
et charge utile (pas sur les octets de synchronisation) :

```c
ckA = ckB = 0;
pour chaque octet o de [classe, id, len_lo, len_hi, charge...] :
    ckA = (ckA + o) & 0xFF;
    ckB = (ckB + ckA) & 0xFF;
```

### 3.4 Table des messages

| Classe/ID | Sens | Long. | Rôle |
|---|---|---|---|
| `FF 01` | → console | 80 | Données en direct |
| `FF 02` | → console | 2 | ACK (classe, id acquittés) |
| `FF 03` | → console | 2 | NACK |
| `FF 21` | → console | 80 | Enregistrement mémoire (même format que `FF 01`) |
| `FF 22` | ↔ | 0 / 12 | État mémoire (requête vide / réponse) |
| `FF 23` | ↔ | 0 / 4 / 1 | Téléchargement : démarrer / nb annoncé / annuler |
| `FF 24` | ↔ | 0 / 1 | Effacement : démarrer / progression % |
| `FF 25` | ↔ | 0 / 12 | Configuration d'enregistrement |
| `FF 26` | → console | 12 | Changement d'état d'enregistrement |
| `FF 27` | ↔ | 0 / 3 | Configuration du récepteur GNSS |
| `FF 30` | ↔ | 4 / 0 | Déverrouillage mémoire |
| `FF F0` | ↔ | 0 / var. | **Extension maison** : identification firmware |

### 3.5 Message de données (80 octets) — offsets

```
 0  u32  iTOW (ms)              40  u32  précision horizontale (mm)
 4  u16  année                  44  u32  précision verticale (mm)
 6  u8   mois                   48  i32  vitesse sol (mm/s)
 7  u8   jour                   52  i32  cap (deg × 1e5)
 8  u8   heure                  56  u32  précision vitesse (mm/s)
 9  u8   minute                 60  u32  précision cap
10  u8   seconde                64  u16  pDOP (× 100)
11  u8   drapeaux validité      66  u8   drapeaux lat/lon
12  u32  précision temps (ns)   67  u8   batterie (bits 0-6) + charge (bit 7)
16  i32  nanosecondes           68  i16  accél. X (milli-g)
20  u8   type de fix            70  i16  accél. Y (milli-g)
21  u8   drapeaux de fix        72  i16  accél. Z (milli-g)
22  u8   drapeaux date/heure    74  i16  gyro X (centi-deg/s)
23  u8   nombre de satellites   76  i16  gyro Y (centi-deg/s)
24  i32  longitude (deg × 1e7)  78  i16  gyro Z (centi-deg/s)
28  i32  latitude (deg × 1e7)
32  i32  altitude WGS84 (mm)    Tous les entiers sont en LITTLE-ENDIAN.
36  i32  altitude MSL (mm)
```

### 3.6 Configuration d'enregistrement `FF 25` (12 octets)

```
0  u8   activation (0 = arrêt, 1 = marche)
1  u8   cadence : 0=25Hz  1=10Hz  2=5Hz  3=1Hz  4=20Hz
2  u8   drapeaux de filtre (voir ci-dessous)
3  u8   réservé
4  u16  seuil de vitesse « à l'arrêt » (mm/s)
6  u16  délai avant pause à l'arrêt (s)
8  u16  délai avant pause sans fix (s)
10 u16  délai avant extinction automatique (s)
```

Drapeaux de filtre (champ `flags`, octet 2) :

| Bit | Valeur | Filtre |
|---|---|---|
| 0 | `0x01` | Attendre un fix GPS avant de commencer |
| 1 | `0x02` | Suspendre à l'arrêt |
| 2 | `0x04` | Suspendre sans signal GPS |
| 3 | `0x08` | Éteindre après inactivité |
| 4 | `0x10` | Ne pas éteindre avant d'avoir enregistré au moins un point |

Valeurs par défaut : cadence 25 Hz, `flags = 0x1F` (tous actifs),
seuil 1389 mm/s (~5 km/h), pause 30 s, sans-fix 30 s, extinction 300 s.

### 3.7 État mémoire `FF 22` (réponse, 12 octets)

```
0  u8   enregistrement en cours (0/1)
1  u8   taux de remplissage (%)
2  u8   drapeaux de sécurité (bit 0 = mémoire verrouillée)
3  u8   réservé
4  u32  enregistrements utilisés
8  u32  capacité totale
```

### 3.8 Extension `FF F0` — identification du firmware

Requête vide, réponse en texte ASCII, champs séparés par `|` :

```
MODÈLE|VERSION|DATE_COMPILATION|PLAGE_G|PSEUDO
TrimBox DIY|1.0|Sep 17 2026 14:12:07|16|Buggy 1
```

Motif : le bootloader UF2 redémarre la carte dès la fin de la copie, ce qui
provoque une erreur de déconnexion **normale** côté PC. Cette empreinte est le
seul moyen fiable de vérifier quelle version tourne réellement.

Un appareil tiers répond NACK à ce message : la console doit traiter ce cas sans
erreur (champ laissé vide).

### 3.9 Séquences

**Téléchargement.** Console envoie `FF 23` (vide) → appareil répond `FF 23` (4
octets : nombre annoncé) → flux de `FF 21` et `FF 26` → `FF 02` (ACK) final.
Les données en direct sont **suspendues** pendant toute l'opération.

**Effacement.** Console envoie `FF 24` (vide) → notifications `FF 24` (1 octet :
%) → `FF 02` final. L'annulation en cours d'effacement est refusée (voir §9.7).

---

## 4. Firmware

### 4.1 Architecture

Boucle principale non bloquante, dans cet ordre strict :

```
serviceSerialCommands()   commandes de secours par port série
serviceRxParser()         assemblage des trames reçues, exécution
serviceErase()            effacement progressif, une page par tour
serviceDownload()         alimentation de la file d'émission
txqService()              vidange de la file vers le Bluetooth
serviceAutoShutdown()     extinction automatique
processGNSS() / processIMU() / managePower()
```

**Le protocole passe avant tout**, y compris avant la mise en veille : sinon une
commande resterait sans réponse pendant la durée du cycle de veille (2,5 s).

### 4.2 File d'émission — exigence critique

Toutes les trames sortantes, **y compris les données en direct**, transitent par
une file d'octets unique (2048 octets, tampon circulaire).

> Ne jamais appeler `notify()` directement depuis le chemin des données en
> direct. Deux chemins de sortie concurrents sur la même caractéristique
> permettent à un paquet de s'insérer au milieu d'une réponse fragmentée : le
> flux d'octets côté console est alors corrompu et la réponse rejetée (§9.5).

- Réserve : les données en direct ne sont empilées que s'il reste au moins
  `88 + 256` octets libres, garantissant qu'une réponse de commande n'est jamais
  perdue faute de place.
- Vidange : jusqu'à 12 notifications par tour de boucle, découpées selon le MTU
  négocié (`MTU − 3`, plafonné à 244 octets).
- Les données en direct sont **sacrifiables** ; les réponses de commande ne le
  sont pas.

### 4.3 Organisation de la mémoire flash

```
Adresse       Taille    Contenu
0x000000      4 Ko      Configuration, exemplaire A
0x001000      4 Ko      Configuration, exemplaire B
0x002000      reste     Journal d'enregistrements, en ajout linéaire
```

Format d'un emplacement (`SLOT_SIZE = 81` octets) :

```
[0]      type : 0x21 = données, 0x26 = changement d'état, 0xFF = libre
[1..80]  charge utile de 80 octets
```

### 4.4 Configuration persistante — double exemplaire

```c
struct RecConfig {        // écrite en alternance en A puis B
  uint32_t magic;         // 0x534D4252
  uint8_t  version;       // 2
  uint8_t  enabled, dataRate, flags;
  uint16_t statSpeed, statInterval, noFixInterval, autoOffInterval;
  uint8_t  gnssDynModel, gnss3dSpeed, gnssMinAcc, reserved;
  uint32_t seq;           // numéro de séquence : le plus grand fait foi
  uint32_t crc;           // contrôle de tous les champs précédents
};
```

**Exigence :** sauvegarder implique d'effacer puis réécrire un secteur. Une
coupure pendant cette fenêtre détruirait l'exemplaire en cours d'écriture.
L'écriture alternée garantit qu'un exemplaire valide subsiste toujours.

Au démarrage : lire les deux, valider magie + version + CRC, retenir le plus
récent par comparaison de `seq` tolérante au rebouclage
(`(int32_t)(b.seq − a.seq) > 0`).

Relecture de contrôle après écriture : si l'exemplaire relu est invalide, ne pas
l'adopter comme référence.

### 4.5 Sécurité à la coupure d'alimentation

Quatre mécanismes, tous obligatoires :

1. **Double exemplaire de configuration** (§4.4).
2. **Pas de reprise automatique.** Si la configuration indique « actif » au
   démarrage, c'est que la session précédente s'est mal terminée. L'appareil
   repart **à l'arrêt**, GPS éteint, Bluetooth à pleine puissance, USB
   disponible. Les données restent téléchargeables.
   Constante : `AUTO_RESUME_RECORDING 0`.
3. **Pas de veille profonde tant que l'USB est branché**, ni avant 120 s après
   le démarrage, ni si aucune donnée n'a encore été enregistrée.
4. **Validation des enregistrements à la lecture.** Un enregistrement interrompu
   contient des octets restés à `0xFF`. Contrôles avant émission : `fixType ≤ 5`,
   `numSV ≤ 60`, `|lat| ≤ 900000000`, `|lon| ≤ 1800000000`,
   `0 ≤ gSpeed ≤ 140000000`. Les rejets sont comptés et signalés en fin de
   téléchargement.

### 4.6 Filtres d'enregistrement

Appliqués à chaque nouveau point GNSS :

- **Attente de fix** : rien n'est stocké tant qu'aucun fix 3D n'a été obtenu.
- **Stationnaire** : si `gSpeed < statSpeed` pendant `statInterval`, passage en
  pause. Uniquement si le fix est valide.
- **Sans fix** : si absence de fix pendant `noFixInterval`, passage en pause.
- **Reprise** : le passage pause → actif est notifié au client mais
  **n'est pas stocké** (contrairement au passage actif → pause).

### 4.7 Commandes de secours (port série, 115200 bauds)

| Touche | Effet |
|---|---|
| `s` | Arrêt d'urgence de l'enregistrement |
| `i` | État complet : mémoire, enregistrement, configuration |
| `z` | Configuration par défaut (données conservées) |
| `?` | Rappel des commandes |

Motif : permettre de reprendre la main sans reflasher.

### 4.8 Configuration GNSS

- Cadence : selon `dataRate`, 25 Hz par défaut.
- Modèle dynamique : **7 (Airborne 2g)** par défaut — les voitures RC dépassent
  facilement 1 g en freinage et en virage, un modèle automobile « lisserait »
  ces valeurs en les traitant comme des anomalies.
- Filtres passe-bas de vitesse et de cap **désactivés** (`CFG-ODO-OUTLPVEL`,
  `CFG-ODO-OUTLPCOG`) : ils introduisent un retard sur les transitoires.
- Maintien statique désactivé (`CFG-MOT-GNSSSPEED_THRS = 0`).
- Constellations superflues désactivées **avant** de demander 25 Hz, sinon le
  module refuse la cadence.

### 4.9 Personnalisation

```c
#define DEVICE_NICKNAME "Buggy 1"   // ≤ 16 caractères (§9.4)
#define BRAND       "TrimBox DIY"
#define DEVICE_NAME "TrimBox " DEVICE_NICKNAME
#define FIRMWARE_VER "1.0"
#define BUILD_STAMP __DATE__ " " __TIME__
```

---

## 5. Console web

### 5.1 Contraintes d'architecture

- **Un seul fichier HTML**, autonome, sans dépendance externe.
- **Aucune bibliothèque** : rendu en Canvas 2D natif, pas de moteur graphique.
- **Origine sécurisée obligatoire** (HTTPS) : exigence du Bluetooth Web.

### 5.2 Réassemblage des trames — exigence critique

Le flux Bluetooth arrive fragmenté, sans correspondance avec les limites de
trame. Le réassembleur doit :

1. Chercher la séquence `B5 62`.
2. **Valider la longueur annoncée** contre la longueur attendue du type de
   message (`{0x01:80, 0x02:2, 0x03:2, 0x21:80, 0x22:12, 0x26:12}`). Une
   longueur incohérente signale un faux départ → avancer de 2 octets.
3. Vérifier la somme de contrôle.
4. **En cas d'échec, avancer de 2 octets — jamais de la longueur annoncée.**

> **C'est le bug le plus coûteux du projet** (§9.5). Mesuré sur banc :
> 50 % de perte de données avant correction, 0,2 % après.

### 5.3 Filtrage des données

Trois niveaux, tous nécessaires :

| Niveau | Règle | Motif |
|---|---|---|
| Plausibilité | `|lat| ≤ 90`, `|lon| ≤ 180`, `0 ≤ vitesse < 500 km/h`, `fix ≤ 5`, `sats ≤ 60`, `|accél.| ≤ 20 g` | Une trame corrompue peut passer la somme de contrôle par hasard (1 sur 65 536) |
| Débruitage position | Point à plus de **30 m** (`MAX_STEP_M`) du précédent **et** du suivant → supprimé | Décrochages isolés du récepteur, visibles en « allers-retours » sur le tracé |
| Débruitage vitesse | Écart > **20 km/h** (`MAX_DV_KMH`) avec les deux voisins → remplacé par leur moyenne | À 25 Hz, même 10 g ne produisent que 14 km/h en 40 ms |

### 5.4 Analyse

**Temps en l'air.** Détection quand `|az| < 0,35 g` (chute libre : l'accéléromètre
ne mesure plus la pesanteur), avec un **minimum de 3 échantillons consécutifs**
(`MIN_AIR_SAMPLES`). Sans ce seuil, les vibrations du châssis produisent des
centaines de faux sauts (mesuré : 15,20 s de faux positifs contre 1,00 s réel).

Quand une ligne d'arrivée est posée, la valeur est rapportée **au tour** (moyenne
et maximum) plutôt qu'en cumul de session.

**Chronométrage.** Deux modes exclusifs, sélectionnés par commutateur :

- *Circuit* : une seule ligne, franchie à chaque tour.
- *Dragster* : ligne de départ **et** ligne d'arrivée distinctes. Chaque
  franchissement du départ est associé au premier franchissement de l'arrivée qui
  le suit.

Détection de franchissement — trois validations obligatoires :

1. Intersection géométrique segment/segment.
2. **Proximité de l'ancrage** : la traversée doit avoir lieu à moins de
   `halfM × 1,15` du point de pose. Sans ce test, une ligne prolongée recoupe le
   tracé à l'opposé du circuit (§9.6).
3. **Sens de passage** : produit scalaire avec le cap de référence > 0,3.
4. Déduplication : deux traversées à moins de 1,5 s = un seul franchissement.

Demi-longueur de ligne : **4 m** (8 m au total).

**Chronos intermédiaires (mode dragster).** Configurables, deux familles :
vitesses cibles (0 → 30/50/80 km/h) et distances de passage (25/50/100 m). Les
temps sont **interpolés entre deux échantillons** : précision mesurée de 0 ms sur
les cibles de vitesse, 3 ms sur les distances.

**Statistiques de passage.** Au survol du tracé, agrégation de **tous les
passages** au même endroit (tours confondus) : nombre de passages, vitesse
max/moyenne/min, valeurs de G.

### 5.5 Cartographie

- **Projection Web Mercator normalisée** [0,1], indépendante du niveau de tuile.
- **Zoom continu** ×1 à ×60, ancré sous le curseur (dérive vérifiée nulle).
- **Niveau de tuile plafonné à 19** (`MAX_TILE_Z`). Au-delà, Esri renvoie une
  tuile grise « Map data not yet available » **avec un code HTTP 200** : une
  gestion d'erreur classique ne peut pas la détecter (§9.8).
- Imagerie : `https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}`
  (noter l'ordre **z/y/x**). Attribution obligatoire : « Esri, Maxar, Earthstar
  Geographics ».
- Repli automatique si un niveau ne renvoie que des erreurs.

### 5.6 Formats d'export et d'import

| Format | Export | Import | Usage |
|---|---|---|---|
| **VBO** | ✅ | ✅ | RaceChrono Pro, Circuit Tools — chronométrage au tour |
| **CSV** | ✅ | ✅ | Tableur, analyse libre |
| **GPX** | ✅ | ✅ | Cartographie généraliste |

**Pièges du format VBO** (vérifiés par aller-retour, écart 1,1 cm) :

- Latitude et longitude en **minutes d'arc**, pas en degrés.
- Longitude comptée **positive vers l'ouest** → inversion de signe.
- Section `[column names]` : jetons courts normalisés
  (`sats time lat long velocity heading height vert-vel LongAcc LatAcc`), et non
  les noms longs de l'en-tête.
- Champ temps au format `HHMMSS.SS`.

Un GPX ne transporte ni vitesse ni accélérations : elles doivent être
reconstituées à partir des positions et des horodatages.

### 5.7 Interface

- **Deux thèmes**, bascule par bouton, préférence mémorisée
  (`localStorage`), choix initial : préférence enregistrée → préférence système →
  sombre.
  - *Sombre* : noir intégral `#000000` (économie de batterie OLED : seuls les
    pixels réellement éteints ne consomment pas).
  - *Clair* : blanc légèrement teinté violet `#F3EFFB`.
- **Couleurs système identiques dans les deux thèmes** : violet `#7C4DFF`
  (boutons, accents), états `#33D6A6` / `#FF4D6D` / `#FFB84D`.
- **Variantes de texte assombries** pour le thème clair (`--ok-text` etc.) : les
  teintes vives utilisées en texte sur fond blanc tombent à 1,7:1 de contraste,
  bien sous le seuil de 4,5:1.
- Les couleurs des canevas ne peuvent pas lire les variables CSS : prévoir un
  cache rafraîchi à chaque bascule de thème, et **redessiner** les canevas.

### 5.8 Source des données

Un commutateur **Bluetooth / Démo** choisit la source. Toutes les commandes
passent par des fonctions uniques (`doConnect`, `doDownload`…) qui se comportent
différemment selon un booléen `demoMode`.

> **Ne pas réaffecter les gestionnaires d'événements en cours d'exécution** pour
> basculer entre les modes : cette approche a produit plusieurs bugs (§9.9).

Le mode démo simule un appareil complet : télémétrie 25 Hz, mémoire, et trois
sessions (deux circuits, un parcours en ligne droite pour le mode dragster).

### 5.9 PWA

- Manifeste et icônes **intégrés en data URI** dans le HTML : aucun fichier
  annexe requis pour l'installation manuelle.
- `display: standalone`, couleurs de thème synchronisées avec le thème actif.
- Service worker (`trimbox-sw.js`) **optionnel**, à déposer à côté de la page :
  un service worker ne peut être ni inline ni chargé depuis un autre domaine.
  Il apporte le fonctionnement hors ligne et l'invite d'installation automatique.
  Son gestionnaire `fetch` doit être **réellement fonctionnel** : Chrome ignore
  désormais les gestionnaires vides pour l'éligibilité.

### 5.10 Versionnage

Constante `CONSOLE_VER`, affichée sous l'état de connexion. Format `1.6.x`,
incrémenté de 1 à **chaque** modification ; après `1.6.9`, passer à `1.7.0`.
Sert d'indicateur de cache navigateur.

---

## 6. Chaîne de compilation

### 6.1 Exigences

Compilation **en ligne** via GitHub Actions, sans PC. Sorties : `.uf2`
(glisser-déposer), `.zip` DFU (Bluetooth), `.hex` (sonde SWD).

### 6.2 Configuration

```yaml
CORE:  Seeeduino:nrf52@1.1.13      # version figée (reproductibilité)
FQBN:  Seeeduino:nrf52:xiaonRF52840Sense
INDEX: https://files.seeedstudio.com/arduino/package_seeeduino_boards_index.json
UF2_FAMILY: '0xADA52840'
```

### 6.3 Bibliothèques

**À installer :** `SparkFun u-blox GNSS Arduino Library`, `Seeed Arduino LSM6DS3`.

**À ne SURTOUT PAS installer :** `Adafruit TinyUSB`, `SdFat`, `Adafruit SPIFlash`
— déjà fournis par le core (§9.2).

**Outil supplémentaire :** `adafruit-nrfutil` (via pipx). Le core l'appelle en
fin de compilation pour produire le paquet DFU mais ne le fournit pas (§9.3).

### 6.4 Points de vigilance

- Refuser la compilation si **plusieurs fichiers `.ino`** existent : risque de
  compiler l'ancien firmware sans s'en apercevoir.
- Le dossier du croquis doit porter le même nom que le `.ino`.
- Artefact **non compressé** (`upload-artifact@v7`, `archive: false`) pour le
  `.uf2` : évite une extraction d'archive sur téléphone.
- Actions en version Node 24 (`checkout@v5`, `cache@v5`, `upload-artifact@v7`).
  Installer arduino-cli par son script officiel plutôt que par l'action
  `arduino/setup-arduino-cli`, restée en Node 20.

---

## 7. Hébergement

GitHub Pages, branche `main`, dossier racine. Fournit l'origine HTTPS requise par
le Bluetooth Web. Les noms de fichiers sont **sensibles à la casse**.

---

## 8. Recette — critères d'acceptation

### 8.1 Firmware

- [ ] Au démarrage, le port série affiche marque, pseudo, version et empreinte
      de compilation.
- [ ] `i` sur le port série renvoie un état mémoire cohérent.
- [ ] La flash est détectée : identifiant JEDEC `85 60 15`.
- [ ] Un enregistrement démarré depuis la console fait progresser le compteur
      mémoire à la cadence choisie.
- [ ] **Test d'arrachement** : couper l'alimentation en pleine session. Au
      redémarrage, l'appareil est accessible en Bluetooth, l'enregistrement est
      à l'arrêt, et les données antérieures sont téléchargeables.
- [ ] Le téléchargement signale le nombre d'enregistrements corrompus écartés.

### 8.2 Console

- [ ] Le journal du protocole ne signale **aucune trame rejetée** lors d'un
      téléchargement complet.
- [ ] Aucune vitesse supérieure à 150 km/h sur une session RC réelle.
- [ ] Le tracé ne présente aucun « aller-retour » traversant la carte.
- [ ] Un export VBO se charge sans erreur dans Circuit Tools, avec des
      coordonnées au bon endroit du globe.
- [ ] Un aller-retour export → import restitue les mêmes valeurs (< 5 cm).
- [ ] Le chronométrage au tour donne des temps réguliers sur un circuit bouclé.
- [ ] Les deux thèmes sont lisibles, le violet des boutons est identique.
- [ ] « Installer l'application » apparaît dans le menu Chrome.

### 8.3 Tests automatisables recommandés

Reproduire ces bancs de test, qui ont tous révélé de vrais bugs :

1. **Réassembleur** : flux de 500 trames contenant des `B5 62` en charge utile,
   avec 3 octets perdus au milieu. Attendu : ≥ 99 % de trames valides.
2. **Chronométrage** : circuit simulé de 5 tours à 20,00 s, ligne posée à
   4 positions différentes. Attendu : 4 tours à 20,00 s dans les 4 cas.
3. **Chronos dragster** : accélération constante 6 m/s². Comparer aux temps
   théoriques `t = v/a` et `t = √(2d/a)`. Attendu : < 5 ms d'écart.
4. **Temps en l'air** : signal vibratoire + 2 sauts connus. Attendu : exactement
   2 sauts détectés.
5. **Aller-retour VBO/CSV** : écart maximal < 5 cm.

---

## 9. Pièges connus — chapitre prioritaire

> Chacun de ces points correspond à un bug rencontré en conditions réelles. Un
> LLM qui reconstruit le projet sans ce chapitre les reproduira.

### 9.1 Variante de carte

Utiliser **Seeed nRF52 Boards**, pas la variante **mbed** : cette dernière ne
fournit pas Bluefruit et ne compile pas.

### 9.2 Conflit de type `File`

Si `Adafruit TinyUSB`, `SdFat` ou `Adafruit SPIFlash` sont installés en doublon
dans `Documents/Arduino/libraries/`, ils prennent la priorité sur ceux du core.
`SdFat` déclare `typedef File32 File` et `Adafruit_LittleFS` déclare une classe
`File` : collision dans `bonding.cpp`, **avant même que le code du projet ne soit
compilé**. Symptôme : `reference to 'File' is ambiguous`.

Correctif : déplacer ces dossiers **hors de** `libraries/` (les renommer sur
place ne suffit pas — l'IDE se fie au contenu, pas au nom).

### 9.3 `adafruit-nrfutil` absent

Le core l'appelle pour produire le paquet DFU mais ne le distribue pas. Sur une
machine vierge, la compilation échoue **après** avoir produit le `.hex` :
`exec: "adafruit-nrfutil": executable file not found in $PATH`.

### 9.4 Longueur du nom Bluetooth

Le pseudo doit rester **sous 16 caractères** : au-delà, la trame d'annonce
déborde et le nom est tronqué.

### 9.5 Deux bugs de flux — les plus coûteux du projet

**a) `setMaxLen` manquant.** Sans `rbRx.setMaxLen(247)`, Bluefruit plafonne la
caractéristique à 20 octets. Le SoftDevice rejette silencieusement toute écriture
plus longue **sans appeler le callback** : les commandes disparaissent sans
aucune trace d'erreur.

**b) Resynchronisation du réassembleur.** Voir §5.2. Avancer de la longueur
annoncée par une trame invalide est une faute : cette longueur n'est justement
pas fiable. Symptôme : valeurs délirantes intermittentes (32,77 g = 32767,
7 670 544 km/h) — ce sont des octets mal alignés interprétés comme des mesures.

### 9.6 Ligne d'arrivée trop longue

Une ligne de 18 m recoupe le tracé à l'opposé d'un circuit RC de 44 m de large :
10 traversées pour 5 tours, temps faux (18,61 s au lieu de 20,00 s). Correctif :
ligne courte **et** validation de proximité à l'ancrage **et** validation du sens.

### 9.7 Annulation d'effacement

Le journal étant en ajout linéaire, s'arrêter à mi-chemin laisserait des secteurs
non effacés devant le pointeur d'écriture, sur lesquels il serait impossible
d'écrire. L'effacement doit être mené à son terme (il ne porte que sur la zone
utilisée, donc il est court).

### 9.8 Tuiles satellite : l'erreur qui n'en est pas une

Au-delà du niveau de couverture, Esri renvoie une tuile grise
« Map data not yet available » **avec un code HTTP 200**. Aucune gestion d'erreur
ne peut la détecter. Seul remède : plafonner le niveau de tuile à 19.

### 9.9 Ordre de déclaration en JavaScript

Une variable `let` lue avant sa ligne de déclaration lève
`Cannot access 'X' before initialization` (zone morte temporelle), même si le
code semble s'exécuter plus tard. Déclarer **toutes** les variables d'état en
tête de script.

Corollaire : une vérification de syntaxe ne détecte pas ce bug. **Exécuter
réellement le script** dans un DOM simulé (Node.js + objets factices) est le seul
test fiable.

### 9.10 Le mode ne doit pas dépendre d'un clic

Réagir au *clic* sur un bouton de mode, plutôt qu'à l'*état* du mode, produit des
incohérences dès que l'utilisateur change de mode avant que les données existent.
Toujours recalculer à partir de l'état courant.

### 9.11 Saturation de l'accéléromètre

À ±8 g, les valeurs plafonnent à 7,99 g simultanément sur les trois axes lors des
atterrissages. Symptôme trompeur : des valeurs qui *semblent* plausibles. Régler
sur **±16 g** et adapter le seuil de plausibilité de la console à 20 g.

### 9.12 Erreur de copie UF2

Le bootloader redémarre la carte dès le dernier bloc écrit, avant que le système
n'ait terminé sa comptabilité de fichiers. L'erreur de déconnexion est **normale**
et n'indique **pas** un échec. Seule l'empreinte de compilation (§3.8) permet de
vérifier quelle version tourne.

---

## 10. Glossaire

| Terme | Définition |
|---|---|
| **Fix** | Verrouillage du récepteur GNSS. Type 3 = position 3D valide |
| **iTOW** | *Time of Week*, horodatage GNSS en millisecondes |
| **UF2** | Format de fichier du bootloader : glisser-déposer sur un lecteur USB |
| **DFU** | *Device Firmware Update*, mise à jour par Bluetooth |
| **MTU** | Taille maximale d'un paquet Bluetooth négociée à la connexion |
| **SoftDevice** | Pile Bluetooth propriétaire de Nordic |
| **VBO** | Format de télémétrie Racelogic (RaceChrono, Circuit Tools) |
| **Tuile** | Image carrée de 256 px composant un fond cartographique |
