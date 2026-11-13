package.path = 'scripts/?.lua;' .. package.path
local now = 10000
charon = { now = function() return now end }
local menus = require('lib.menus')
local move = require('lib.move')
local stash = require('lib.stash')
local services = require('lib.services')

dofile('tests/script_move.lua')
dofile('tests/script_travel.lua')

-- Character identity ignores prefixes and extra display cells.
do
  local selected
  local controls = {
    { role = 'character_slot', character_name = 'Anna', text = 'Anna\tOther\nAnn\nLevel 12 Sorceress', id = 1 },
    { role = 'character_slot', character_name = 'Ann', text = 'Title\nAnn\tLevel 2 Sorceress\nExpansion', id = 2 },
    { role = 'select_ok', id = 3 },
  }
  oog = {
    screen = function() return 'character_select' end,
    controls = function() return controls end,
    click = function(c) if c.role == 'character_slot' then selected = c.id end; return true end,
  }
  local machine = menus.enterer('Ann')
  machine:tick()
  assert(selected == 2, 'must select Ann rather than Anna')
end

-- Irrelevant doors are ignored; real but unresponsive doors fail finitely.
do
  local me = { x = 0, y = 0, area = 1, stamina = 20, max_stamina = 20, mode = 1 }
  local door = { id = 9, x = 0, y = 5, door = true, mode = 0 }
  local clicks, moves = 0, 0
  game = {
    me = function() return me end,
    units = function(kind) return kind == 'object' and { door } or {} end,
  }
  map = {
    path = function() return { { x = 10, y = 0 } } end,
    invalidate = function() end,
    line_of_sight = function() return false end,
  }
  act = { ready = function() return true end, interact = function() clicks = clicks + 1 end, move = function() moves = moves + 1 end }
  local mover = move.new()
  mover:to(10, 0)
  for i = 1, 60 do now = now + 1300; mover:tick() end
  assert(mover.status == 'failed' and mover.replans > 0 and clicks == 0 and moves > 0)
  door.x, door.y = 4, 0
  mover:to(10, 0)
  for i = 1, 60 do now = now + 1300; mover:tick() end
  assert(mover.status == 'failed' and clicks > 0 and clicks < 60)
  door.y, clicks = 3, 0 -- A wide door also keeps the finite refusal budget.
  mover:to(10, 0)
  for i = 1, 60 do now = now + 1300; mover:tick() end
  assert(mover.status == 'failed' and clicks > 0 and clicks < 60)
end

-- A blocked route can cross the edge of a wide door, away from its center.
do
  local me = { x = 22548, y = 9612 }
  local point = { x = 22547, y = 9599 }
  local door = { id = 192, class_id = 47, x = 22545, y = 9610, door = true, mode = 0 }
  local clear = false
  game = { units = function() return { door } end }
  map = { line_of_sight = function(x1, y1, x2, y2)
    assert(x1 == me.x and y1 == me.y and x2 == point.x and y2 == point.y)
    return clear
  end }
  assert(move.closed_door_ahead(me, point) == door, 'route crossing a wide closed door must open it')
  clear = true
  assert(move.closed_door_ahead(me, point) == nil, 'a clear route must ignore a wide door beside it')
  clear = false
  me.x, me.y, point.x, point.y = 0, 0, 4, 0
  door.x, door.y = -1, 3
  assert(move.closed_door_ahead(me, point) == nil, 'ignore a wide door behind the player')
  door.x = 5
  assert(move.closed_door_ahead(me, point) == nil, 'ignore a wide door beyond the waypoint')
  door.x, door.y, clear = 2, 0, true
  assert(move.closed_door_ahead(me, point) == door, 'preserve narrow door detection')
end

-- Timeout between lift and placement rolls our item back before completion.
do
  local me = { x = 0, y = 0, area = 1, expansion = true, gold = 0, cursor_item = false }
  local rune = { id = 8, code = 'r01', width = 1, height = 1, location = 'container', page = 0, grid_x = 3, grid_y = 1 }
  local ui, placements = true, 0
  game = {
    me = function() return me end,
    items = function() return { rune } end,
    ui = function(panel) return panel == 'stash' and ui end,
  }
  act = {
    key = function() ui = false end,
    pick_item = function() me.cursor_item = true; rune.location = 'cursor' end,
    place_item = function(item, where)
      assert(item.id == rune.id and where == 'inventory')
      placements = placements + 1; rune.location = 'container'; me.cursor_item = false
    end,
  }
  local stasher = stash.new(function(item) return item.code == 'r01' end)
  stasher.started = now - 39600 -- approach took most of the visit's budget
  assert(stasher:tick(me) == 'busy' and me.cursor_item)
  now = now + 440
  assert(stasher:tick(me) == 'busy' and not me.cursor_item and placements == 1)
  now = now + 440
  assert(stasher:tick(me) == 'done' and not stasher.held)
