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
model = { setGlobalVariable = function(i, fm, v) gv[#gv + 1] = { t = T, i = i, fm = fm, v = v } end }
playTone = function(f, d) sounds[#sounds + 1] = "tone" .. f end
playNumber = function(v, u, a) sounds[#sounds + 1] = "num" .. v end
playHaptic = function() end
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
check(#sounds == 3, "un seul traitement malgré 5 répétitions (sons : " .. #sounds .. ")")
check(sounds[3] == "num2135", "annonce du temps : " .. tostring(sounds[3]))
check(shown("21.345"), "dernier tour affiché")
say("S REC C")
sounds = {}
say("L2 20.998-0.35")
check(sounds[1] == "tone2000" and sounds[3] == "num2100", "nouveau meilleur : double bip puis annonce")
check(shown("-0.35") and shown("20.998"), "écart et meilleur affichés")
say("S REC C")
sounds = {}
say("L3 22.001+1.00")
check(#sounds == 1 and sounds[1] == "num2200", "tour plus lent : annonce seule")
check(shown("20.998"), "meilleur conservé")
check(shown("2:21.00") or shown("2:21.0"), "historique des tours")

-- tour sans écart (texte trop long côté module) et parcours dragster
say("L100 123.456")
check(shown("123.456"), "tour sans écart accepté")

-- accusés et Wi-Fi
say("K VIT FAIBLE")
check(shown("VIT FAIBLE"), "accusé d'erreur affiché")
say("W WIFI ON")
check(shown(" W"), "indicateur Wi-Fi")
say("S STOP -")
check(shown("STOP -"), "état arrêté, aucune ligne")

-- pose de ligne : page 3, action 1, appui long → GV9 = 100 pendant 0,6 s
tick(1, EVT_VIRTUAL_NEXT) tick(1, EVT_VIRTUAL_NEXT)
check(shown("LIGNES"), "page Lignes")
gv = {}
local t0 = T
tick(1, EVT_VIRTUAL_ENTER_LONG)
tick(80)
check(#gv == 2 and gv[1].i == 8 and gv[1].v == 100 and gv[2].v == 0, "GV9 : 100 puis 0")
check(gv[2] and (gv[2].t - gv[1].t) >= 50 and (gv[2].t - gv[1].t) <= 70, "impulsion de 0,5 à 0,7 s : " .. tostring(gv[2] and gv[2].t - gv[1].t))
-- arrivée
tick(1, EVT_VIRTUAL_ENTER)
gv = {}
tick(1, EVT_VIRTUAL_ENTER_LONG) tick(80)
check(#gv == 2 and gv[1].v == -100, "arrivée : GV9 = -100")
-- effacement : 3,2 s
tick(1, EVT_VIRTUAL_ENTER)
gv = {}
tick(1, EVT_VIRTUAL_ENTER_LONG) tick(400)
check(#gv == 2 and gv[1].v == -100 and (gv[2].t - gv[1].t) >= 300, "effacement : -100 pendant plus de 3 s")
-- pas de double commande pendant une impulsion
gv = {}
tick(1, EVT_VIRTUAL_ENTER_LONG) tick(1, EVT_VIRTUAL_ENTER_LONG) tick(400)
check(#gv == 2, "une impulsion à la fois")
say("K EFFACE")
check(shown("aucune"), "lignes effacées affichées")

-- page machine avec capteurs
sensors.GSpd = 45.5 sensors.Sats = 14 sensors.RQly = 100
tick(1, EVT_VIRTUAL_NEXT) tick(1, EVT_VIRTUAL_NEXT)      -- Lignes → Chrono → Machine
check(shown("MACHINE") and shown("45.5"), "page Machine, vitesse")
sensors.GSpd = 61.2 tick(5) sensors.GSpd = 30 tick(1, false) tick(1, 0)
check(shown("61.2"), "vitesse maximale retenue")

-- télémétrie perdue
sensors.FM = 0
tick(400)
check(shown("PAS DE LIAISON"), "perte de télémétrie signalée")

print(string.format("%-12s %d OK, %d ECHEC(S)", "lua", passes, fails))
os.exit(fails == 0 and 0 or 1)
