# Script Lua TrimBox pour RadioMaster MT12

`trmbox.lua` : écran de télémétrie EdgeTX (128 × 64) qui affiche le chrono
calculé par le module, annonce chaque tour et pose les lignes de départ et
d'arrivée **depuis la radio**.

## Installation

1. Brancher la MT12 en USB, choisir *Stockage USB (carte SD)*.
2. Copier `trmbox.lua` dans **`SCRIPTS/TELEMETRY/`** (le nom doit rester de
   6 caractères au plus : limite d'EdgeTX pour ces écrans), ou décompresser
   `carte-sd-mt12.zip` à la racine de la carte.
3. **Voix (facultatif)** : copier les 12 mots du script dans
   **`SOUNDS/trimbox/`** (voir *Annonces vocales* ci-dessous). Sans eux, le
   script remplace chaque mot par un bip.
4. Sur la radio : *Radio → Général → Langue des voix* = **Français**.
5. Sur la radio : **Modèle → Télémétrie → Découvrir les capteurs**, voiture
   et module allumés. Doivent apparaître notamment `GPS`, `GSpd`, `Sats`,
   `RQly` et **`FM`** (messages du chrono).
6. Toujours dans *Télémétrie*, **Écran 1 → Script → trmbox**.
7. Affichage : depuis l'écran principal, **appui long sur TELE** (ou la touche
   de pages de télémétrie selon la version d'EdgeTX).

## Mixage de la voie 8 (pose de ligne) — INDISPENSABLE

Le script n'envoie rien au module directement : il écrit la variable globale
**GV9**, et c'est un **mixage** qui la transmet sur **CH8**, la voie que le
module écoute. **Sans ce mixage, les commandes de pose ne partent pas** : la
page *Lignes* affiche `Mode : aucune`, et rien ne se passe à l'appui.

Sur la MT12, *Modèle → Mixeur*, ligne **CH8** :

1. Sélectionner **CH8** et l'ouvrir (appui long → *Modifier*, ou *Insérer*
   si la ligne est vide).
2. **Source** : `MAX`.
3. **Poids** : appuyer sur la valeur et la remplacer par la variable
   globale **GV9** (tourner la molette au-delà de 100 : les GV apparaissent
   après les nombres, ou appui long sur le champ selon la version).
4. **Mode / opération** : `+=` (« Ajouter »).
5. Sortir. Au repos, CH8 doit afficher **0 %** (page *Sorties* ou l'écran des
   voies) : le module ignore toute commande tant qu'il n'a pas vu la voie au
   neutre.

**Vérification immédiate** : la page *Lignes* du script affiche en bas à
gauche la valeur réelle de la voie, `CH8 +0%`. Pendant un envoi, elle doit
passer à **+100 %** (départ) ou **−100 %** (arrivée, effacement). Si elle
reste à 0, le script prévient : « **CH8 immobile : mixage ?** ».

Autre possibilité, sans le script : un **inter 3 positions** (SA par exemple)
directement sur CH8, poids 100, mode Ajouter. Haut 0,5 s = départ ; bas 0,5 à
3 s = arrivée ; bas 3 s = effacement.

## Annonces vocales

À chaque passage de ligne :

1. « **meilleur tour** » si c'est ton record de la session ;
2. **le temps du tour**, au centième : « vingt et un virgule trente-quatre » ;
3. **l'écart avec le tour précédent** : « moins zéro virgule trente-cinq »
   (plus rapide), « plus un virgule zéro zéro » (plus lent), ou « égal »
   (moins d'un demi-centième d'écart).

L'écart n'est annoncé que si le tour précédent a bien été reçu (numéros qui
se suivent) : après une coupure radio, pas de comparaison trompeuse.

À la pose d'une ligne, la radio confirme : « départ posé », « arrivée
posée », « lignes effacées », ou refuse : « vitesse trop faible », « pas de
GPS », « commande refusée ».

**Les nombres** sont dits par le pack vocal de la radio (`SOUNDS/fr/`).
**Les mots** sont 12 fichiers à placer dans `SOUNDS/trimbox/`, à produire
avec l'outil de synthèse vocale de ton choix :