end

-- NPC identity is independent of its localized display name.
do
  local me = { x = 0, y = 0, area = 1, stamina = 20, max_stamina = 20, mode = 1 }
  local npc = { class_id = 148, name = 'Localized Akara', x = 0, y = 0 }
  local interactions = 0
  game = {
    me = function() return me end,
    units = function(kind) return kind == 'monster' and { npc } or {} end,
    ui = function() return false end,
  }
  map = { presets = function() return { { type = 'monster', class_id = 148, x = 0, y = 0 } } end, path = function() return {} end }
  act = { ready = function() return true end, interact = function() interactions = interactions + 1 end }
  local visit = services.new()
  for i = 1, 20 do now = now + 1000; assert(visit:talk(me, 'Akara') == nil) end
  assert(interactions > 0)
end


-- Shared-module regression tests, run by charon_script_host_test from the repo root.
-- Game state is mocked; this does not launch or contact the game.
package.path = 'scripts/?.lua;' .. package.path

local function fresh()
  for name in pairs(package.loaded) do
    if name:sub(1, 4) == 'lib.' then package.loaded[name] = nil end
  end
end

fresh()
do
  local t, sends = 4000, 0
  charon = { now = function() return t end }
  local inventory = {{ id=123, code='hp1', location='container', page=0 }}
  game = { items = function() return inventory end }
  act = { to_belt = function() sends = sends + 1 end }
  local c = require('lib.combat').new({})
  c:refill_belt({}) -- Full belt: the server leaves this potion in inventory.
  t = 8000
  c:refill_belt({}) -- A slot is now free; potion still has the same id.
  assert(sends == 2, 'rejected transfer must be retried')
  inventory[1].location = 'belt'
  t = 12000; c:refill_belt({})
  assert(sends == 2)
  inventory = {
    {id=124, code='hp1', location='container', page=0},
    {id=125, code='mp1', location='container', page=0},
  }
  local attempted = {}
  act.to_belt = function(item) attempted[#attempted+1] = item.id end
  t = 16000; c:refill_belt({})
  t = 20000; c:refill_belt({})
  assert(attempted[1] == 124 and attempted[2] == 125,
    'one refused potion must not starve the other inventory transfers')
end

local function setup(skills, mana)
  act = {ready=function() return true end}
  local me = {id=1, x=0, y=0, level=21, mana=mana, max_mana=50,
    life=100, max_life=100, stamina=20, max_stamina=20, left_skill=0, right_skill=38}
  local target = {id=2, type='monster', class_id=1, x=7, y=0,
    name='ordinary monster', alive=true, attackable=true, npc=false,
    owner=-1, life_percent=100}
  charon = { now = function() return 4000 end }
  game = {
    skills=function() return skills end, me=function() return me end,
    items=function() return {} end,
    units=function() return {target} end,
    state=function(kind,_,state) return kind=='monster' and state==11 end, -- The target is already chilled.
    difficulty_info=function() return {static_field_min=0} end,
    monster_info=function() return {min_life=100,max_life=100,experience=100,
      level=21,resist={physical=0,fire=0,lightning=0,cold=0}} end,
    skill_info=function() return {element=1} end,
    skill_damage=function() return {physical_min=0,physical_max=0,
      elemental_min=10,elemental_max=10} end,
  }
  map={line_of_sight=function() return true end, safe_spot=function()
    return {found=false,path={},danger_here=0,missile_here=0,open_sectors=8}
  end}
  return me, target
end

fresh()
do
  -- These mana costs are mocked API inputs; this test proves the logic error,
  -- not exact live Skills.txt costs at this rank. No Fire Bolt item is present.
  local me, target = setup({
    {name='Charged Bolt',id=38,level=19,mana=12,usable=false,owner=-1},
    {name='Frost Nova',id=44,level=1,mana=9,usable=true,owner=-1},
    {name='Static Field',id=42,level=1,mana=9,usable=true,owner=-1},
  }, 9)
  local c=require('lib.combat').new(require('lib.builds').sorceress)
  c:fight(me,12)
  assert(not c.ignored[target.id])
  assert(c.resting and c:out_of_mana())
end

fresh()
do
  local me = setup({
    {name='Fire Bolt',id=36,level=1,mana=3,usable=true,owner=123,charges=20},
  }, 20)
  local attempted_owner, selected = false, false
  act = {
    move=function() end,
    cast_at=function() end,
    ready=function() return true end,
    select_skill=function(hand,id,owner)
      attempted_owner = owner
      -- Match ActSelectSkill: owner defaults to -1; no innate Fire Bolt exists.
      selected = owner == 123
      return selected
    end,
  }
  local c=require('lib.combat').new(require('lib.builds').sorceress)
  c:fight(me,12)
  assert(attempted_owner == false and not selected)
  assert(next(c:known_skills()) == nil, 'item-only skills excluded by campaign policy')
  local innate = {name='Fire Bolt',id=36,level=1,mana=3,usable=true,owner=4294967295}
  game.skills = function() return {innate, {name='Fire Bolt',id=36,owner=123}} end
  assert(c:known_skills()['Fire Bolt'] == innate, 'item duplicate must not replace innate')
end

-- Both deadlines must settle our cursor operation; refusal must stop finitely.
do
  local t, placed, closed = 100000, 0, 0
  charon = { now = function() return t end }
  local rune = {id=8, code='r01', width=1, height=1, location='cursor', page=0, grid_x=3, grid_y=1}
  local me = {in_town=true, cursor_item=true, expansion=true, gold=0}
  game = {me=function() return me end, items=function() return {rune} end, ui=function() return true end}
  act = {place_item=function(item, where)
    assert(item.id == 8 and where == 'inventory'); placed = placed + 1
  end, key=function() closed=closed+1 end}
  town = {close=function() closed=closed+1 end}
  local visit = require('lib.services').new()
  visit.started = t - 90001
  visit.stasher.held, visit.stasher.tries, visit.stasher.picked_at = rune, 0, t
  assert(visit:tick(me) == 'busy' and placed == 1 and closed == 0)
  t = t + 11000
  assert(not pcall(function() visit:tick(me) end))
  assert(visit.stasher.held == rune and closed == 0)
  -- A later confirmed rollback permits completion.
  rune.location, me.cursor_item = 'container', false
  assert(visit:tick(me) == 'done')
  -- An unrelated cursor item is neither placed nor cleared by stash finish.
  local stasher = require('lib.stash').new(function() return false end)
  me.cursor_item = true
  local before = placed
  assert(stasher:finish() == 'done' and placed == before)
end

fresh()
do
  local me, target = setup({{name='Charged Bolt',id=38,level=1,mana=3,usable=true,owner=-1}}, 20)
  target.x=5 -- a legal cast is needed to exercise skill-selection refusal
  local build = require('lib.builds').sorceress
  local c = require('lib.combat').new(build)
  act = {ready=function() return true end, select_skill=function() return false end}
  me.right_skill = 0
  assert(not pcall(function() c:fight(me,12) end), 'failed selection must stop rather than loop')
  game.monster_info=function() return {min_life=100,max_life=100,experience=100,level=21,
    resist={physical=100,fire=100,lightning=100,cold=100}} end
  fresh()
  c = require('lib.combat').new(require('lib.builds').sorceress)
  target.x = 3
  c:fight(me,12)
  assert(c.ignored[target.id], 'actual immunity still permits skipping')
end

-- Services also expires failed belt transfers rather than remembering success.
fresh()
do
  local t, sends = 4000, 0
  charon = {now=function() return t end}
  local potion = {id=1, code='hp1', location='container', page=0}
  game = {items=function() return {potion} end, ui=function(panel) if panel then return false end; return {} end}
  act = {to_belt=function() sends=sends+1 end}
  local visit = require('lib.services').new()
  visit.step, visit.logged_panels = 'belt', true
  local me = {in_town=true, gold=0, stash_gold=0}
  assert(visit:tick(me) == 'busy' and sends == 1)
  t = 8000
  assert(visit:tick(me) == 'done' and sends == 1, 'one belt pass must finish')
  visit.step = 'belt' -- a later pass can retry the same rejected item
  assert(visit:tick(me) == 'busy' and sends == 2)
  t = 9000
  visit.step = 'belt'
  visit:tick(me)
  assert(sends == 2, 'belt requests must respect cooldown')
end

-- A full belt and many spare potions must not postpone stashing until timeout.
fresh()
do
  local t, sends, stashed = 4000, 0, 0
  charon = {now=function() return t end}
  local potions = {}
  for id = 1, 8 do potions[id] = {id=id, code='hp1', location='container', page=0} end
  game = {items=function() return potions end, ui=function() return false end}
  act = {to_belt=function() sends=sends+1 end}
  local visit = require('lib.services').new()
  visit.step, visit.logged_panels = 'belt', true
  visit.stasher = {wanted=function() return true end, tick=function() stashed=stashed+1; return 'done' end}
  local status
  for i = 1, 12 do
    status = visit:tick({in_town=true, gold=500, stash_gold=0})
    if status == 'done' then break end
    t = t + 720
  end
  assert(status == 'done' and stashed == 1 and sends == 1,
    'refused belt transfers must not cycle and prevent stashing')
end

-- Healed by speaking to Akara with nothing to trade, the visit is not over
-- until her menu has come up and been closed: a speech left running holds the character.
fresh()
do
  local t, clicks, escapes, closed, menu = 100000, 0, 0, 0, false
  charon = {now=function() return t end}
  local akara = {id=7, class_id=148, x=2, y=0, name='Akara'}
  local me = {in_town=true, x=0, y=0, life=20, max_life=40, mana=35, max_mana=35, gold=0, stash_gold=0}
  game = {me=function() return me end, items=function() return {} end,
    units=function() return {akara} end,
    ui=function(panel) if panel then return panel=='npc_menu' and menu end; return {} end}
  act = {ready=function() return true end, interact=function() clicks=clicks+1; me.life=40 end,
    key=function(key) assert(key==27); escapes=escapes+1; menu=true end}
  town = {close=function() closed=closed+1; menu=false end}
  local visit = require('lib.services').new({}, {})
  visit.stasher = {wanted=function() return false end, tick=function() return 'done' end, settle=function() end}
  assert(visit:tick(me) == 'busy' and clicks == 1 and me.life == 40, 'she speaks to Akara and is healed')
  t = t + 800
  assert(visit:tick(me) == 'busy' and visit.step == 'akara' and closed == 0,
    'healed with nothing to trade, the visit still waits for her menu')
  t = t + 2400
  assert(visit:tick(me) == 'busy' and escapes == 1, 'no menu three seconds on: a speech is playing, and it is skipped')
  t = t + 800
  assert(visit:tick(me) == 'busy' and closed == 1 and visit.step == 'belt', 'with the menu up it is closed, and the visit goes on')
end

-- Gear that is wearing out is mended at the smith's, once a visit.
fresh()
do
  local t, repaired, traded, closed = 200000, 0, 0, 0
  charon = {now=function() return t end}
  local charsi = {id=9, class_id=154, x=2, y=0, name='Charsi'}
  local cap = {id=21, name='Cap', location='equipped', body_location=1}
  local durability = {[72]=3, [73]=12}
  local me = {in_town=true, x=0, y=0, life=40, max_life=40, mana=35, max_mana=35, gold=500, stash_gold=0}
  local menu, shop = false, false
  game = {me=function() return me end, items=function() return {cap} end, units=function() return {charsi} end,
    stat=function(_, id, stat) return id==21 and durability[stat] or 0 end,
    ui=function(panel) if panel then return panel=='npc_menu' and menu or panel=='npc_shop' and shop end; return {} end}
  act = {ready=function() return true end, interact=function() menu=true end, key=function() end, to_belt=function() end}
  town = {close=function() closed=closed+1; menu,shop=false,false end,
    trade=function(npc) assert(npc==charsi); traded=traded+1; shop=true end,
    repair_all=function(npc) assert(npc==charsi and shop); repaired=repaired+1; durability[72]=12; return 37 end}
  local services = require('lib.services')
  local visit = services.new({}, {})
  visit.stasher = {wanted=function() return false end, tick=function() return 'done' end, settle=function() end}
  assert(services.worn_out()==cap and visit:needs(me), 'a cap at a quarter of its durability is a reason for a visit')
  local status
  for _=1,12 do
    status = visit:tick(me)
    if status == 'done' then break end
    t = t + 800
  end
  assert(traded==1 and repaired==1 and closed>=1 and status=='done', 'she goes to Charsi, asks for a repair of everything, and leaves')
  assert(not services.worn_out() and not visit:needs(me), 'and mended gear asks for nothing more')
  durability[72]=5
  assert(not services.worn_out(), 'above a third it is left alone')
end

print('shared Lua library regressions passed')
dofile('tests/script_combat.lua')
dofile('tests/script_explore.lua')
dofile('tests/script_stamina.lua')
dofile('tests/script_supplies.lua')
dofile('tests/script_campaign.lua')
dofile('tests/script_loot.lua')
dofile('tests/script_value.lua')
dofile('tests/script_gear.lua')
