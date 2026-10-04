-- ===========================================================================
--  TrimBox DIY — script de télémétrie EdgeTX pour RadioMaster MT12 (128×64)
--  À copier dans  SCRIPTS/TELEMETRY/trmbox.lua  (6 caractères au plus : limite
--  d'EdgeTX pour les scripts de télémétrie des écrans monochromes), puis
--  Modèle → Télémétrie → Écran 1 → Script → trmbox.
--
--  Le module fait tout le calcul (chrono à 25 Hz) ; ce script AFFICHE,
--  ANNONCE et COMMANDE. Messages reçus dans le capteur « FM » (trame CRSF
--  mode de vol), un préfixe par nature (cahier des charges v2 §4.4) :
--    S <état> <lignes>   état : REC / PAUSE / STOP / NOFIX ; lignes C, D ou -
--    L<n> <temps>[±écart] tour (ou parcours) n, écart au meilleur précédent
--    R <chrono> <temps>  chrono intermédiaire (dragster)
--    K <texte>           accusé de pose de ligne
--    W WIFI ON|OFF       point d'accès de la console
--    U <texte>           mise à jour du firmware
--  Pose de ligne : le script écrit la variable globale GV9, reprise par un
--  mixage sur CH8 (source MAX, poids GV9, en « addition »).
--
--  Commandes :  molette = page suivante/précédente ; sur la page Lignes,
--                           choix de l'action (au-delà des bouts : page)
--               ENT court = page suivante ; page Lignes : exécuter l'action
--                           (« Effacer lignes » : second appui pour confirmer)
--               ENT long  = page Chrono : remettre l'affichage à zéro
-- ===========================================================================

local GV_INDEX = 8          -- GV9 (numérotée à partir de 0)
local LINE_CHANNEL = 8      -- voie qui porte la commande (mixage sur CH8)
local POSE_TICKS = 60       -- 0,6 s (le module demande 0,5 s)
local ARM_TICKS = 1000      -- 10 s pour démarrer après une pose à l'arrêt
local CONFIRM_TICKS = 300   -- 3 s pour confirmer l'effacement
local CLEAR_TICKS = 320     -- 3,2 s (le module demande 3 s)
local STALE_TICKS = 300     -- 3 s sans message : télémétrie perdue
local SPEED_MIN = 3         -- km/h : en dessous, le GPS ne mesure que son bruit
-- Tension de la batterie : noms de capteurs essayés dans l'ordre. Le premier
-- qui renvoie une valeur est gardé. « Batt » = capteur personnalisé du
-- récepteur ; les suivants couvrent les réglages EdgeTX habituels.
local BATT_NAMES = { "Batt", "RxBt", "VFAS", "A1" }
-- Date et heure GPS : capteur « Date » créé par EdgeTX à partir de la trame
-- CRSF 0x03 (ExpressLRS ≥ 4.1). Le GPS donne l'heure UTC ; on la convertit
-- en heure locale : décalage d'hiver + heure d'été européenne.
-- Annonces vocales. À chaque tour : « meilleur tour » s'il y a lieu, le
-- temps, puis l'écart avec le tour PRÉCÉDENT (« plus 0,35 » / « moins
-- 0,12 » / « égal »). Pose de ligne : « départ posé », « vitesse trop
-- faible »… Les nombres sont dits par le pack vocal de la radio ; les mots
-- viennent de SOUND_DIR (plus, moins, egal, meilleur, depart, arrivee,
-- efface, arme, delai, vitfaib, pasgps, refus .wav), à produire soi-même : liste et
-- textes dans tools/sons/liste-sons-mt12.csv. Sans eux : bips équivalents.
local ANNOUNCE_PREV = true
-- Dossiers essayés dans l'ordre (le premier qui contient plus.wav gagne) :
-- tolère une faute de frappe courante et le rangement sous SOUNDS/fr/.
local SOUND_DIRS = { "/SOUNDS/trimbox/", "/SOUNDS/trmbox/", "/SOUNDS/fr/trimbox/", "/SOUNDS/TRIMBOX/" }
local SOUND_DIR = SOUND_DIRS[1]
local DATE_SENSOR = "Date"
local TZ_OFFSET = 1         -- France : UTC+1 en hiver
local TZ_EU_DST = true      -- heure d'été européenne (dernier dimanche de mars → d'octobre)

