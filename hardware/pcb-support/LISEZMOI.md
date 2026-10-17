# PCB support TrimBox (révision 1)

Petit PCB 2 couches de **23,4 × 29 mm**, à la taille de la carte NiceMCU
« ESP32-S3_MINI_Nano » N16R8 (il s'arrête sous la première rangée de trous :
**aucun cuivre sous l'antenne** de l'ESP32). Il porte :

- la centrale inertielle **LSM6DS3TR-C**, soudée par JLCPCB (rigide, I2C 0x6A) ;
- un **régulateur 5 V** (TPS563201) alimenté directement par le BEC (6 à 8,4 V) :
  plus besoin du régulateur Pololu séparé ;
- **J1** (JST SH 5 broches) : récepteur ELRS + alimentation BEC ;
- **J2** (JST SH 4 broches) : GPS HGLRC M100 Mini.

L'ESP32 se soude **de l'autre côté**, sur des picots 2,54 mm (composants du
PCB vers le bas, ESP32 au-dessus, prise USB-C et DEL accessibles).

> ⚠️ **Avant de commander** : les cotes de la carte ESP32 (`GEO` dans
> `src/model.py`) sont **estimées d'après une photo**, et la correspondance
> trou ↔ sérigraphie (`MAPPING`) est une **hypothèse** (voir plus bas). Les
> deux doivent être confirmées sur la carte réelle, puis le projet régénéré.

## Brochage des connecteurs

| J1 (récepteur) | Signal | | J2 (GPS) | Signal |
|---|---|---|---|---|
| 1 | masse | | 1 | masse (G) |
| 2 | + BEC 6-8,4 V | | 2 | + 5 V (V) |
| 3 | vers **RX** du récepteur (IO15) | | 3 | depuis **TX** du GPS (IO18) |
| 4 | depuis **TX** du récepteur (IO16) | | 4 | vers **RX** du GPS (IO17) |
| 5 | masse | | | |

Le 5 broches côté récepteur rend impossible l'inversion des deux câbles (le
GPS ne recevra jamais les 8,4 V du BEC). Les câbles se font avec des
connecteurs JST SH 1,0 mm (fils sertis vendus tout faits) ; vérifier l'ordre
G V T R au bout du câble du GPS (les câbles SH existent en « 1 → 1 » et en
« inversé »).

## Fichiers

| Dossier / fichier | Contenu |
|---|---|
| `trimbox-support-kicad5-pour-EasyEDA.zip` | Projet KiCad 5.1 (schéma, symboles, empreintes, PCB) |
| `fabrication/trimbox-support-gerber-JLCPCB.zip` | Gerber + perçages, prêts pour JLCPCB |
| `fabrication/*-BOM-JLCPCB.csv`, `*-CPL-JLCPCB.csv` | Nomenclature (codes LCSC) et placement pour l'assemblage CMS |
| `apercus/schema.pdf` · `schema.svg` | Schéma lisible |
| `apercus/faces-cuivre.png` | Les deux faces (gauche : composants, droite : côté ESP32, vue de son côté) |
| `apercus/placement-routage.png` | Placement et pistes |
| `kicad/` | Le projet KiCad décompressé |
| `src/` | Générateur : **tout** découle de `src/model.py` |

### Importer dans EasyEDA

- **EasyEDA Standard** : *Fichier → Ouvrir → KiCad…*, choisir le zip (schéma et
  PCB s'importent séparément selon les versions). Après import du PCB :
  *Outils → Reconstruire les zones de cuivre* (les plans de masse sont
  recalculés par EasyEDA).
- **EasyEDA Pro** (gratuit, même compte) : page d'accueil → *Importer → KiCad*,
  même zip. C'est l'import le plus complet si la version Standard bute.
- L'import n'a pas pu être testé dans EasyEDA d'ici : si un élément manque,
  les Gerber ci-dessous restent la référence pour commander.

### Commander chez JLCPCB (sans passer par EasyEDA)

1. jlcpcb.com → *Order now* → déposer `trimbox-support-gerber-JLCPCB.zip`
   (2 couches, 1,6 mm, finition HASL sans plomb ou ENIG).
2. Cocher **PCB Assembly**, face *Top*, déposer la BOM et le CPL.
3. Dans l'aperçu de pose, **vérifier l'orientation** de U1 (SOT-23-6), U2
   (point de la broche 1 du LGA-14), D1 (barre de cathode côté prise VIN de
   l'ESP32) et J1/J2 (ouverture vers le bord de la carte) : les rotations
   KiCad doivent souvent être corrigées à la main dans cet aperçu. Pour J1/J2,
   le point de pose est l'origine de l'empreinte KiCad, pas le centre du
   boîtier (≈ 0,45 mm d'écart, l'aperçu le montre).
4. La carte ESP32 n'est pas dans la BOM : picots soudés à la main.

## Règles respectées (contrôle automatique au 0,01 mm, `src/check.py`)

- Isolement ≥ 0,15 mm (mesuré : 0,16 mm), cuivre à 0,3 mm du bord, 0,88 mm
  minimum entre perçages, vias 0,3 / 0,6 mm, pistes ≥ 0,15 mm.
- Chaque réseau d'un seul tenant (deux faces, vias et trous traversants
  compris), plans de masse sur les deux faces.
- Gerber relus par un lecteur indépendant (`src/verify_gerber.py`) et listes
  de connexions du schéma et du PCB KiCad comparées au modèle
  (`src/verify_kicad.py`).

## Points à confirmer sur le matériel

1. **Correspondance trou ↔ sérigraphie** de l'ESP32 (« hypothèse A » : sur
   chaque rangée, les deux noms gravés au dos se lisent dans l'ordre des
   trous de gauche à droite). Test : carte vue de dessus, USB-C en bas,
   bouton BOOT ↔ trou en haut à gauche, colonne extérieure → doit biper
   (IO0). Si c'est l'inverse, rien n'est dangereux (les rangées 3V3, GND et
   VIN ont le même nom sur leurs deux trous) : il suffit d'échanger dans
   `config.h` 7 ↔ 8, 9 ↔ 10, 15 ↔ 16 et 17 ↔ 18, ou de corriger `MAPPING` et
   de régénérer avant de commander.
2. **Cotes** de la carte (largeur, hauteur, entraxe des colonnes, position de
   la première rangée) → `GEO` dans `src/model.py`, puis
   `python3 src/do_route.py && python3 src/build.py`.
3. **Orientation de l'IMU** : elle est sur la face composants, tournée vers le
   bas une fois montée. Voiture à plat, la touche série `m` doit afficher
   `az ≈ +1000 mg` ; sinon régler `AXIS_*` dans `config.h` (axe X vers
   l'avant de la voiture).
4. Le 5 V réel vaut environ **4,7 à 4,8 V** (chute de la diode D1, qui empêche
   l'USB de l'ESP32 d'alimenter le BEC) : normal pour VIN et pour le GPS.
5. `PIN_GPS_EN` (IO10) n'est relié à rien sur ce PCB : le GPS reste alimenté,
   comme sur le montage actuel.

## Régénérer

```
cd hardware/pcb-support/src
python3 do_route.py      # routage (≈ 3 min), écrit routing.json
python3 build.py         # contrôle, KiCad, Gerber, BOM/CPL, aperçus
python3 verify_kicad.py && python3 verify_gerber.py
```

Dépendances : Python 3 avec numpy, scipy et Pillow. Empreintes : bibliothèque
officielle KiCad (licence dans `lib/`).
