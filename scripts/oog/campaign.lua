-- Plays one character across many games: creates it once, then re-enters it
-- every time the menus come back (after save and exit or a death).
--
--   Game.exe -oogscript scripts\oog\campaign.lua -gamescript scripts\game\act1.lua
--
-- Leaving and re-entering is a speedrun tool: it returns the character to town
-- instantly, keeps waypoints and quest progress, and refills the area with
-- monsters. Class: store 'create.class' or CLASS below. To continue an existing
-- character instead, set store 'character' to its name before the first tick.

local menus = require 'lib.menus'

local CLASS = 'sorceress'
local MAX_GAMES = 50 -- stop rather than loop forever if something keeps failing

math.randomseed(os.time() + math.floor(charon.now()))

local machine, games = nil, 0

function tick()
  if store.get('campaign.stop') then return end
  if not machine or machine:is('returned') then
    games = games + 1
    if games > MAX_GAMES then
      print('campaign: giving up after ' .. MAX_GAMES .. ' games')
      store.set('campaign.stop', true)
      return
    end
    local name = store.get('character')
    machine = name and menus.enterer(name) or menus.creator(store.get('create.class') or CLASS)
  end
  machine:tick()
end
