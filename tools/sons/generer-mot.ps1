# ============================================================================
#  TrimBox — génère À LA DEMANDE des mots ou phrases avec la voix Azure
#  fr-FR-DeniseNeural : la MÊME voix et le MÊME format (WAV 16 bits, mono,
#  16 kHz) que le pack français officiel d'EdgeTX et que generer-voix.ps1.
#
#  Le script demande le texte à prononcer et le nom du fichier, fait écouter
#  le résultat, puis le range dans SOUNDS\trimbox\ (ou un autre dossier).
#  On peut enchaîner autant de mots que l'on veut ; Entrée seule pour finir.
#
#  Il faut une clé « Speech » Azure (niveau gratuit F0) : voir LISEZMOI-voix.md.
#  Lancement : double-clic sur generer-mot.bat
# ============================================================================
$ErrorActionPreference = "Stop"

$voix   = "fr-FR-DeniseNeural"
$format = "riff-16khz-16bit-mono-pcm"      # format du pack EdgeTX officiel
$NOM_MAX = 8                               # noms courts : sûrs sur la carte SD et dans EdgeTX
$fichierCle = Join-Path $PSScriptRoot "azure-cle.txt"

# --------------------------------------------------------------- utilitaires
# Nom de fichier proposé à partir du texte : minuscules, sans accent, lettres
# et chiffres seulement, 8 caractères au plus (« Arrivée posée » → arriveep).
function Nom-Propose([string]$texte) {
    $d = $texte.Normalize([Text.NormalizationForm]::FormD)
    $s = -join ($d.ToCharArray() | Where-Object {
        [Globalization.CharUnicodeInfo]::GetUnicodeCategory($_) -ne
        [Globalization.UnicodeCategory]::NonSpacingMark })
    $s = ($s.ToLower() -replace '[^a-z0-9]', '')
    if ($s.Length -gt $NOM_MAX) { $s = $s.Substring(0, $NOM_MAX) }
    if (-not $s) { $s = "mot" }
    return $s
}

function Xml-Echappe([string]$t) {
    return $t.Replace('&', '&amp;').Replace('<', '&lt;').Replace('>', '&gt;').Replace('"', '&quot;').Replace("'", '&apos;')
}

function Demande-ON([string]$question, [bool]$defaut) {
    $suffixe = if ($defaut) { "(O/n)" } else { "(o/N)" }
    $r = (Read-Host "$question $suffixe").Trim().ToLower()
    if (-not $r) { return $defaut }
    return $r.StartsWith("o")
}

