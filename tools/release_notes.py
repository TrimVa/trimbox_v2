#!/usr/bin/env python3
# ============================================================================
#  TrimBox — rédige les notes d'une release GitHub (appelé par le workflow
#  .github/workflows/build-s3.yml, job « Publication »).
#
#  python3 tools/release_notes.py <dossier des fichiers publiés> [tag précédent]
#
#  Écrit le texte (Markdown) sur la sortie standard : versions, fichiers et
#  leur usage, procédure de flash, nouveautés depuis la release précédente
#  (messages de commit), tests passés, empreintes SHA-256.
#  Fonctionne aussi en local, pour relire les notes avant de pousser.
# ============================================================================
import hashlib
import os
import re
import subprocess
import sys

RACINE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def lire(chemin):
    with open(os.path.join(RACINE, chemin), encoding="utf-8") as f:
        return f.read()


def version_firmware():
    m = re.search(r'#define\s+FIRMWARE_VER\s+"([^"]+)"', lire("trimbox_s3/src/config.h"))
    if not m:
        sys.exit("FIRMWARE_VER introuvable dans trimbox_s3/src/config.h")
    return m.group(1)


def version_console():
    m = re.search(r"const\s+CONSOLE_VER\s*=\s*'([^']+)'", lire("index.html"))
    if not m:
        sys.exit("CONSOLE_VER introuvable dans index.html")
    return m.group(1)


def git(*args):
    return subprocess.run(["git", *args], cwd=RACINE, capture_output=True,
                          text=True, check=False).stdout.strip()


def nouveautes(tag_prec):
    """Commits depuis la release précédente (sujet + corps), du plus récent au plus ancien."""
    if tag_prec:
        plage = f"{tag_prec}..HEAD"
    else:
        # pas encore de release : commits depuis le passage à la version
        # précédente (2e commit le plus récent qui modifie FIRMWARE_VER)
        chg = git("log", "--format=%H", r"-G#define[[:space:]]+FIRMWARE_VER",
                  "--", "trimbox_s3/src/config.h").split()
        plage = f"{chg[1]}..HEAD" if len(chg) > 1 else "-1"
    brut = git("log", "--no-merges", "--format=%h%x1f%s%x1f%b%x1e", plage)
    lignes = []
    for bloc in brut.split("\x1e"):
        bloc = bloc.strip()
        if not bloc:
            continue
        h, sujet, corps = (bloc.split("\x1f") + ["", ""])[:3]
        lignes.append(f"- **{sujet.strip()}** (`{h}`)")
        for l in corps.splitlines():
            l = l.rstrip()
            # les lignes d'attribution n'ont rien à faire dans les notes
            if not l or re.match(r"(Co-Authored-By|Claude-Session|Signed-off-by):", l, re.I):
                continue
            lignes.append(f"  {l}")
    return "\n".join(lignes) if lignes else "- (aucun commit depuis la release précédente)"


