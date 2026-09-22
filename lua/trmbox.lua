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
--  Commandes :  molette = page suivante/précédente
--               ENT court = page suivante (page Lignes : action suivante)
--               ENT long  = page Lignes : exécuter l'action choisie
--                           page Chrono : remettre l'affichage à zéro
-- ===========================================================================

local GV_INDEX = 8          -- GV9 (numérotée à partir de 0)
local POSE_TICKS = 60       -- 0,6 s (le module demande 0,5 s)
local CLEAR_TICKS = 320     -- 3,2 s (le module demande 3 s)
local STALE_TICKS = 300     -- 3 s sans message : télémétrie perdue

local page = 1
local sel = 1               -- action choisie sur la page Lignes
local ACTIONS = { "Poser DEPART", "Poser ARRIVEE", "Effacer lignes" }

local st = {
  rec = "--", lines = "-", wifi = false,
  lapN = 0, last = nil, best = nil, delta = nil, laps = {},
  split = nil, banner = nil, bannerUntil = 0,
  lastMsg = nil, lastMsgAt = -100000, vmax = 0,
}
local pulse = { value = 0, untilT = 0 }

-- ---------------------------------------------------------------- outils
local function now() return getTime() end

local function banner(text, ticks)
  st.banner = text
  st.bannerUntil = now() + (ticks or 300)
end

local function setGV(v)
  if model and model.setGlobalVariable then model.setGlobalVariable(GV_INDEX, 0, v) end
end

local function startPulse(v, ticks)
  pulse.value = v
  pulse.untilT = now() + ticks
  setGV(v)
end

local function servicePulse()
  if pulse.value ~= 0 and now() >= pulse.untilT then
    pulse.value = 0
    setGV(0)
  end
end

local function fmtTime(t)
  if not t then return "--.---" end
  return string.format("%.3f", t)
end

local function tone(ok)
  if ok then playTone(1800, 80, 40, 0) playTone(2400, 80, 0, 0)
  else playTone(400, 250, 0, 0) end
end

-- ---------------------------------------------------------------- messages
local function onLap(n, t, d)
  local newBest = (st.best == nil) or (t < st.best)
  st.lapN, st.last, st.delta = n, t, d
  if newBest then st.best = t end
  table.insert(st.laps, 1, { n = n, t = t })
  if #st.laps > 4 then table.remove(st.laps) end
  if newBest then playTone(2000, 60, 30, 0) playTone(2600, 60, 30, 0) end
  playNumber(math.floor(t * 100 + 0.5), 0, PREC2)
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
    if txt == "DEPART OK" then st.lines = (st.lines == "D") and "D" or "C" tone(true)
    elseif txt == "ARRIVEE OK" then st.lines = "D" tone(true)
    elseif txt == "EFFACE" then st.lines = "-" tone(true)
    else tone(false) end
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
        local chars = {}
        for i = 1, #data do
          if data[i] == 0 then break end
          chars[#chars + 1] = string.char(data[i])
        end
        receive(table.concat(chars))
      end
    end
  end
  local v = getValue("GSpd")
  if type(v) == "number" and v > st.vmax then st.vmax = v end
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
  sensor("Vitesse", "GSpd", "%.1f", 1, 12)
  lcd.drawText(66, 12, "Vmax", SMLSIZE)
  lcd.drawText(LCD_W - 1, 12, string.format("%.1f", st.vmax), SMLSIZE + RIGHT)
  sensor("Satell.", "Sats", "%d", 1, 22)
  sensor("Liaison", "RQly", "%d%%", 66, 22)
  sensor("RSSI", "1RSS", "%d", 1, 32)
  sensor("Alt.", "Alt", "%.0f", 66, 32)
  lcd.drawText(1, 44, "ESC : a venir", SMLSIZE)
  footer()
end

local function pageLignes()
  header("LIGNES")
  local mode = (st.lines == "D") and "dragster" or (st.lines == "C") and "circuit" or "aucune"
  lcd.drawText(1, 12, "Mode : " .. mode, SMLSIZE)
  for i, a in ipairs(ACTIONS) do
    lcd.drawText(4, 13 + i * 9, a, SMLSIZE + (i == sel and INVERS or 0))
  end
  if pulse.value ~= 0 then
    lcd.drawText(LCD_W - 1, 12, "ENVOI", SMLSIZE + RIGHT + BLINK)
  end
  lcd.drawText(1, 50, "ENT: choisir  ENT long: go", SMLSIZE)
  footer()
end

local function execute()
  if pulse.value ~= 0 then return end
  if sel == 1 then startPulse(100, POSE_TICKS) banner("Depart : roulez > 7 km/h", 200)
  elseif sel == 2 then startPulse(-100, POSE_TICKS) banner("Arrivee envoyee", 200)
  else startPulse(-100, CLEAR_TICKS) banner("Effacement (3 s)...", 350) end
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

local function run(event)
  poll()
  if event == EVT_VIRTUAL_NEXT or event == EVT_VIRTUAL_NEXT_PAGE then
    page = page % NPAGES + 1
  elseif event == EVT_VIRTUAL_PREV or event == EVT_VIRTUAL_PREV_PAGE then
    page = (page + NPAGES - 2) % NPAGES + 1
  elseif event == EVT_VIRTUAL_ENTER then
    if page == 3 then sel = sel % #ACTIONS + 1 else page = page % NPAGES + 1 end
  elseif event == EVT_VIRTUAL_ENTER_LONG then
    if page == 3 then execute()
    elseif page == 1 then st.lapN, st.last, st.best, st.delta, st.laps = 0, nil, nil, nil, {} end
  end
  lcd.clear()
  if page == 1 then pageChrono() elseif page == 2 then pageMachine() else pageLignes() end
  return 0
end

return { init = init, background = background, run = run }
