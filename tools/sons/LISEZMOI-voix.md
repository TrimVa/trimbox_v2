# Voix de la radio : les 12 mots du script, avec la voix d'EdgeTX

Le pack vocal français officiel d'EdgeTX (celui déjà installé sur la MT12)
est lu par la voix Azure **fr-FR-DeniseNeural**, au format WAV 16 bits mono
16 kHz. `generer-voix.ps1` produit les 12 mots du script TrimBox avec **cette
même voix et ce même format** : ils se fondent dans les annonces de nombres.

## 1. Obtenir une clé Azure (une seule fois, gratuit)

1. Créer un compte sur <https://portal.azure.com> (compte Microsoft ; Azure
   demande une carte bancaire pour vérifier l'identité, rien n'est débité
   avec le niveau gratuit).
2. **Créer une ressource** → rechercher **« Speech »** (service *Speech* /
   *Speech services*) → **Créer** :
   - *Région* : **France Central** (ou une autre, à noter) ;
   - *Niveau tarifaire* : **Free F0** (500 000 caractères par mois ; les 12
     mots en font environ 160).
3. Une fois la ressource créée : **Clés et point de terminaison** (*Keys and
   Endpoint*). Noter **KEY 1** et **Emplacement/Région** (ex. `francecentral`).

## 2. Générer les fichiers

1. Double-clic sur **`generer-voix.bat`**.
2. Coller la clé, puis taper la région (ex. `francecentral`).
3. Le dossier **`SOUNDS\trimbox\`** apparaît à côté du script, avec :

| Fichier | Phrase |
|---|---|
| `meilleur.wav` | meilleur tour |
| `plus.wav` / `moins.wav` / `egal.wav` | plus / moins / égal |
| `depart.wav` / `arrivee.wav` / `efface.wav` | départ posé / arrivée posée / lignes effacées |
| `arme.wav` / `delai.wav` | ligne armée, roulez / délai dépassé |
| `vitfaib.wav` / `pasgps.wav` / `refus.wav` | vitesse trop faible / pas de GPS / commande refusée |

## 2 bis. Générer d'autres mots, à la demande

Double-clic sur **`generer-mot.bat`** (même dossier). Le script :

1. demande la clé et la région (il peut les **mémoriser** dans
   `azure-cle.txt`, à côté du script, pour ne plus les redemander ; supprimer
   ce fichier pour les oublier, ne pas le partager) ;
2. demande le dossier de destination (Entrée = `SOUNDS\trimbox\`) ;
3. puis, en boucle :
   - le **texte à prononcer** (un mot ou une phrase ; Entrée seule pour
     terminer) ;
   - le **nom du fichier** : il en propose un, tiré du texte (« Arrivée
     posée » → `arriveep`) ; **8 caractères au plus**, minuscules sans accent,
     chiffres, `-` ou `_` ;
   - il fait **écouter** le résultat, puis : *Entrée* = garder,
     `e` = réécouter, `t` = changer le texte (une autre orthographe corrige
     souvent une prononciation), `l` / `r` = 10 % plus lent / plus rapide,
     `a` = abandonner. Un fichier existant n'est remplacé qu'après
     confirmation.

Un mot nouveau ne sera prononcé par la radio que si quelque chose le joue :
le script `trmbox.lua` (qui ne connaît que les 12 mots ci-dessus) ou une
*fonction spéciale* EdgeTX « Jouer piste ». Pour **remplacer** un des 12 mots
(autre formulation, autre débit), lui donner exactement le même nom.

## 3. Copier sur la radio

MT12 branchée en USB, mode *Stockage USB* : copier le dossier **`SOUNDS`**
à la racine de la carte SD (il se fusionne avec le `SOUNDS` existant ; seul
`SOUNDS/trimbox/` est ajouté, le pack `SOUNDS/fr/` n'est pas touché).

## En cas de problème

- **« clé ou région incorrecte »** : la région doit être exactement celle de
  la ressource (page *Clés et point de terminaison*).
- **Changer une phrase** : modifier la liste `$mots` en tête de
  `generer-voix.ps1` ; le nom du fichier doit rester le même.
- La clé n'est enregistrée nulle part : le script la demande à chaque fois.