local page = 1
local sel = 1               -- action choisie sur la page Lignes
local confirmUntil = 0      -- effacement : second appui attendu avant cette date
local ACTIONS = { "Poser DEPART", "Poser ARRIVEE", "Effacer lignes" }

local st = {
  rec = "--", lines = "-", wifi = false,
  lapN = 0, last = nil, best = nil, delta = nil, laps = {},
  split = nil, banner = nil, bannerUntil = 0,
  lastMsg = nil, lastMsgAt = -100000, vmax = 0,
}
local pulse = { value = 0, untilT = 0, seen = 0 }

-- ---------------------------------------------------------------- outils
local function now() return getTime() end

-- Vitesse filtrée : à l'arrêt, le GPS « promène » sa position et annonce
-- 1 à 2 km/h. En dessous de SPEED_MIN, la voiture est à l'arrêt.
local function speed()
  local v = getValue("GSpd")
  if type(v) ~= "number" or v < SPEED_MIN then return 0 end
  return v
end

-- Tension de la batterie, quel que soit le nom donné au capteur.
local battName = nil
local function battery()
  if battName then
    local v = getValue(battName)
    if type(v) == "number" and v > 0 then return v end
    battName = nil
  end
  for i = 1, #BATT_NAMES do
    local n = BATT_NAMES[i]
    local v = getValue(n)
    if type(v) == "number" and v > 0 then battName = n return v end
  end
  return nil
end


-- ---- date et heure
local MDAYS = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
local function mdays(y, m)
  if m == 2 and ((y % 4 == 0 and y % 100 ~= 0) or y % 400 == 0) then return 29 end
  return MDAYS[m]
end
local function dow(y, m, d)                  -- 0 = dimanche (méthode de Sakamoto)
  local t = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 }
  if m < 3 then y = y - 1 end
  return (y + math.floor(y / 4) - math.floor(y / 100) + math.floor(y / 400) + t[m] + d) % 7
end
local function lastSunday(y, m)
  local d = mdays(y, m)
  while dow(y, m, d) ~= 0 do d = d - 1 end
  return d
end
-- Heure d'été européenne, instant donné en UTC : du dernier dimanche de mars
-- 01:00 UTC au dernier dimanche d'octobre 01:00 UTC.
local function euSummer(y, m, d, h)
  if m < 3 or m > 10 then return false end
  if m > 3 and m < 10 then return true end
  local ls = lastSunday(y, m)
  if m == 3 then return d > ls or (d == ls and h >= 1) end
  return d < ls or (d == ls and h < 1)
end

-- Date et heure GPS converties en heure locale, ou nil sans capteur valide.
local function gpsLocalTime()
  local v = getValue(DATE_SENSOR)
  if type(v) ~= "table" then return nil end
  local y, m, d = v.year, v.mon or v.month, v.day
  local h, mi, se = v.hour, v.min, v.sec
  if not (y and m and d and h and mi and se) or y < 2020 or m < 1 or m > 12 then return nil end
  h = h + TZ_OFFSET + ((TZ_EU_DST and euSummer(y, m, d, h)) and 1 or 0)
  if h >= 24 then
    h = h - 24; d = d + 1
    if d > mdays(y, m) then d = 1; m = m + 1; if m > 12 then m = 1; y = y + 1 end end
  elseif h < 0 then
    h = h + 24; d = d - 1
    if d < 1 then m = m - 1; if m < 1 then m = 12; y = y - 1 end; d = mdays(y, m) end
  end
  return { year = y, mon = m, day = d, hour = h, min = mi, sec = se }
