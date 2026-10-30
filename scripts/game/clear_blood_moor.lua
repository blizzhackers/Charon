-- Clears the Blood Moor: leaves the Rogue Encampment, explores every room of
-- the Blood Moor and kills every hostile monster it finds.
--
--   Game.exe -oogscript scripts\oog\create_character.lua -gamescript scripts\game\clear_blood_moor.lua
--
-- Along the way it drinks healing potions, picks up gold and potions, spends
-- stat and skill points, walks back to Akara to heal when out of potions, and
-- recovers its corpse after a death. Everything runs from tick(); nothing waits.
-- Set store 'exit_when_done' to leave the game once the area is clear.

local fsm = require 'lib.fsm'
local move = require 'lib.move'
local world = require 'lib.world'

local TOWN, BLOOD_MOOR = 1, 2
local SIGHT = 22            -- engage hostiles within this many subtiles
local MELEE_RANGE = 2.8
local VISIT_RADIUS = 14     -- a room counts as explored once we pass this close to its centre
local LOOT_RADIUS = 12
local POTION_AT = 0.55      -- drink below this fraction of life
local RETREAT_AT = 0.30     -- go heal in town below this with no potions left
local TARGET_TIMEOUT = 20000
local LOOT = { gld = true, hp1 = true, hp2 = true, hp3 = true, hp4 = true, hp5 = true, rvs = true, rvl = true }
local STAT_PLAN = { 3, 3, 3, 0, 0 } -- per level: vitality x3, strength x2
local SKILL_PLAN = { 'Bash', 'Sword Mastery', 'Axe Mastery', 'Howl', 'Double Swing' }
local ATTACK = 0

local stats = { kills = 0, started = charon.now() }
local visited = {}          -- room index -> true
local ignored = {}          -- unit id -> time until which it is skipped
local rooms                 -- Blood Moor rooms, loaded on arrival
local mover = move.new()
local target_id
local last_potion, last_points, last_attack = 0, 0, 0
local start_xp

local function now() return charon.now() end

local function life_fraction(me)
  return me.max_life > 0 and me.life / me.max_life or 1
end

