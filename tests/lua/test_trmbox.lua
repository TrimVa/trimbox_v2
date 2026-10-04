-- ===========================================================================
--  Banc du script trmbox.lua, hors radio : l'API EdgeTX est simulée
--  (temps, capteurs, écran, sons, variables globales, touches).
--  Usage : lua5.2 tests/lua/test_trmbox.lua lua/trmbox.lua
-- ===========================================================================
local path = arg[1] or "lua/trmbox.lua"
local T = 0
local sensors, gv, sounds, screen = {}, {}, {}, {}
local fails, passes = 0, 0
local function check(c, msg)
  if c then passes = passes + 1 else fails = fails + 1 print("  ECHEC : " .. msg) end
end

-- ---- API EdgeTX simulée ----
getTime = function() return T end
getValue = function(n) return sensors[n] or 0 end
-- gv : écritures du mode de vol 0 (celles que comptent les tests)
-- gvAll : toutes, pour vérifier que les 9 modes sont couverts
local gvAll = {}
model = { setGlobalVariable = function(i, fm, v)
  gvAll[#gvAll + 1] = { t = T, i = i, fm = fm, v = v }
  if fm == 0 then gv[#gv + 1] = { t = T, i = i, fm = fm, v = v } end
end }
playTone = function(f, d) sounds[#sounds + 1] = "tone" .. f end
playNumber = function(v, u, a) sounds[#sounds + 1] = "num" .. v end
playHaptic = function() end
playFile = function(f) sounds[#sounds + 1] = "file:" .. f end
local files = { ["/SOUNDS/trimbox/plus.wav"] = true }
fstat = function(f) return files[f] and { size = 1 } or nil end
LCD_W, LCD_H = 128, 64
SMLSIZE, MIDSIZE, DBLSIZE, INVERS, RIGHT, BLINK, PREC2 = 0x100, 0x200, 0x400, 0x1, 0x2, 0x4, 0x20
EVT_VIRTUAL_NEXT, EVT_VIRTUAL_PREV, EVT_VIRTUAL_ENTER, EVT_VIRTUAL_ENTER_LONG = 101, 102, 103, 104
lcd = {
  clear = function() screen = {} end,
  drawText = function(x, y, s, f)
    check(type(s) == "string", "drawText reçoit une chaîne")
    local w = #s * (bit32.band(f or 0, DBLSIZE) ~= 0 and 11 or 5)
    local x0 = (bit32.band(f or 0, RIGHT) ~= 0) and (x - w) or x
    check(x0 >= -1 and x0 + w <= LCD_W + 6 and y >= 0 and y <= LCD_H - 6, "texte hors écran : '" .. s .. "' x=" .. x0 .. " y=" .. y)
    screen[#screen + 1] = s
  end,
  drawFilledRectangle = function() end, drawLine = function() end, drawRectangle = function() end,
}
local function shown(pat) for _, s in ipairs(screen) do if string.find(s, pat, 1, true) then return true end end return false end

-- file CRSF brute : la radio y depose parfois les trames « mode de vol »
local crsfQueue = {}
crossfireTelemetryPop = function()
  local f = crsfQueue[1]
  if f == nil then return nil end
  for i = 1, #crsfQueue - 1 do crsfQueue[i] = crsfQueue[i + 1] end
  crsfQueue[#crsfQueue] = nil
  return f[1], f[2]
end
local function pushCrsf(txt)
  local d = {}
  for i = 1, #txt do d[i] = string.byte(txt, i) end
  d[#d + 1] = 0
  crsfQueue[#crsfQueue + 1] = { 0x21, d }
end

-- La radio (bac a sable Lua d'EdgeTX, script de telemetrie) n'expose PAS la
-- bibliotheque « table » : le script doit tourner sans elle.
table = nil
local script = dofile(path)
script.init()
local function tick(n, ev) for _ = 1, n do T = T + 1 script.background() end if ev ~= false then script.run(ev or 0) end end
local function say(msg, repeats) sensors.FM = msg for _ = 1, (repeats or 5) do tick(15) end end

-- sans télémétrie
tick(10)
check(shown("PAS DE LIAISON"), "« pas de liaison » tant que rien n'arrive")

-- état + lignes
say("S REC C")
check(shown("REC C"), "état REC et lignes circuit dans l'en-tête")

-- tours : répétitions ignorées, meilleur, écart, annonces
sounds = {}
say("L1 21.345", 5)
check(#sounds == 2, "un seul traitement malgré 5 répétitions (sons : " .. #sounds .. ")")
check(sounds[1] == "file:/SOUNDS/trimbox/meilleur.wav" and sounds[2] == "num2135", "« meilleur tour » puis le temps : " .. tostring(sounds[2]))
check(shown("21.345"), "dernier tour affiché")
say("S REC C")
sounds = {}
say("L2 20.998-0.35")
check(sounds[1] == "file:/SOUNDS/trimbox/meilleur.wav" and sounds[2] == "num2100", "nouveau meilleur : annonce vocale puis temps")
check(sounds[3] == "file:/SOUNDS/trimbox/moins.wav" and sounds[4] == "num35",
      "ecart avec le tour precedent : moins 0,35 (" .. tostring(sounds[3]) .. " " .. tostring(sounds[4]) .. ")")
check(shown("-0.35") and shown("20.998"), "écart et meilleur affichés")
say("S REC C")
sounds = {}
say("L3 22.001+1.00")
check(#sounds == 3 and sounds[1] == "num2200", "tour plus lent : pas de « meilleur tour »")
check(sounds[2] == "file:/SOUNDS/trimbox/plus.wav" and sounds[3] == "num100", "ecart : plus 1,00")
check(shown("20.998"), "meilleur conservé")
check(shown("2:21.00") or shown("2:21.0"), "historique des tours")
-- liste des derniers tours : 4 gardés au plus, du plus récent au plus ancien
for i = 4, 8 do say("L" .. i .. " 2" .. i .. ".000") say("S REC C") end
check(shown("7:27.00") and shown("6:26.00") and shown("5:25.00"), "trois tours precedents affiches")
check(not shown("4:24.00"), "au-dela de 4 tours, le plus ancien est oublie")

-- tour sans écart (texte trop long côté module) et parcours dragster
sounds = {}
say("L100 123.456")
check(shown("123.456"), "tour sans écart accepté")
check(#sounds == 1, "tours non consecutifs : pas d'ecart annonce (" .. #sounds .. " sons)")
sounds = {}
say("L101 123.459")
check(sounds[2] == "file:/SOUNDS/trimbox/egal.wav" and #sounds == 2, "moins d'un demi-centieme : egal")
-- fichiers absents de la carte SD : un bip remplace le mot
files = {}
package.loaded = package.loaded
local s2 = dofile(path) s2.init()
sounds = {}
local function feed(m) sensors.FM = m for _ = 1, 3 do T = T + 15 s2.background() end end
feed("S REC C") feed("L1 20.000") feed("S REC C") feed("L2 19.500")
check(sounds[1] == "tone2000" and sounds[2] == "tone2600", "sans fichiers : double bip du meilleur tour")
check(sounds[#sounds - 1] == "tone2200" and sounds[#sounds] == "num50",
      "sans fichiers : bip aigu puis 0,50 (" .. tostring(sounds[#sounds - 1]) .. ")")

-- accusés et Wi-Fi
sounds = {}
say("K VIT FAIBLE")
check(shown("VIT FAIBLE"), "accusé d'erreur affiché")
check(sounds[1] == "file:/SOUNDS/trimbox/vitfaib.wav", "« vitesse trop faible » dit")
sounds = {} say("K DEPART OK")
check(sounds[1] == "file:/SOUNDS/trimbox/depart.wav", "« départ posé » dit")
sounds = {} say("K PAS DE FIX")
check(sounds[1] == "file:/SOUNDS/trimbox/pasgps.wav", "« pas de GPS » dit")
sounds = {} say("K ARRIVEE OK")
check(sounds[1] == "file:/SOUNDS/trimbox/arrivee.wav", "« arrivée posée » dit")
sounds = {} say("K EFFACE")
check(sounds[1] == "file:/SOUNDS/trimbox/efface.wav", "« lignes effacées » dit")
sounds = {} say("K ???")
check(sounds[1] == "file:/SOUNDS/trimbox/refus.wav", "accusé inconnu : « commande refusée »")
say("W WIFI ON")
check(shown(" W"), "indicateur Wi-Fi")
say("S STOP -")
check(shown("STOP -"), "état arrêté, aucune ligne")

-- pose de ligne : page 3, action 1, appui COURT → GV9 = 100 pendant 0,6 s
tick(1, EVT_VIRTUAL_NEXT) tick(1, EVT_VIRTUAL_NEXT)
check(shown("LIGNES"), "page Lignes")
gv = {}
tick(1, EVT_VIRTUAL_ENTER)
tick(80)
check(#gv == 2 and gv[1].i == 8 and gv[1].v == 100 and gv[2].v == 0, "ENT court : départ, GV9 100 puis 0")
do  -- la GV est écrite pour les 9 modes de vol
  local fms = {}
  for _, g in ipairs(gvAll) do if g.v == 100 then fms[g.fm] = true end end
  local n = 0; for _ in pairs(fms) do n = n + 1 end
  check(n == 9, "GV9 écrite pour les 9 modes de vol (" .. n .. ")")
end
check(gv[2] and (gv[2].t - gv[1].t) >= 50 and (gv[2].t - gv[1].t) <= 70, "impulsion de 0,5 à 0,7 s : " .. tostring(gv[2] and gv[2].t - gv[1].t))
-- arrivée : la molette choisit l'action
tick(1, EVT_VIRTUAL_NEXT)
check(shown("LIGNES"), "molette sur Lignes : on reste sur la page")
gv = {}
tick(1, EVT_VIRTUAL_ENTER) tick(80)
check(#gv == 2 and gv[1].v == -100, "arrivée : GV9 = -100")
-- effacement : confirmation par un second appui, puis 3,2 s
tick(1, EVT_VIRTUAL_NEXT)
gv = {}
tick(1, EVT_VIRTUAL_ENTER) tick(10)
check(#gv == 0 and shown("ENT pour CONFIRMER"), "effacement : confirmation demandée, rien n'est envoyé")
tick(1, EVT_VIRTUAL_ENTER) tick(400)
check(#gv == 2 and gv[1].v == -100 and (gv[2].t - gv[1].t) >= 300, "effacement confirmé : -100 pendant plus de 3 s")
-- confirmation expirée : rien
gv = {}
tick(1, EVT_VIRTUAL_ENTER) tick(350) tick(1, EVT_VIRTUAL_ENTER) tick(20)
check(#gv == 0, "confirmation expirée après 3 s : pas d'effacement")
tick(400)
-- pas de double commande pendant une impulsion
tick(1, EVT_VIRTUAL_PREV) tick(1, EVT_VIRTUAL_PREV)            -- retour sur « Poser DEPART »
gv = {}
tick(1, EVT_VIRTUAL_ENTER) tick(1, EVT_VIRTUAL_ENTER) tick(100)
check(#gv == 2, "une impulsion à la fois")
-- l'appui long reste accepté
gv = {}
tick(1, EVT_VIRTUAL_ENTER_LONG) tick(80)
check(#gv == 2 and gv[1].v == 100, "appui long toujours accepté")
-- la voie de commande ne bouge pas (mixage CH8 absent) : le script prévient
sensors.ch8 = 0                                   -- sel reste sur « Poser DEPART »
tick(1, EVT_VIRTUAL_ENTER) tick(80)
check(shown("immobile"), "voie immobile : mixage signalé")
tick(520)                                         -- bandeau expiré
-- voie qui suit la commande : pas d'avertissement
sensors.ch8 = 1024
tick(1, EVT_VIRTUAL_ENTER) tick(80)
check(not shown("immobile"), "voie qui bouge : pas d'avertissement")
check(shown("CH8 +100%"), "valeur de la voie affichée")
sensors.ch8 = 0
tick(520)

-- pose à l'arrêt : le module arme la ligne, compte à rebours de 10 s
sounds = {}
say("K ARME DEPART", 1)
check(shown("ROULEZ") and sounds[1] == "file:/SOUNDS/trimbox/arme.wav", "ligne armée : « roulez » affiché et dit")
say("K DEPART OK", 1)
check(not shown("ROULEZ"), "départ posé : fin du compte à rebours")
sounds = {}
say("K ARME ARRIVEE", 1) say("K DELAI", 1)
check(shown("DELAI DEPASSE") and sounds[2] == "file:/SOUNDS/trimbox/delai.wav", "délai dépassé annoncé")
-- dossier des voix mal nommé « trmbox » : trouvé quand même (instance neuve,
-- la recherche du dossier n'a lieu qu'au premier mot prononcé)
local s3 = dofile(path) s3.init()
files = { ["/SOUNDS/trmbox/plus.wav"] = true }
sounds = {} sensors.FM = "K DEPART OK"
T = T + 15 s3.background() s3.run(0)
check(sounds[1] == "file:/SOUNDS/trmbox/depart.wav", "dossier « trmbox » accepté")
files = { ["/SOUNDS/trimbox/plus.wav"] = true }
-- aucun dossier : bips de repli
local s4 = dofile(path) s4.init()
files = {}
sounds = {} T = T + 15 s4.background() s4.run(0)
check(sounds[1] == "tone1800", "sans fichiers : bip de repli")
files = { ["/SOUNDS/trimbox/plus.wav"] = true }
tick(500)
say("K EFFACE")
check(shown("aucune"), "lignes effacées affichées")

-- page machine avec capteurs
sensors.GSpd = 45.5 sensors.Sats = 14 sensors.RQly = 100
local guard = 0                                          -- Lignes → … → Machine
while not shown("MACHINE") and guard < 12 do tick(1, EVT_VIRTUAL_NEXT) guard = guard + 1 end
check(shown("MACHINE") and shown("45.5"), "page Machine, vitesse")
sensors.GSpd = 61.2 tick(5) sensors.GSpd = 30 tick(1, false) tick(1, 0)
check(shown("61.2"), "vitesse maximale retenue")

-- voiture à l'arrêt : le bruit du GPS ne doit pas s'afficher ni entrer en Vmax
sensors.GSpd = 2.4 tick(3) tick(1, 0)
check(shown("0.0") and not shown("2.4"), "vitesse sous 3 km/h ramenée à 0")
check(shown("61.2"), "Vmax inchangée par le bruit à l'arrêt")
sensors.GSpd = 3.2 tick(1, 0)
check(shown("3.2"), "au-dessus du seuil, la vitesse s'affiche")

-- batterie de propulsion remontée par le récepteur
check(not shown("ESC : a venir"), "ligne ESC supprimée")
sensors.Batt = 15.8 tick(1, 0)
check(shown("15.8 V"), "tension batterie affichée")
check(shown("3.95 V/el"), "tension par element (4S)")
sensors.Batt = nil sensors.RxBt = 7.6 tick(1, 0)
check(shown("7.6 V") and shown("3.80 V/el"), "autre nom de capteur reconnu (RxBt, 2S)")
sensors.RxBt = nil tick(1, 0)
check(shown("Batterie"), "libelle batterie toujours present sans capteur")

-- date et heure GPS (capteur « Date », trame CRSF 0x03) converties en heure locale
tick(1, 0)
check(shown("Heure") and shown("--"), "heure absente sans capteur Date")
local function heure(y, m, d, h, mi, se)
  sensors.Date = { year = y, mon = m, day = d, hour = h, min = mi, sec = se }
  tick(1, 0)
end
heure(2026, 9, 24, 12, 32, 5)
check(shown("24/09 14:32:05"), "ete : UTC+2")
heure(2026, 12, 31, 23, 30, 0)
check(shown("01/01 00:30:00"), "hiver : UTC+1, passage au nouvel an")
heure(2026, 3, 29, 0, 59, 0)
check(shown("29/03 01:59:00"), "29/03/2026 00:59 UTC : encore l'heure d'hiver")
heure(2026, 3, 29, 1, 0, 0)
check(shown("29/03 03:00:00"), "29/03/2026 01:00 UTC : passage a l'heure d'ete")
heure(2026, 10, 25, 0, 59, 0)
check(shown("25/10 02:59:00"), "25/10/2026 00:59 UTC : encore l'heure d'ete")
heure(2026, 10, 25, 1, 0, 0)
check(shown("25/10 02:00:00"), "25/10/2026 01:00 UTC : retour a l'heure d'hiver")
heure(2028, 2, 28, 23, 10, 0)
check(shown("29/02 00:10:00"), "annee bissextile : 28/02 → 29/02")
sensors.Date = { year = 2026, month = 7, day = 1, hour = 8, min = 0, sec = 0 } tick(1, 0)
check(shown("01/07 10:00:00"), "champ « month » accepte aussi")
sensors.Date = 0 tick(1, 0)
check(shown("Heure") and shown("--"), "capteur perdu : tirets")

-- messages recus par la file CRSF brute (et non par le capteur « FM »)
sensors.FM = 0
local g2 = 0
while not shown("CHRONO") and g2 < 12 do tick(1, EVT_VIRTUAL_NEXT) g2 = g2 + 1 end
pushCrsf("L42 18.250")
tick(2, 0)
check(shown("18.250"), "tour recu par la file CRSF brute")

-- télémétrie perdue
sensors.FM = 0
tick(400)
check(shown("PAS DE LIAISON"), "perte de télémétrie signalée")

print(string.format("%-12s %d OK, %d ECHEC(S)", "lua", passes, fails))
os.exit(fails == 0 and 0 or 1)