end

local function banner(text, ticks)
  st.banner = text
  st.bannerUntil = now() + (ticks or 300)
end

-- La variable globale est écrite pour TOUS les modes de vol : le mixage lit
-- celle du mode actif, qui n'est pas forcément le mode 0.
local function setGV(v)
  if model and model.setGlobalVariable then
    for fm = 0, 8 do model.setGlobalVariable(GV_INDEX, fm, v) end
  end
end

-- Valeur réellement sortie sur la voie de commande, en pourcentage.
-- C'est elle qui part vers le module : si elle ne bouge pas pendant un envoi,
-- le mixage CH8 est absent ou mal réglé.
local function chPercent()
  local v = getValue("ch" .. LINE_CHANNEL)
  if type(v) ~= "number" then return nil end
  return v / 10.24
end

local function startPulse(v, ticks)
  pulse.value = v
  pulse.untilT = now() + ticks
  pulse.seen = 0                     -- écart maximal vu sur la voie
  setGV(v)
end

local function servicePulse()
  if pulse.value == 0 then return end
  local c = chPercent()
  if c and math.abs(c) > pulse.seen then pulse.seen = math.abs(c) end
  if now() >= pulse.untilT then
    pulse.value = 0
    setGV(0)
    -- La voie n'a pas bougé : la commande n'est jamais partie. C'est le
    -- mixage CH8 qui manque (Modèle → Mixeur → CH8 : MAX, poids GV9).
    if pulse.seen < 20 then
      st.banner = "CH" .. LINE_CHANNEL .. " immobile : mixage ?"
      st.bannerUntil = now() + 500
      playTone(400, 250, 0, 0)
    end
  end
end

local function fmtTime(t)
  if not t then return "--.---" end
  return string.format("%.3f", t)
end

-- ---------------------------------------------------------------- messages
-- Fichiers de voix présents sur la carte SD ? (fstat n'existe pas sur les
-- vieilles versions d'EdgeTX : on suppose alors qu'ils sont là.)
-- Sans eux, chaque mot est remplacé par un bip équivalent.
local wordsOk = nil
local FALLBACK = {
  moins = function() playTone(2200, 120, 20, 0) end,            -- plus rapide : aigu
  plus = function() playTone(500, 200, 20, 0) end,              -- plus lent : grave
  meilleur = function() playTone(2000, 60, 30, 0) playTone(2600, 60, 30, 0) end,
  ok = function() playTone(1800, 80, 40, 0) playTone(2400, 80, 0, 0) end,
  ko = function() playTone(400, 250, 0, 0) end,
}
local WORD_FALLBACK = { depart = "ok", arrivee = "ok", efface = "ok", arme = "ok",
                        vitfaib = "ko", pasgps = "ko", refus = "ko", delai = "ko" }
local function word(name)
  if wordsOk == nil then
    if fstat == nil then
      wordsOk = true                       -- impossible de vérifier : on essaie
    else
      wordsOk = false
      for i = 1, #SOUND_DIRS do
        local d = SOUND_DIRS[i]
        if fstat(d .. "plus.wav") ~= nil then SOUND_DIR = d wordsOk = true break end
      end
    end
  end
  if wordsOk then
    playFile(SOUND_DIR .. name .. ".wav")
  else
    local f = FALLBACK[name] or FALLBACK[WORD_FALLBACK[name] or ""]
    if f then f() end
  end
end