# Synthèse d'un texte à une vitesse donnée (0 = normale, -20 = 20 % plus lent).
function Synthese([string]$texte, [int]$vitesse, [string]$fichier) {
    $corps = Xml-Echappe $texte
    if ($vitesse -ne 0) {
        $signe = if ($vitesse -gt 0) { "+" } else { "" }
        $corps = "<prosody rate='$signe$vitesse%'>$corps</prosody>"
    }
    $ssml = "<speak version='1.0' xml:lang='fr-FR'><voice name='$voix'>$corps</voice></speak>"
    Invoke-WebRequest -Uri $script:url -Method Post -UseBasicParsing -OutFile $fichier `
        -Headers @{
            "Ocp-Apim-Subscription-Key" = $script:cle
            "X-Microsoft-OutputFormat"  = $format
            "User-Agent"                = "TrimBox"
        } `
        -ContentType "application/ssml+xml; charset=utf-8" `
        -Body ([Text.Encoding]::UTF8.GetBytes($ssml)) | Out-Null
    $taille = (Get-Item $fichier).Length
    if ($taille -lt 2000) { throw "fichier trop petit ($taille octets)" }
}

function Ecoute([string]$fichier) {
    try {
        $lecteur = New-Object System.Media.SoundPlayer $fichier
        $lecteur.PlaySync()
        $lecteur.Dispose()
    } catch {
        Write-Host "  (lecture impossible sur ce PC : ouvrez le fichier pour l'écouter)" -ForegroundColor DarkGray
    }
}

# ------------------------------------------------------------------- clé Azure
Write-Host ""
Write-Host "TrimBox - générer des mots à la demande (voix Azure $voix)" -ForegroundColor Cyan
Write-Host ""

$cle = ""; $region = ""
if (Test-Path $fichierCle) {
    $lignes = @(Get-Content $fichierCle -Encoding UTF8 | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($lignes.Count -ge 2) {
        Write-Host "Clé mémorisée trouvée (région $($lignes[1]))."
        if (Demande-ON "L'utiliser ?" $true) { $cle = $lignes[0]; $region = $lignes[1].ToLower() }
    }
}
if (-not $cle) {
    $cle = (Read-Host "Clé de la ressource Speech Azure (KEY 1)").Trim()
    $region = (Read-Host "Région de la ressource (ex. francecentral, westeurope)").Trim().ToLower()
    if (-not $cle -or -not $region) { throw "Clé et région obligatoires." }
    if (Demande-ON "Mémoriser la clé sur ce PC (fichier azure-cle.txt à côté du script) ?" $false) {
        Set-Content -Path $fichierCle -Value @($cle, $region) -Encoding UTF8
        Write-Host "  Clé mémorisée. Ne partagez pas ce fichier ; supprimez-le pour l'oublier." -ForegroundColor DarkGray
    }
}
$script:cle = $cle
$script:url = if ($env:TRIMBOX_TTS_URL) { $env:TRIMBOX_TTS_URL } else { "https://$region.tts.speech.microsoft.com/cognitiveservices/v1" }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ------------------------------------------------------------- dossier de sortie
$sortieDefaut = Join-Path $PSScriptRoot "SOUNDS\trimbox"
Write-Host ""
$s = (Read-Host "Dossier de destination [Entrée = $sortieDefaut]").Trim().Trim('"')
$sortie = if ($s) { $s } else { $sortieDefaut }
New-Item -ItemType Directory -Force -Path $sortie | Out-Null

# --------------------------------------------------------------------- boucle
$crees = @()
while ($true) {
    Write-Host ""
    $texte = (Read-Host "Texte à prononcer (Entrée seule pour terminer)").Trim()
    if (-not $texte) { break }

    $propose = Nom-Propose $texte
    $nom = (Read-Host "Nom du fichier, sans .wav [Entrée = $propose]").Trim().ToLower()
    if (-not $nom) { $nom = $propose }
    $nom = $nom -replace '\.wav$', ''
    if ($nom -notmatch "^[a-z0-9_-]{1,$NOM_MAX}$") {
        Write-Host "  Nom refusé : $NOM_MAX caractères au plus, lettres minuscules sans accent, chiffres, - ou _." -ForegroundColor Yellow
        continue
    }
    $fichier = Join-Path $sortie "$nom.wav"
    if ((Test-Path $fichier) -and -not (Demande-ON "  $nom.wav existe déjà. Le remplacer ?" $false)) { continue }

    $tmp = Join-Path $env:TEMP "trimbox-$nom-$PID.wav"
    $vitesse = 0
    $garde = $false
    while ($true) {
        try {
            Synthese $texte $vitesse $tmp
        } catch {
            Write-Host "  ÉCHEC : $($_.Exception.Message)" -ForegroundColor Red
            if ($_.Exception.Message -match "401|403") {
                Write-Host "  -> clé ou région incorrecte (la région doit être celle de la ressource)." -ForegroundColor Yellow
                if (Test-Path $fichierCle) { Write-Host "  -> si la clé mémorisée a changé, supprimez azure-cle.txt." -ForegroundColor Yellow }
            }
            break
        }
        $v = if ($vitesse -eq 0) { "vitesse normale" } elseif ($vitesse -gt 0) { "+$vitesse %" } else { "$vitesse %" }
        Write-Host ("  « {0} » → {1}.wav ({2})" -f $texte, $nom, $v) -ForegroundColor Green
        Ecoute $tmp
        $c = (Read-Host "  Entrée = garder | e = réécouter | t = changer le texte | l = plus lent | r = plus rapide | a = abandonner").Trim().ToLower()
        if (-not $c) { $garde = $true; break }
        switch ($c) {
            "e" { Ecoute $tmp; $c2 = (Read-Host "  Entrée = garder, autre touche = revenir au choix").Trim(); if (-not $c2) { $garde = $true } }
            "t" { $n = (Read-Host "  Nouveau texte (une autre orthographe change souvent la prononciation)").Trim(); if ($n) { $texte = $n } }
            "l" { $vitesse = [Math]::Max(-50, $vitesse - 10) }
            "r" { $vitesse = [Math]::Min(50, $vitesse + 10) }
            "a" { break }
        }
        if ($garde -or $c -eq "a") { break }
    }
    if ($garde) {
        Move-Item -Force $tmp $fichier
        $crees += "$nom.wav  « $texte »"
        Write-Host "  Enregistré : $fichier" -ForegroundColor Cyan
    } elseif (Test-Path $tmp) {
        Remove-Item $tmp
    }
}

# --------------------------------------------------------------------- bilan
Write-Host ""
if ($crees.Count) {
    Write-Host "Fichiers créés dans $sortie :" -ForegroundColor Cyan
    $crees | ForEach-Object { Write-Host "  $_" }
    Write-Host ""
    Write-Host "Copiez le dossier SOUNDS à la racine de la carte SD de la radio."
    Write-Host "Le script de la radio ne joue que les mots qu'il connaît : un mot nouveau"
    Write-Host "doit aussi être appelé dans lua/trmbox.lua (ou par une fonction spéciale EdgeTX)."
} else {
    Write-Host "Aucun fichier créé."
}
Write-Host ""
Read-Host "Appuie sur Entrée pour fermer"