def taille(octets):
    if octets >= 1024 * 1024:
        return f"{octets / 1024 / 1024:.2f} Mio".replace(".", ",")
    return f"{octets / 1024:.0f} Kio"


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__ or "usage : release_notes.py <dossier> [tag précédent]")
    dossier = sys.argv[1]
    tag_prec = sys.argv[2] if len(sys.argv) > 2 and sys.argv[2] else ""
    fw, cons = version_firmware(), version_console()
    depot = os.environ.get("GITHUB_REPOSITORY", "TrimVa/trimbox_v2")
    serveur = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
    run_id = os.environ.get("GITHUB_RUN_ID")
    commit = git("rev-parse", "--short", "HEAD")
    date = git("log", "-1", "--format=%cd", "--date=format:%d/%m/%Y %H:%M")

    usage = {
        "trimbox_s3-app.bin": "**Mise à jour par Wi-Fi** (module déjà sous TrimBox 2.x) : console → *Réglages* → *Mise à jour du firmware*",
        "trimbox_s3-complet.bin": "**Premier flash par câble**, adresse `0x0` (bootloader + partitions + application)",
        "trimbox_s3.ino.bootloader.bin": "Chargeur de démarrage seul (`0x0`) — flash manuel en plusieurs fichiers",
        "trimbox_s3.ino.partitions.bin": "Table des partitions seule (`0x8000`) — flash manuel en plusieurs fichiers",
        "trimbox-radio.zip": "Radio EdgeTX (MT12) : `trmbox.lua` → `SCRIPTS/TELEMETRY/`, dossier `SOUNDS` → racine de la carte SD",
        "index.html": "Console autonome (Bluetooth depuis Chrome Android, ou ouverture de fichiers hors ligne)",
    }
    fichiers = sorted(f for f in os.listdir(dossier) if os.path.isfile(os.path.join(dossier, f))
                      and f != "SHA256SUMS.txt")
    ordre = list(usage)
    fichiers.sort(key=lambda f: ordre.index(f) if f in ordre else len(ordre))

    tableau = ["| Fichier | Taille | Usage |", "|---|---|---|"]
    empreintes = []
    for f in fichiers:
        p = os.path.join(dossier, f)
        with open(p, "rb") as fh:
            sha = hashlib.sha256(fh.read()).hexdigest()
        empreintes.append(f"{sha}  {f}")
        tableau.append(f"| `{f}` | {taille(os.path.getsize(p))} | {usage.get(f, '')} |")

    lien_run = f"{serveur}/{depot}/actions/runs/{run_id}" if run_id else ""
    prec = f"depuis [`{tag_prec}`]({serveur}/{depot}/releases/tag/{tag_prec})" if tag_prec else "(première release)"

    print(f"""## TrimBox — firmware {fw} · console {cons}

Compilé le {date} depuis le commit `{commit}` (ESP32-S3-DevKitC-1 N16R8, core Arduino `esp32:esp32@3.3.12`, NimBLE 2.5.1).

### Quel fichier prendre ?

{chr(10).join(tableau)}

### Mise à jour (module déjà sous TrimBox 2.x)

1. Voiture arrêtée, aucun enregistrement en cours.
2. Se connecter au Wi-Fi du module, ouvrir http://192.168.4.1/
3. *Réglages* → *Mise à jour du firmware* → envoyer `trimbox_s3-app.bin`.
   Le module redémarre ; s'il ne démarre pas correctement, il revient tout seul à la version précédente.

### Premier flash (par câble, une seule fois)

1. Chrome ou Edge sur ordinateur : https://espressif.github.io/esptool-js/
2. Carte branchée sur sa prise **USB** (pas « UART »), câble de données → *Connect*
   (si rien : maintenir **BOOT**, appuyer sur **RST**, relâcher **BOOT**).
3. Adresse **`0x0`**, fichier `trimbox_s3-complet.bin` → *Program* (2 à 4 min), puis **RST**.
4. 30 s plus tard, le Wi-Fi `TrimBox-Buggy-1` apparaît (mot de passe `trimbox-rc`) : http://192.168.4.1/

> ⚠️ Le fichier complet **efface la mémoire d'enregistrement** (sessions, réglages, nom du véhicule). Pour un module déjà installé, préférer la mise à jour par Wi-Fi.

Détails : [`DEMARRAGE.md`]({serveur}/{depot}/blob/{commit}/DEMARRAGE.md) et [`trimbox_s3/LISEZMOI.md`]({serveur}/{depot}/blob/{commit}/trimbox_s3/LISEZMOI.md).

### Nouveautés {prec}

{nouveautes(tag_prec)}

### Vérifications passées avant publication

- Tests natifs de la logique du firmware (chrono, protocoles, automate, configuration)
- Chrono du firmware identique à celui de la console
- Script Lua de la radio (banc avec API EdgeTX simulée)
- Compilation ESP32-S3 sans avertissement
- Console embarquée dans Chromium (parcours complet en Wi-Fi, mise à jour avec ce `.bin`)
- Émulateur QEMU : Wi-Fi auto à l'arrêt, pose de ligne, tours, coupure d'alimentation, retour arrière après une mise à jour qui plante
{f"{chr(10)}Journal de compilation : {lien_run}" if lien_run else ""}

### Empreintes SHA-256

```
{chr(10).join(empreintes)}
```
""")


if __name__ == "__main__":
    main()