-- Spend points as they arrive; requests are throttled until the server answers.
local function spend_points(me)
  if now() - last_points < 1500 then return end
  if me.stat_points > 0 then
    local stat = STAT_PLAN[(me.stat_points - 1) % #STAT_PLAN + 1]
    act.add_stat(stat, 1)
    last_points = now()
    return
  end
  if me.skill_points > 0 then
    local known = {}
    for _, skill in ipairs(game.skills()) do known[skill.id] = skill.base_level end
    local by_name = {}
    for _, info in ipairs(game.class_skills()) do by_name[info.name] = info end
    for _, name in ipairs(SKILL_PLAN) do
      local info = by_name[name]
      if info and info.required_level <= me.level and (known[info.id] or 0) < info.max_level then
        local ready = true
        for _, required in ipairs(info.prerequisites) do
          if (known[required] or 0) == 0 then ready = false end
        end
        if ready then
          print('learning ' .. name)
          act.add_skill(info.id)
          last_points = now()
          return
        end
      end
    end
  end
end

-- Bash when learned and affordable, otherwise the basic attack.
local function choose_attack(me)
  local best = ATTACK
  for _, skill in ipairs(game.skills()) do
    if skill.name == 'Bash' and skill.level > 0 and skill.usable and me.mana >= skill.mana then best = skill.id end
  end
  if me.left_skill ~= best then act.select_skill('left', best) end
end

local function drink_if_needed(me)
  if life_fraction(me) >= POTION_AT or now() - last_potion < 1500 then return end
  local slot = world.belt_slot('rv') or world.belt_slot('hp')
  if slot then
    act.use_belt(slot)
    last_potion = now()
  end
end

local function hostiles_near(me, radius)
  return world.nearest(game.units('monster'), me, function(u)
    return world.hostile(u) and (ignored[u.id] or 0) < now() and world.distance(me, u) <= radius
  end)
end

local function loot_near(me)
  return world.nearest(game.units('item'), me, function(item)
    return item.location == 'ground' and LOOT[item.code] and (ignored[item.id] or 0) < now()
      and world.distance(me, item) <= LOOT_RADIUS
  end)
end

local function mark_visited(me)
  for index, room in ipairs(rooms) do
    if not visited[index] and room.center_x and world.distance(me, { x = room.center_x, y = room.center_y }) <= VISIT_RADIUS then
      visited[index] = true
    end
  end
end

local function next_room(me)
  local best, best_index, best_distance
  for index, room in ipairs(rooms) do
    if not visited[index] then
      if not room.center_x then
        visited[index] = true -- no walkable cell at all
      else
        local d = world.distance(me, { x = room.center_x, y = room.center_y })
        if not best_distance or d < best_distance then best, best_index, best_distance = room, index, d end
      end
    end
  end
  return best, best_index
end

local function remaining_rooms()
  local count = 0
  for index in ipairs(rooms) do if not visited[index] then count = count + 1 end end
  return count
end

local function my_corpse(me)
  for _, unit in ipairs(game.units('player')) do
    if unit.name == me.name and unit.id ~= me.id then return unit end
  end
end

local bot
bot = fsm.new('start', {
  start = {
    tick = function(m)
      local me = game.me()
      start_xp = start_xp or me.experience
      if me.area == BLOOD_MOOR then return 'explore', 'in the Blood Moor' end
      if me.area ~= TOWN then return 'failed', 'started outside act 1 town and Blood Moor' end
      mover:to_level(BLOOD_MOOR)
      return 'travel', 'leaving town'
    end,
  },

  travel = {
    tick = function(m)
      local status = mover:tick()
      if status == 'arrived' then return 'explore', 'reached the Blood Moor' end
      if status == 'failed' then return 'failed', 'no route to the Blood Moor: ' .. tostring(mover.reason) end
    end,
  },

  explore = {
    enter = function(m)
      if not rooms then
        rooms = map.rooms(BLOOD_MOOR)
        print(string.format('Blood Moor has %d rooms', #rooms))
      end
      m.data.room = nil
    end,
    tick = function(m)
      local me = game.me()
      if me.area ~= BLOOD_MOOR then return 'start', 'left the area' end
      local corpse = my_corpse(me)
      if corpse then target_id = corpse.id; return 'recover', 'corpse nearby' end
      if hostiles_near(me, SIGHT) then return 'fight' end
      if loot_near(me) then return 'loot' end
      mark_visited(me)
      local room, index = next_room(me)
      if not room then return 'done', 'every room visited' end
      if m.data.room ~= index then
        m.data.room = index
        if mover:to(room.center_x, room.center_y) == 'failed' then
          print('room ' .. index .. ' unreachable: ' .. tostring(mover.reason))
          visited[index] = true
          m.data.room = nil
        end
        return
      end
      local status = mover:tick()
      if status == 'arrived' or status == 'failed' then
        visited[index] = true
        m.data.room = nil
      end
    end,
  },

  fight = {
    enter = function(m) target_id = nil end,
    tick = function(m)
      local me = game.me()
      local monster = target_id and game.unit('monster', target_id)
      if monster and not monster.alive then
        stats.kills = stats.kills + 1
        target_id, monster = nil, nil
      end
      if not monster or not world.hostile(monster) then
        local nearest = hostiles_near(me, SIGHT)
        if not nearest then return 'explore', string.format('area clear around us (%d kills)', stats.kills) end
        target_id, monster = nearest.id, nearest
        m.data.since = now()
        mover:stop()
      end
      if now() - m.data.since > TARGET_TIMEOUT then
        ignored[target_id] = now() + 30000
        target_id = nil
        return
      end
      choose_attack(me)
      local d = world.distance(me, monster)
      if d > MELEE_RANGE then
        -- Close in: straight when the line is clear, otherwise along a path.
        if map.line_clear(me.x, me.y, monster.x, monster.y) then
          if now() - last_attack > 300 and act.ready() then
            act.move(math.floor(monster.x), math.floor(monster.y))
            last_attack = now()
          end
        else
          if mover.status ~= 'moving' or now() - (m.data.planned or 0) > 1500 then
            mover:to(monster.x, monster.y)
            m.data.planned = now()
          end
          if mover:tick() == 'failed' then
            ignored[target_id] = now() + 30000
            target_id = nil
          end
        end
      elseif act.ready() and now() - last_attack > 150 then
        mover:stop()
        act.cast_at('left', monster)
        last_attack = now()
      end
    end,
  },

  loot = {
    tick = function(m)
      local me = game.me()
      if hostiles_near(me, 10) then return 'fight' end
      local item, d = loot_near(me)
      if not item then return 'explore' end
      if m.data.item ~= item.id then
        m.data.item, m.data.since = item.id, now()
      elseif now() - m.data.since > 6000 then
        ignored[item.id] = now() + 60000
        return 'explore', 'could not pick up ' .. tostring(item.name)
      end
      if d > 3 then
        if act.ready() and now() - last_attack > 400 then
          act.move(math.floor(item.x), math.floor(item.y))
          last_attack = now()
        end
      elseif now() - last_attack > 500 then
        act.pickup(item)
        last_attack = now()
      end
    end,
  },

  retreat = {
    enter = function(m) mover:to_level(TOWN) end,
    tick = function(m)
      local me = game.me()
      if me.area == TOWN then return 'heal' end
      if mover:tick() == 'failed' then mover:to_level(TOWN) end
    end,
  },

  heal = {
    enter = function(m) m.data.talked = false end,
    tick = function(m)
      local me = game.me()
      local akara = world.nearest(game.units('monster'), me, function(u) return u.name == 'Akara' end)
      if not akara then
        -- Akara stands in the camp; head for its middle until she is in sight.
        if mover.status ~= 'moving' then
          local x, y, w, h = map.bounds(TOWN)
          mover:to(x + w / 2, y + h / 2)
        end
        mover:tick()
        return
      end
      if game.ui('npc_menu') then
        m.data.talked = true
        act.key(27) -- close the dialog; talking already healed us
        return
      end
      if m.data.talked and life_fraction(me) > 0.95 then return 'start', 'healed by Akara' end
      if world.distance(me, akara) > 4 then
        if mover.status ~= 'moving' or now() - (m.data.planned or 0) > 2000 then
          mover:to(akara.x, akara.y)
          m.data.planned = now()
        end
        mover:tick()
      elseif act.ready() and now() - last_attack > 1000 then
        act.interact(akara)
        last_attack = now()
      end
    end,
  },

  recover = {
    tick = function(m)
      local me = game.me()
      local corpse = target_id and game.unit('player', target_id)
      if not corpse then return 'explore', 'corpse recovered' end
      if world.distance(me, corpse) > 3 then
        if mover.status ~= 'moving' then mover:to(corpse.x, corpse.y) end
        mover:tick()
      elseif now() - last_attack > 1000 then
        act.interact(corpse)
        last_attack = now()
      end
    end,
  },

  dead = {
    tick = function(m)
      local me = game.me()
      if me.alive then return 'start', 'respawned' end
      if m:elapsed() > 2000 and now() - last_attack > 2000 then
        act.key(27) -- "press ESC to continue"
        last_attack = now()
      end
    end,
  },

  done = {
    enter = function(m)
      local me = game.me()
      print(string.format('Blood Moor cleared: %d kills, level %d, %d experience, %.0f s',
        stats.kills, me.level, me.experience - (start_xp or me.experience), (now() - stats.started) / 1000))
      if store.get('exit_when_done') then
        store.set('blood_moor.done', true)
        act.leave_game()
      end
    end,
  },

  failed = {
    enter = function(m)
      if store.get('exit_when_done') then
        store.set('blood_moor.done', true)
        act.leave_game()
      end
    end,
  },
}, { name = 'blood moor' })

function tick()
  if not game.ready() then return end
  local me = game.me()
  if not me.alive then
    if not bot:is('dead') then bot:go('dead', 'died') end
  elseif not bot:is('done') and not bot:is('failed') then
    spend_points(me)
    drink_if_needed(me)
    if me.area == BLOOD_MOOR and life_fraction(me) < RETREAT_AT and not world.belt_slot('hp')
      and not world.belt_slot('rv') and not bot:is('retreat') then
      bot:go('retreat', 'low life and no potions')
    end
  end
  bot:tick()
end
