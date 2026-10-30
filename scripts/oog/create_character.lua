-- Creates a new single player character with a random name and enters the game.
--
--   Game.exe -oogscript scripts\oog\create_character.lua [-gamescript ...]
--
-- Class: set store 'create.class' from another script, or edit CLASS below
-- ('amazon', 'sorceress', 'necromancer', 'paladin', 'barbarian', 'druid',
-- 'assassin' or 'random'). The created name is published as store 'character'.

local menus = require 'lib.menus'

local CLASS = 'barbarian'

math.randomseed(os.time() + math.floor(charon.now()))

local creator = menus.creator(store.get('create.class') or CLASS)

function tick()
  creator:tick()
end
