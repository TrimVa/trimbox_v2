# Sonde de la prise de données du variateur (« X-Bus »)

Croquis autonome pour l'ESP32-S3. Il remplace provisoirement le firmware
TrimBox, interroge la prise secondaire du variateur et affiche à l'écran tout
ce qui en revient. Le firmware TrimBox se reflashe ensuite normalement.

## 1. Mesurer avant de brancher

Variateur allumé, prise secondaire configurée sur **X-Bus**, mesure au
voltmètre entre le fil **signal** et le fil **−** :

| Tension au repos | Ce que ça veut dire | Câblage de la réception |
|---|---|---|
| ≈ 3,3 V | logique 3,3 V | fil signal directement sur **GPIO 4** |
| ≈ 5 V | logique 5 V | pont diviseur : 10 kΩ entre le fil et GPIO 4, 20 kΩ entre GPIO 4 et GND |
| ≈ 0 V | ligne tirée au bas, ou mauvais fil | vérifier le brochage avant d'aller plus loin |

**Les entrées de l'ESP32 ne supportent pas le 5 V.** En cas de doute, mets le
pont diviseur : il fonctionne dans les deux cas.

## 2. Câblage

| Prise du variateur | ESP32-S3 |
|---|---|
| − (noir ou marron) | **GND** — obligatoire |
| + (rouge) | **rien** : l'ESP32 est alimenté par l'USB |
| signal (orange) | **GPIO 5** à travers une résistance de **1 kΩ** (émission) et **GPIO 4** (réception, direct ou par le pont) |

La résistance de 1 kΩ protège les deux côtés si les deux parlent en même
temps : c'est une liaison à un seul fil, chacun son tour.

## 3. Essais

Flasher `xbus_probe.ino`, ouvrir le moniteur série à 115200, puis taper :

| Touche | Essai | Ce qu'on cherche |
|---|---|---|
| `a` | mesure des impulsions pendant 10 s + répartition des durées | des durées **multiples d'un même temps de bit** : signe d'une vraie liaison série |
| `r` | relevé détaillé des 200 premières durées | la structure fine des trames, à m'envoyer telle quelle |
| `f` | test du fil débranché | distingue une vraie liaison d'une entrée qui flotte (bruit) |
| `h` | poignée de main SRXL2, 115 200 puis 400 000 bauds, toutes les adresses | une réponse commençant par `A6` |
| `c` | liaison SRXL2 puis trames de commande pendant 5 s | la télémétrie, qui n'arrive qu'une fois la liaison établie |
| `e` | écoute passive à 11 vitesses, dont 500 000 et 1 M | le variateur qui parlerait tout seul |
| `b` | « break » puis écoute | certains bus ne se réveillent qu'ainsi |
| `p` | test de liaison : tire la ligne au bas puis relâche | savoir si le fil atteint vraiment le variateur (une résistance de tirage la remonte) |

**L'écho.** Émission et réception partagent le même fil : tout ce que la sonde
envoie revient dans sa propre entrée. Depuis cette version, ces octets sont
filtrés et l'affichage ne montre que ce qui vient **vraiment du variateur**,
sous le mot `REPONSE`. Si tu vois `A6 CD 16 …`, c'est notre trame à nous.

Ordre conseillé : `a`, puis `f` (pour éliminer le bruit), puis `r`, puis `h`
et `c`.

**Attention au piège du bruit.** Une entrée d'ESP32 laissée en l'air capte
des milliers de fronts par seconde, qui ressemblent à du trafic. Une vraie
liaison série donne des durées toutes multiples du temps de bit (2 µs, 4 µs,
6 µs… à 500 000 bauds) ; le bruit donne des durées quelconques. C'est
exactement ce que montre la répartition affichée par `a`.

## 4. Quoi m'envoyer

Copie-colle simplement le texte du moniteur série, même s'il est vide, en
précisant la tension mesurée et l'option choisie pour la prise. S'il y a des
octets, j'en déduis la vitesse, la structure des trames et j'écris le
décodeur pour le firmware.
