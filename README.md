# TrimBox DIY S3

Enregistreur de télémétrie GPS/inertiel pour voitures RC, sur **ESP32-S3**
(DevKitC-1 N16R8), avec chrono au tour affiché et annoncé sur la radio
**RadioMaster MT12** (ExpressLRS), console web embarquée et mise à jour par
Wi-Fi.

## Ce que fait le module

- Enregistre seul, sans téléphone : GPS 25 Hz + accéléromètre/gyroscope,
  environ **1 h 43** de mémoire à 25 Hz.
- **Chrono embarqué** : la ligne de départ (et d'arrivée en dragster) se pose
  depuis la radio, en roulant. Temps au tour, meilleur tour, écart et chronos
  intermédiaires sont envoyés à la MT12, qui les annonce à voix haute.
- **Console web** : pilotage, téléchargement, analyse (tracé, tours, G…).
  - par **Bluetooth**, depuis https://`<pseudo>`.github.io/`<dépôt>`/ ;
  - par **Wi-Fi** : le module ouvre son propre réseau quand la voiture est
    arrêtée depuis 30 s (http://192.168.4.1/), et le coupe dès qu'elle roule.
- **Mise à jour du firmware par Wi-Fi** depuis le téléphone, avec retour
  automatique à la version précédente si la nouvelle ne démarre pas bien.

## Contenu du dépôt

```
trimbox_s3/                   firmware ESP32-S3 (croquis Arduino)
    trimbox_s3.ino            point d'entrée
    partitions.csv            table de partitions 16 Mo — NE PAS MODIFIER
    src/config.h              pseudo, mot de passe Wi-Fi, brochage, réglages
    src/core/                 logique sans matériel, testée sur PC
    src/hw/                   pilotes : GNSS, IMU, flash, Bluetooth, Wi-Fi, radio, OTA
    src/console_gz.h          console intégrée (générée par tools/embed_console.py)
    LISEZMOI.md               câblage, premier flash, bancs d'essai, mises à jour
lua/trmbox.lua                script de télémétrie pour la MT12 (+ LISEZMOI.md)
index.html                    console web (GitHub Pages ET intégrée au firmware)
trimbox-sw.js                 service worker (console hors ligne, facultatif)
trimbox-diy-console-demo.html redirection des anciens liens vers la démo
tools/embed_console.py        intègre index.html au firmware
tests/                        tests : PC, navigateur, émulateur, script Lua
.github/workflows/build-s3.yml  compilation + tests à chaque envoi sur GitHub
CAHIER-DES-CHARGES.md         spécification v1 (protocole, console, pièges)
CAHIER-DES-CHARGES-V2.md      spécification v2 (ESP32-S3, radio, Wi-Fi, OTA)
trimbox-etat-projet.json      état du projet, pour reprendre avec une IA
```

## Démarrer

Voir **[DEMARRAGE.md](DEMARRAGE.md)** : mise en ligne du dépôt, activation de
la console, premier flash, puis mises à jour.

## Personnaliser

Dans `trimbox_s3/src/config.h`, avant la compilation :

```c
#define DEVICE_NICKNAME "Buggy 1"      // ≤ 16 caractères : nom Bluetooth et Wi-Fi
#define WIFI_PASS       "trimbox-rc"   // mot de passe du Wi-Fi du module (8 car. min.)
```

Le module s'annonce alors `TrimBox Buggy 1` en Bluetooth et `TrimBox-Buggy-1`
en Wi-Fi. Modifier ces lignes dans l'interface web de GitHub relance la
compilation ; le nouveau firmware s'installe ensuite par Wi-Fi.

## Tests automatiques

À chaque envoi, GitHub Actions (onglet **Actions**) :

1. teste la logique du firmware sur PC et compare son chrono à celui de la
   console ;
2. teste le script Lua avec l'API EdgeTX simulée ;
3. compile le firmware ;
4. charge la console embarquée dans un navigateur, avec le vrai serveur web du
   firmware, jusqu'à l'envoi d'une mise à jour ;
5. fait tourner le firmware dans un émulateur ESP32-S3 : Wi-Fi automatique,
   pose de ligne depuis le script Lua, tours chronométrés, coupure
   d'alimentation, retour arrière après une mise à jour qui plante.

Le firmware n'est publié en artefact que si les tests de logique passent.

## État

Firmware **2.0-a4**, console **1.7.5**. Écrit et testé sur PC, dans un
navigateur et sous émulateur ; **essais sur le matériel en cours**. À venir :
lecture de l'ESC XC-E8 (port X-Bus), en attente de captures.