local function onLap(n, t, d)
  local prev = st.laps[1]
  local newBest = (st.best == nil) or (t < st.best)
  st.lapN, st.last, st.delta = n, t, d
  if newBest then st.best = t end
  -- Décalage manuel : la bibliotheque « table » n'existe pas dans le bac a
  -- sable Lua d'EdgeTX (script de telemetrie) — tout en Lua de base.
  local nb = #st.laps
  if nb > 3 then nb = 3 end
  for i = nb, 1, -1 do st.laps[i + 1] = st.laps[i] end
  st.laps[1] = { n = n, t = t }
  if newBest then word("meilleur") end
  playNumber(math.floor(t * 100 + 0.5), 0, PREC2)
  -- écart avec le tour précédent, s'il s'agit bien du tour juste avant
  if ANNOUNCE_PREV and prev and n == prev.n + 1 then
    local diff = t - prev.t
    local c = math.floor(math.abs(diff) * 100 + 0.5)        -- centièmes
    if c == 0 then
      word("egal")
    else
      word(diff > 0 and "plus" or "moins")
      playNumber(c, 0, PREC2)
    end
  end
end

local function handle(msg)
  local p = string.sub(msg, 1, 1)
  if p == "S" then
    local s, l = string.match(msg, "^S (%u+) ?([CD%-]?)")
    if s then st.rec = s end
    if l and l ~= "" then st.lines = l end
  elseif p == "L" then
    local n, t, d = string.match(msg, "^L(%d+) (%d+%.%d+)([%+%-]?%d*%.?%d*)")
    if n then onLap(tonumber(n), tonumber(t), tonumber(d)) end
  elseif p == "R" then
    st.split = string.sub(msg, 3)
    banner("Chrono " .. st.split, 400)
  elseif p == "K" then
    local txt = string.sub(msg, 3)
    if txt == "DEPART OK" then st.lines = (st.lines == "D") and "D" or "C" word("depart") st.armUntil = nil
    elseif txt == "ARRIVEE OK" then st.lines = "D" word("arrivee") st.armUntil = nil
    elseif txt == "ARME DEPART" or txt == "ARME ARRIVEE" then
      -- voiture à l'arrêt sur la ligne : la ligne sera posée au démarrage
      st.armUntil = now() + ARM_TICKS
      word("arme")
      txt = (txt == "ARME DEPART" and "DEPART" or "ARRIVEE") .. " ARME : ROULEZ"
    elseif txt == "DELAI" then word("delai") st.armUntil = nil txt = "DELAI DEPASSE"
    elseif txt == "EFFACE" then st.lines = "-" word("efface")
    elseif txt == "VIT FAIBLE" then word("vitfaib")
    elseif txt == "PAS DE FIX" then word("pasgps")
    else word("refus") end
    banner(txt, 300)
  elseif p == "W" then
    st.wifi = (msg == "W WIFI ON")
    banner(st.wifi and "Wi-Fi console ON" or "Wi-Fi coupe", 200)
  elseif p == "U" then
    banner("MAJ : " .. string.sub(msg, 3), 500)
  end
end

-- Un même message arrive plusieurs fois (le module le répète pendant 0,7 s) :
-- on ne le traite qu'à son changement.
local function receive(msg)
  if type(msg) ~= "string" or msg == "" then return end
  st.lastMsgAt = now()
  if msg == st.lastMsg then return end
  st.lastMsg = msg
  handle(msg)
end

local function poll()
  -- 1. capteur « FM » (découvert par EdgeTX)
  receive(getValue("FM"))
  -- 2. file brute CRSF, si la radio y dépose aussi les trames « mode de vol »
  if crossfireTelemetryPop then
    for _ = 1, 8 do
      local cmd, data = crossfireTelemetryPop()
      if cmd == nil then break end
      if cmd == 0x21 and data then
        local txt = ""
        for i = 1, #data do
          if data[i] == 0 then break end
          txt = txt .. string.char(data[i])
        end
        receive(txt)
      end
    end
  end
  local v = speed()
  if v > st.vmax then st.vmax = v end
  servicePulse()
end

