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
