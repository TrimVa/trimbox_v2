# Par où commencer

## 1. Mettre le projet sur GitHub

1. Créer un dépôt sur GitHub (public ou privé).
2. Y déposer **tout le contenu** de ce dossier, en gardant l'arborescence
   (dont le dossier caché `.github/`).
   - Depuis un ordinateur : *Add file → Upload files*, glisser le contenu du
     dossier. Si le dossier `.github` n'est pas pris (dossier caché), le
     créer à la main : *Add file → Create new file*, nom
     `.github/workflows/build-s3.yml`, coller le contenu du fichier.
   - Ou en ligne de commande :
     ```bash
     git init && git add -A && git commit -m "TrimBox DIY S3"
     git branch -M main
     git remote add origin https://github.com/<pseudo>/<dépôt>.git
     git push -u origin main
     ```
3. Onglet **Actions** : la compilation *Firmware S3* démarre seule. Compter
   **10 à 15 min** la première fois (installation du compilateur et de
   l'émulateur), moins ensuite. Tout doit passer au vert.

### Si le dépôt contient déjà la v1 (XIAO)

Le workflow de la v1 refuse la compilation s'il trouve **plusieurs fichiers
`.ino`** dans le dépôt : il échouera dès que `trimbox_s3/` sera ajouté. Il
suffit de limiter sa vérification au dossier `trimbox_diy/` (la ligne
`find … -name '*.ino'` de `build.yml`). Le plus simple reste **un dépôt neuf**
pour la version S3.

## 2. Activer la console en ligne (Bluetooth)

*Settings → Pages → Branch : `main`, dossier `/ (root)` → Save.*

La console est alors à l'adresse `https://<pseudo>.github.io/<dépôt>/`
(Chrome sur Android pour le Bluetooth). La console **Wi-Fi** n'a besoin de
rien : elle est dans le firmware.

## 3. Premier flash (une seule fois, par câble, depuis un ordinateur)

1. *Actions* → dernière compilation verte → artefact **firmware-s3-complet**
   → dézipper → `trimbox_s3-complet.bin`.
2. Chrome ou Edge sur ordinateur : https://espressif.github.io/esptool-js/
3. Carte branchée sur sa prise **USB** (pas « UART »), câble de données.
   *Connect* (si rien : maintenir **BOOT**, appuyer sur **RST**, relâcher
   **BOOT**).
4. Adresse **`0x0`**, fichier `trimbox_s3-complet.bin`, **Program**
   (2 à 4 min), puis **RST**.
5. 30 s plus tard, le Wi-Fi **`TrimBox-Buggy-1`** apparaît (mot de passe
   `trimbox-rc`) : ouvrir http://192.168.4.1/ sur le téléphone.

Détails, câblage et bancs d'essai : `trimbox_s3/LISEZMOI.md`.

## 4. Mises à jour suivantes (sans PC)

1. Modifier le code dans GitHub (crayon), valider : la compilation repart.
2. Artefact **firmware-s3-app** → `trimbox_s3-app.bin`.
3. Voiture et enregistrement arrêtés, console Wi-Fi → *Mise à jour du
   firmware* → envoyer le fichier.

Ne jamais modifier `trimbox_s3/partitions.csv` dans une mise à jour Wi-Fi.

## 5. Radio MT12

Copier `lua/trmbox.lua` dans `SCRIPTS/TELEMETRY/` de la radio et régler le
mixage de CH8 : voir `lua/LISEZMOI.md`.

## Reprendre le développement avec une IA

Fournir `trimbox-etat-projet.json`, `CAHIER-DES-CHARGES.md`,
`CAHIER-DES-CHARGES-V2.md`, puis les fichiers concernés. Chapitres
prioritaires : v1 §9 et v2 §10 (pièges connus). Toute modification de la
console : incrémenter `CONSOLE_VER`, puis `python3 tools/embed_console.py`.
Les tests (`tests/`) doivent rester verts.