-- ---------------------------------------------------------------- affichage
local function header(title)
  lcd.drawFilledRectangle(0, 0, LCD_W, 9, 0)
  lcd.drawText(1, 1, title, SMLSIZE + INVERS)
  local stale = now() - st.lastMsgAt > STALE_TICKS
  local right = stale and "PAS DE LIAISON" or (st.rec .. " " .. st.lines .. (st.wifi and " W" or ""))
  lcd.drawText(LCD_W - 1, 1, right, SMLSIZE + INVERS + RIGHT)
end

local function footer()
  if st.banner and now() < st.bannerUntil then
    lcd.drawFilledRectangle(0, LCD_H - 9, LCD_W, 9, 0)
    lcd.drawText(1, LCD_H - 8, st.banner, SMLSIZE + INVERS)
  end
end

local function pageChrono()
  header("CHRONO")
  lcd.drawText(1, 12, "Tour " .. st.lapN, SMLSIZE)
  lcd.drawText(1, 21, fmtTime(st.last), DBLSIZE)
  if st.delta then
    lcd.drawText(LCD_W - 1, 12, string.format("%+.2f", st.delta), SMLSIZE + RIGHT + (st.delta < 0 and INVERS or 0))
  end
  lcd.drawText(LCD_W - 1, 24, "Meilleur", SMLSIZE + RIGHT)
  lcd.drawText(LCD_W - 1, 32, fmtTime(st.best), SMLSIZE + RIGHT)
  local y = 42
  for i = 2, #st.laps do
    local l = st.laps[i]
    lcd.drawText(1 + (i - 2) * 43, y, string.format("%d:%.2f", l.n, l.t), SMLSIZE)
  end
  if st.lines == "-" then lcd.drawText(1, 52, "Aucune ligne : page 3", SMLSIZE) end
  footer()
end

local function sensor(label, name, fmt, x, y)
  local v = getValue(name)
  local txt = (type(v) == "number") and string.format(fmt, v) or "--"
  lcd.drawText(x, y, label, SMLSIZE)
  lcd.drawText(x + 62, y, txt, SMLSIZE + RIGHT)
end

local function pageMachine()
  header("MACHINE")
  lcd.drawText(1, 12, "Vitesse", SMLSIZE)
  lcd.drawText(63, 12, string.format("%.1f", speed()), SMLSIZE + RIGHT)
  lcd.drawText(66, 12, "Vmax", SMLSIZE)
  lcd.drawText(LCD_W - 1, 12, string.format("%.1f", st.vmax), SMLSIZE + RIGHT)
  sensor("Satell.", "Sats", "%d", 1, 22)
  sensor("Liaison", "RQly", "%d%%", 66, 22)
  sensor("RSSI", "1RSS", "%d", 1, 32)
  sensor("Alt.", "Alt", "%.0f", 66, 32)
  -- batterie de propulsion, remontée par le récepteur
  local bat = battery()
  lcd.drawText(1, 44, "Batterie", SMLSIZE)
  if bat then
    lcd.drawText(75, 44, string.format("%.1f V", bat), SMLSIZE + RIGHT)
    local cells = math.floor(bat / 3.8 + 0.5)
    if cells >= 1 and cells <= 8 then
      lcd.drawText(LCD_W - 1, 44, string.format("%.2f V/el", bat / cells), SMLSIZE + RIGHT)
    end
  else
    lcd.drawText(75, 44, "--", SMLSIZE + RIGHT)
  end
  -- date et heure du GPS, en heure locale
  local t = gpsLocalTime()
  lcd.drawText(1, 54, "Heure", SMLSIZE)
  lcd.drawText(LCD_W - 1, 54, t and string.format("%02d/%02d %02d:%02d:%02d", t.day, t.mon, t.hour, t.min, t.sec) or "--",
               SMLSIZE + RIGHT)
  footer()
end

