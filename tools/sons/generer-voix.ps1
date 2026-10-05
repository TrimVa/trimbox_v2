# ============================================================================
#  TrimBox — génère les 12 mots du script radio avec la voix Azure
#  fr-FR-DeniseNeural : la MÊME voix que le pack français officiel d'EdgeTX,
#  dans le même format (WAV 16 bits, mono, 16 kHz, réglages par défaut).
#
#  Il faut une clé « Speech » Azure (niveau gratuit F0 suffisant : ces 12 mots
#  font une centaine de caractères). Voir LISEZMOI-voix.md.
#
#  Lancement : double-clic sur generer-voix.bat
#  Résultat  : dossier SOUNDS\trimbox\ à côté de ce script, à copier à la
#              racine de la carte SD de la radio.
# ============================================================================
$ErrorActionPreference = "Stop"

$voix   = "fr-FR-DeniseNeural"
$format = "riff-16khz-16bit-mono-pcm"      # format du pack EdgeTX officiel
$mots = [ordered]@{
    "meilleur" = "meilleur tour"
    "plus"     = "plus"
    "moins"    = "moins"
    "egal"     = "égal"
    "depart"   = "départ posé"
    "arrivee"  = "arrivée posée"
    "efface"   = "lignes effacées"
    "arme"     = "ligne armée, roulez"
    "delai"    = "délai dépassé"
    "vitfaib"  = "vitesse trop faible"
    "pasgps"   = "pas de GPS"
    "refus"    = "commande refusée"
}

Write-Host ""
Write-Host "TrimBox - voix Azure $voix" -ForegroundColor Cyan
Write-Host ""
$cle = Read-Host "Clé de la ressource Speech Azure (KEY 1)"
$region = Read-Host "Région de la ressource (ex. francecentral, westeurope)"
$cle = $cle.Trim(); $region = $region.Trim().ToLower()
if (-not $cle -or -not $region) { throw "Clé et région obligatoires." }

$url = "https://$region.tts.speech.microsoft.com/cognitiveservices/v1"
$sortie = Join-Path $PSScriptRoot "SOUNDS\trimbox"
New-Item -ItemType Directory -Force -Path $sortie | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ok = 0
foreach ($nom in $mots.Keys) {
    $texte = $mots[$nom]
    $ssml = "<speak version='1.0' xml:lang='fr-FR'><voice name='$voix'>$texte</voice></speak>"
    $fichier = Join-Path $sortie "$nom.wav"
    try {
        Invoke-WebRequest -Uri $url -Method Post -UseBasicParsing -OutFile $fichier `
            -Headers @{
                "Ocp-Apim-Subscription-Key" = $cle
                "X-Microsoft-OutputFormat"  = $format
                "User-Agent"                = "TrimBox"
            } `
            -ContentType "application/ssml+xml; charset=utf-8" `
            -Body ([System.Text.Encoding]::UTF8.GetBytes($ssml))
        $taille = (Get-Item $fichier).Length
        if ($taille -lt 2000) { throw "fichier trop petit ($taille octets)" }
        Write-Host ("  ok  {0,-13} « {1} »" -f "$nom.wav", $texte) -ForegroundColor Green
        $ok++
    } catch {
        Write-Host ("  ÉCHEC {0} : {1}" -f "$nom.wav", $_.Exception.Message) -ForegroundColor Red
        if (Test-Path $fichier) { Remove-Item $fichier }
        if ($_.Exception.Message -match "401|403") {
            Write-Host "  -> clé ou région incorrecte (la région doit être celle de la ressource)." -ForegroundColor Yellow
            break
        }
    }
    Start-Sleep -Milliseconds 300
}

Write-Host ""
if ($ok -eq $mots.Count) {
    Write-Host "Terminé : $ok fichiers dans $sortie" -ForegroundColor Cyan
    Write-Host "Copie le dossier SOUNDS à la racine de la carte SD de la radio."
} else {
    Write-Host "$ok fichier(s) sur $($mots.Count) générés." -ForegroundColor Yellow
}
Write-Host ""
Read-Host "Appuie sur Entrée pour fermer"
