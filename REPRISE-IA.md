# Reprendre TrimBox avec un assistant IA

Cette archive contient **tout le projet** (firmware ESP32-S3, console web,
script radio, tests, outils, documentation) et l'état du projet au format JSON.
Elle suffit pour reprendre le développement dans une conversation neuve.

## 1. Ce qu'il faut joindre à la conversation

Le plus simple : **toute l'archive** (`trimbox-reprise-ia.zip`). Si l'outil
limite la taille ou le nombre de fichiers, joindre au minimum, dans cet ordre :

1. `trimbox-etat-projet.json` — état, règles dures, prochaines étapes ;
2. `CAHIER-DES-CHARGES.md` — spécification complète (§0 règles dures, §12
   pièges connus) ;
3. les fichiers concernés par la demande (par exemple `index.html` pour la
   console, `trimbox_s3/src/…` pour le firmware, `lua/trmbox.lua` pour la
   radio), et leurs bancs de test (`tests/…`).

## 2. Texte à coller en premier message

> Je reprends le projet TrimBox (enregistreur GPS/IMU pour voiture RC
> sur ESP32-S3, avec console web et script radio EdgeTX). Je te joins l'archive
> de reprise : lis d'abord `trimbox-etat-projet.json` (en particulier
> `regles_dures`, `etat` et `prochaines_etapes`), puis `CAHIER-DES-CHARGES.md`
> §0 (règles dures) et §12 (pièges connus), puis les fichiers concernés par ma
> demande. Respecte la convention de versionnage de la console (`CONSOLE_VER`
> + `tools/embed_console.py`). Valide toute modification de la console en
> **exécutant** le script (`tests/web`, `tests/native/console_check.js`), toute
> modification du firmware par `make -C tests/native` (et l'émulateur QEMU si
> possible), toute modification du script Lua par `tests/lua`. Mets à jour
> `trimbox-etat-projet.json` et le cahier des charges à la fin. Réponds en
> français, commentaires du code en français.
>
> Ma demande : …

## 3. Règles que l'assistant doit respecter (rappel)

- **Console** : ne jamais modifier `distM`, `feed`, `anal.pts` / `anal.scr`, ni
  `despike`, `despikeSpeed`, `plausible`, `gStats`, `pathLength`. Valider en
  exécutant la page, pas par une vérification de syntaxe.
- **Console** : `CONSOLE_VER` +1 à chaque modification, puis
  `python3 tools/embed_console.py`.
- **Lua** : jamais `table.*` ni `ipairs` dans `lua/trmbox.lua`.
- **Firmware** : `trimbox_s3/partitions.csv` ne change jamais par mise à jour
  Wi-Fi ; le chrono C++ (`core/lapcore`) et celui de la console restent
  identiques à 1 ms près.
- **Sécurité** : le module ne pilote jamais la voiture ; Wi-Fi et Bluetooth se
  coupent quand elle roule.

## 4. Contenu de l'archive

| Élément | Rôle |
|---|---|
| `trimbox-etat-projet.json` | État du projet pour l'IA |
| `CAHIER-DES-CHARGES.md` | Spécification unique (version 3.0) |
| `README.md`, `DEMARRAGE.md` | Utilisation, mise en route |
| `trimbox_s3/` | Firmware (croquis Arduino) |
| `index.html`, `trimbox-sw.js`, `trimbox-diy-console-demo.html` | Console web |
| `lua/` | Script radio MT12 et son mode d'emploi |
| `tests/` | Bancs : natif, navigateur, Lua, émulateur |
| `tools/` | Intégration de la console, schéma, boîtier, voix, sondes |
| `docs/` | Schéma de câblage, boîtier imprimé 3D |
| `.github/workflows/build-s3.yml` | Compilation et tests en ligne |
| `binaires/` | Firmware 2.0-a16 compilé (mise à jour Wi-Fi et premier flash) |

## 5. Vérifier que tout est en ordre (si l'assistant peut exécuter du code)

```bash
make -C tests/native                                            # logique du firmware
node tests/native/console_check.js index.html tests/native/lap_cases.json
lua5.2 tests/lua/test_trmbox.lua lua/trmbox.lua                 # script radio
python3 tools/embed_console.py                                  # console → firmware
g++ -std=c++17 -O1 -o tests/web/fakedev tests/web/fakedev.cpp trimbox_s3/src/core/*.cpp -lm
python3 tests/web/console_web_test.py tests/web/fakedev 8088    # console dans Chromium
```

`lap_cases.json` est produit par `make -C tests/native` (fichier ignoré par
git). Compilation du firmware et émulateur : `CAHIER-DES-CHARGES.md` §10.