local function pageLignes()
  header("LIGNES")
  local mode = (st.lines == "D") and "dragster" or (st.lines == "C") and "circuit" or "aucune"
  lcd.drawText(1, 12, "Mode : " .. mode, SMLSIZE)
  for i = 1, #ACTIONS do
    local a = ACTIONS[i]
    local txt = a
    if i == 3 and now() < confirmUntil then txt = "ENT pour CONFIRMER" end
    lcd.drawText(4, 12 + i * 9, txt, SMLSIZE + (i == sel and INVERS or 0) + ((i == 3 and now() < confirmUntil) and BLINK or 0))
  end
  local c = chPercent()
  lcd.drawText(1, 49, "CH" .. LINE_CHANNEL .. " " .. (c and string.format("%+d%%", math.floor(c + (c < 0 and -0.5 or 0.5))) or "--"), SMLSIZE)
  if pulse.value ~= 0 then
    lcd.drawText(LCD_W - 1, 12, "ENVOI", SMLSIZE + RIGHT + BLINK)
  elseif st.armUntil and now() < st.armUntil then
    local left = math.ceil((st.armUntil - now()) / 100)
    lcd.drawText(LCD_W - 1, 12, "ROULEZ " .. left .. "s", SMLSIZE + RIGHT + BLINK)
  end
  if st.banner == nil or now() >= st.bannerUntil then
    lcd.drawText(LCD_W - 1, 49, "ENT: go", SMLSIZE + RIGHT)
  end
  footer()
end

-- Voiture ARRÊTÉE sur la ligne : le module arme la ligne et la pose au
-- démarrage (dans les 10 s), dans le sens du départ. Voiture lancée
-- (> 7 km/h) : pose immédiate, comme avant.
local function execute()
  if pulse.value ~= 0 then return end
  if sel == 1 then startPulse(100, POSE_TICKS) banner("Depart : envoi...", 150)
  elseif sel == 2 then startPulse(-100, POSE_TICKS) banner("Arrivee : envoi...", 150)
  elseif now() < confirmUntil then
    confirmUntil = 0
    startPulse(-100, CLEAR_TICKS) banner("Effacement (3 s)...", 350)
  else
    confirmUntil = now() + CONFIRM_TICKS                  -- effacer : on confirme
    return
  end
  playHaptic(30, 0)
end

-- ---------------------------------------------------------------- EdgeTX
local NPAGES = 3

local function init()
  setGV(0)
end

local function background()
  poll()
end

local function nextPage() page = page % NPAGES + 1 end
local function prevPage() page = (page + NPAGES - 2) % NPAGES + 1 end

local function run(event)
  poll()
  local wheelNext = event == EVT_VIRTUAL_NEXT
  local wheelPrev = event == EVT_VIRTUAL_PREV
  if page == 3 and (wheelNext or wheelPrev) then
    -- Page Lignes : la molette choisit l'action ; au-delà des bouts, elle
    -- change de page comme ailleurs.
    confirmUntil = 0
    if wheelNext then
      if sel < #ACTIONS then sel = sel + 1 else sel = 1 nextPage() end
    else
      if sel > 1 then sel = sel - 1 else prevPage() end
    end
  elseif wheelNext or event == EVT_VIRTUAL_NEXT_PAGE then
    nextPage()
    if page == 3 then sel = 1 end
  elseif wheelPrev or event == EVT_VIRTUAL_PREV_PAGE then
    prevPage()
    if page == 3 then sel = #ACTIONS end
  elseif event == EVT_VIRTUAL_ENTER then
    if page == 3 then execute() else nextPage() if page == 3 then sel = 1 end end
  elseif event == EVT_VIRTUAL_ENTER_LONG then
    if page == 3 then execute()
    elseif page == 1 then st.lapN, st.last, st.best, st.delta, st.laps = 0, nil, nil, nil, {} end
  end
  lcd.clear()
  if page == 1 then pageChrono() elseif page == 2 then pageMachine() else pageLignes() end
  return 0
end

return { init = init, background = background, run = run }
