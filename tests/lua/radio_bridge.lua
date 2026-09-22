-- ===========================================================================
--  « Radio » pour le banc d'intégration : exécute trmbox.lua avec l'API
--  EdgeTX simulée, piloté ligne par ligne par tests/qemu/sim.py :
--    entrée : "FM <texte>"  capteur FM (dernier message reçu par la radio)
--             "T <n>"       avance de n ticks (10 ms)
--             "EV <nom>"    touche : NEXT, PREV, ENTER, ENTER_LONG
--    sortie : "GV <valeur>" à chaque écriture de GV9
--             "SAY <n>"     à chaque annonce vocale (centièmes)
--             "SCR <texte>" contenu de l'écran après chaque touche
-- ===========================================================================
local T, fm = 0, 0
getTime = function() return T end
getValue = function(n) if n == "FM" then return fm end return 0 end
model = { setGlobalVariable = function(i, f, v) if i == 8 then io.write("GV ", v, "\n") io.flush() end end }
playTone = function() end
playHaptic = function() end
playNumber = function(v) io.write("SAY ", v, "\n") io.flush() end
LCD_W, LCD_H = 128, 64
SMLSIZE, MIDSIZE, DBLSIZE, INVERS, RIGHT, BLINK, PREC2 = 0x100, 0x200, 0x400, 1, 2, 4, 0x20
EVT_VIRTUAL_NEXT, EVT_VIRTUAL_PREV, EVT_VIRTUAL_ENTER, EVT_VIRTUAL_ENTER_LONG = 101, 102, 103, 104
local screen = {}
lcd = { clear = function() screen = {} end, drawText = function(_, _, s) screen[#screen + 1] = s end,
        drawFilledRectangle = function() end, drawLine = function() end, drawRectangle = function() end }
local EV = { NEXT = 101, PREV = 102, ENTER = 103, ENTER_LONG = 104 }
local s = dofile(arg[1])
s.init()
for line in io.lines() do
  local cmd, rest = line:match("^(%S+) ?(.*)$")
  if cmd == "FM" then fm = rest
  elseif cmd == "T" then for _ = 1, tonumber(rest) do T = T + 1 s.background() end
  elseif cmd == "EV" then s.run(EV[rest]) io.write("SCR ", table.concat(screen, " | "), "\n") io.flush()
  elseif cmd == "Q" then break end
end
