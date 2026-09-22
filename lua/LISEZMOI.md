# Script Lua TrimBox pour RadioMaster MT12

`trmbox.lua` : écran de télémétrie EdgeTX (128 × 64) qui affiche le chrono
calculé par le module, annonce chaque tour et pose les lignes de départ et
d'arrivée **depuis la radio**.

## Installation

1. Brancher la MT12 en USB, choisir *Stockage USB (carte SD)*.
2. Copier `trmbox.lua` dans **`SCRIPTS/TELEMETRY/`** (le nom doit rester de
   6 caractères au plus : limite d'EdgeTX pour ces écrans).
3. Sur la radio : **Modèle → Télémétrie → Découvrir les capteurs**, voiture
   et module allumés. Doivent apparaître notamment `GPS`, `GSpd`, `Sats`,
   `RQly` et **`FM`** (messages du chrono).
4. Toujours dans *Télémétrie*, **Écran 1 → Script → trmbox**.
5. Affichage : depuis l'écran principal, **appui long sur TELE** (ou la touche
   de pages de télémétrie selon la version d'EdgeTX).

## Mixage de la voie 8 (pose de ligne)

Le script écrit la variable globale **GV9** ; un mixage la transmet au module
sur **CH8**. *Modèle → Mixeur → CH8* :

| Ligne | Source | Poids | Mode | Rôle |
|---|---|---|---|---|
| 1 | **MAX** | **GV9** | Ajouter | commande envoyée par le script |
| 2 (facultatif) | un inter 3 positions (ex. SA) | 100 | Ajouter | pose à l'inter, sans passer par le script |

Au repos, CH8 doit être **au neutre** (0 %) : le module ignore toute commande
tant qu'il n'a pas vu la voie au neutre.

## Utilisation

Trois pages : **Chrono**, **Machine**, **Lignes**.

| Touche | Effet |
|---|---|
| Molette | page suivante / précédente |
| ENT (court) | page suivante — sur *Lignes* : action suivante |
| ENT (long) | sur *Lignes* : exécuter l'action choisie — sur *Chrono* : remise à zéro de l'affichage |

**Poser la ligne de départ** : page *Lignes*, « Poser DEPART », **appui long
sur ENT au moment où la voiture passe à l'endroit voulu**, en roulant à plus
de 7 km/h. La ligne est posée perpendiculairement à la trajectoire, là où
était la voiture au début de l'appui. Le module confirme par `DEPART OK`
(double bip), ou refuse (`VIT FAIBLE`, `PAS DE FIX` : bip grave).

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

## Points à valider sur la vraie radio

- `getValue("FM")` renvoie bien le texte (banc v2 §9 n°4). Le script lit
  aussi la file CRSF brute (`crossfireTelemetryPop`) si EdgeTX y dépose ces
  trames.
- Codes de touches de la MT12 : le script utilise les événements
  « virtuels » d'EdgeTX (`EVT_VIRTUAL_*`), prévus pour toutes les radios.