| Fichier | Texte |
|---|---|
| `meilleur.wav` | meilleur tour |
| `plus.wav` / `moins.wav` / `egal.wav` | plus / moins / égal |
| `depart.wav` / `arrivee.wav` / `efface.wav` | départ posé / arrivée posée / lignes effacées |
| `arme.wav` / `delai.wav` | ligne armée, roulez / délai dépassé |
| `vitfaib.wav` / `pasgps.wav` / `refus.wav` | vitesse trop faible / pas de GPS / commande refusée |

La liste complète de ce que la radio peut dire (mots du script **et** pack
vocal français d'EdgeTX, 245 fichiers, avec priorité et rôle de chacun) est
dans **`tools/sons/liste-sons-mt12.csv`** (ouvrable dans Excel). La même
liste au format des générateurs officiels d'EdgeTX
(dépôt `EdgeTX/edgetx-sdcard-sounds`) : `tools/sons/fr-FR-trimbox-edgetx.csv`.

**Pour une voix homogène, le plus simple** : le pack français officiel
d'EdgeTX est lu par la voix Azure **fr-FR-DeniseNeural**. Le script
`tools/sons/generer-voix.bat` (Windows) produit les 12 mots avec cette même
voix et dans le même format, à partir d'une clé Azure gratuite : mode
d'emploi dans `tools/sons/LISEZMOI-voix.md`.

**Format exigé par EdgeTX** : WAV PCM **16 bits, mono, 32 kHz** (8 ou 16 kHz
acceptés), sans silence au début ni à la fin. Conversion avec ffmpeg :

```
ffmpeg -i source.mp3 -af "silenceremove=start_periods=1:start_threshold=-45dB,areverse,silenceremove=start_periods=1:start_threshold=-45dB,areverse" -ar 32000 -ac 1 -sample_fmt s16 plus.wav
```

- **Dossier des voix** : le script cherche `SOUNDS/trimbox/`, puis
  `SOUNDS/trmbox/` et `SOUNDS/fr/trimbox/`. Le premier qui contient
  `plus.wav` est retenu au premier mot prononcé.
- **Rien du tout, seulement la vibration** : les annonces passent par le
  **volume voix** de la radio (*Radio → Général → Son* : volume et mode ne
  doivent pas être à « silencieux »). La vibration, elle, confirme juste
  l'appui sur ENT.
- **Fichiers absents** : le script le détecte et remplace chaque mot par un
  bip (aigu pour « moins », grave pour « plus », double bip pour « meilleur
  tour », bip grave pour un refus).
- **Couper l'annonce de l'écart** : `ANNOUNCE_PREV = false` en tête du script.

L'écran, lui, continue d'afficher l'écart avec le **meilleur** tour.

## Utilisation

Trois pages : **Chrono**, **Machine**, **Lignes**.

La page **Machine** affiche la vitesse, la vitesse maximale, les satellites,
la qualité de liaison, le RSSI, l'altitude et la **tension de la batterie de
propulsion**, avec la tension par élément.

- **Vitesse** : en dessous de **3 km/h**, elle est affichée à 0 et n'entre pas
  dans la vitesse maximale. À l'arrêt, le GPS ne mesure que son propre bruit
  (réglage `SPEED_MIN` en tête du script).
- **Batterie** : le script prend le premier capteur trouvé parmi `Batt`,
  `RxBt`, `VFAS` et `A1` (liste `BATT_NAMES`). Si ton capteur porte un autre
  nom, ajoute-le dans cette liste. La tension par élément est déduite du
  nombre d'éléments, calculé à partir de la tension mesurée.
- **Heure** : date et heure du GPS, lues dans le capteur **`Date`** et
  converties en heure locale. Le GPS donne l'heure UTC : le script ajoute
  `TZ_OFFSET` (1 pour la France) et une heure de plus en été si `TZ_EU_DST`
  vaut `true` (dernier dimanche de mars → dernier dimanche d'octobre). Ce
  capteur n'existe qu'avec **ExpressLRS 4.1 ou plus récent** sur l'émetteur
  et le récepteur, et une version d'EdgeTX qui décode la trame CRSF `0x03`.
  Sans lui, la ligne affiche `--`.

**Mettre la radio à l'heure automatiquement** : *Modèle → Télémétrie*,
sélectionner le capteur `Date`, cocher l'option d'ajustement de l'horloge
(libellé selon la langue d'EdgeTX, *Adjust RTC* en anglais).

| Touche | Effet |
|---|---|
| Molette | page suivante / précédente — sur *Lignes* : choix de l'action (au-delà de la première ou de la dernière, on change de page) |
| ENT (court) | page suivante — sur *Lignes* : **exécute** l'action choisie |
| ENT (long) | sur *Chrono* : remise à zéro de l'affichage (sur *Lignes*, même effet que l'appui court) |

« **Effacer lignes** » demande une confirmation : le premier appui affiche
« ENT pour CONFIRMER » pendant 3 s, le second efface.

**Poser la ligne de départ — voiture ARRÊTÉE (le plus simple)** :

1. Pose la voiture **sur la ligne voulue**, dans le sens de la course, et
   attends que le GPS ait son fix.
2. Page *Lignes*, molette sur « Poser DEPART », **ENT**.
3. La radio dit « ligne armée, roulez » et affiche un compte à rebours
   « ROULEZ 10s » : **démarre dans les 10 secondes**.
4. Dès les premiers mètres, le module prend le cap du départ et pose la ligne
   perpendiculairement, 0,5 m devant la position de départ. La radio dit
   « départ posé ». Le départ compte déjà : en circuit, le premier tour
   commence là ; en dragster, c'est un **départ arrêté** chronométré.
5. Pas de départ dans les 10 s : « délai dépassé », rien n'est posé.

**Voiture lancée** (au-dessus de 7 km/h) : la même commande pose la ligne
immédiatement à l'endroit où passe la voiture, perpendiculaire à sa
trajectoire, comme avant.

La ligne d'**arrivée** (mode dragster) se pose de la même façon, avec
« Poser ARRIVEE ». Refus possible : `PAS DE FIX` (bip grave ou « pas de GPS »).

- Une seule ligne = **circuit** : un temps par tour.
- Départ **et** arrivée = **dragster** : un temps par parcours, plus les
  chronos intermédiaires (0-30, 0-50, 0-80 km/h ; 25, 50, 100 m).
- Les lignes sont **gardées par le module** d'une session à l'autre ; le
  script retrouve leur état tout seul.

À chaque tour : **annonce vocale du temps** ; double bip aigu avant
l'annonce si c'est le **meilleur tour**. L'en-tête affiche l'état
(`REC`, `PAUSE`, `STOP`, `NOFIX`), les lignes (`C` circuit, `D` dragster,
`-` aucune) et `W` quand le Wi-Fi de la console est allumé.

## Réglage ExpressLRS conseillé

Le récepteur ne transmet **qu'un message texte à la fois** : le module tient
donc chaque message 0,7 s. Il faut au moins environ 2 trames de télémétrie
par seconde. Avec le script *ExpressLRS* de la radio, choisir un **Telem
Ratio** de **1:16** ou plus fréquent (1:8, 1:4…) selon le *Packet Rate*.
Si des tours manquent à l'affichage alors qu'ils apparaissent dans la
console, c'est ce réglage.

## Validation sur la vraie radio

**Validé sur la MT12 (2.3)** : `getValue("FM")` renvoie bien le texte (banc
§11.2 n°4), touches et navigation (événements virtuels `EVT_VIRTUAL_*`),
lisibilité de l'affichage, annonces vocales et accusés de pose. Le script
lit aussi la file CRSF brute (`crossfireTelemetryPop`) si EdgeTX y dépose
ces trames.

**Reste à valider** : le mixage CH8 configuré sur le modèle, la pose de ligne
à l'arrêt sur la voiture, et l'enregistrement automatique en roulage.

⚠️ **Règle pour toute modification du script** : ni `table.insert`,
`table.remove`, `table.concat`, ni `ipairs`. EdgeTX ne fournit pas la
bibliothèque `table` aux scripts de télémétrie : la radio plante au premier
tour reçu. Lancer `tests/lua` après chaque modification.
