-- Plays through Act 1 toward Andariel, speedrun style.
--
--   Game.exe -oogscript scripts\oog\campaign.lua -gamescript scripts\game\act1.lua
--
-- Plan, re-evaluated whenever a step finishes (so a fresh game resumes):
--   1. In town: heal and buy potions with the gold picked up (lib/services.lua).
--   2. After a death: walk back and recover the corpse.
--   3. Den of Evil once the build can take it (build.den_level): safe experience
--      plus a skill point from Akara, read back from the quest record.
--   4. Head along the map's known exits toward Catacombs Level 4, fighting what
--      is on or near the way and activating waypoints passed on the route.
--      From town, the waypoint closest to the goal saves the walk.
--   5. When the next area out-levels the character, farm the most valuable
--      nearby area: experience per point of effective damage for the build's
--      element, spawn density, detour and measured rates (lib/leveling.lua).
--   6. Andariel: found from the level's preset spawns; done when the quest
--      record credits her kill.

local fsm = require 'lib.fsm'
local builds = require 'lib.builds'
local combat_lib = require 'lib.combat'
local explore = require 'lib.explore'
local leveling = require 'lib.leveling'
local loot_lib = require 'lib.loot'
local move = require 'lib.move'
local services = require 'lib.services'
local stamina = require 'lib.stamina'
local travel = require 'lib.travel'
local world = require 'lib.world'

local TOWN, DEN, CATACOMBS_4 = 1, 8, 37
local ANDARIEL_CLASS = 156
local QUEST_DEN, QUEST_ANDARIEL = 1, 6
local QUEST_TOWER, COUNTESS_CELLAR = 5, 25 -- Forgotten Tower; Tower Cellar Level 5
local QUEST_RESPEC = 41      -- Akara's free reset, armed by the Den of Evil
local HIRE_MIN_GOLD = 300    -- a Normal Act 1 Rogue costs a few hundred
local ENTER_ABOVE = 1        -- enter areas whose monsters are at most this many levels above us
local ANDARIEL_LEVEL = 12    -- character level from which Andariel is a fair fight
-- Chicken: leave the game (instant, unlike a portal cast) when a fight turns.
local CHICKEN_CROWDED, CHICKEN_LOW = 0.25, 0.15 -- with 2+ monsters close / any monster near
local CHICKEN_BURST, BURST_BELOW = 0.30, 0.40    -- that much life lost fast, ending below this
local BURST_MS = 2000
local FROZEN_MS = 5000      -- unable to act this long: leave the game
local NOT_FARMED = { [1] = true, [38] = true, [39] = true } -- town, Tristram, Cow Level
-- Supplies: mana potions and rejuvenations carried plus those the gold buys.
-- Below SUPPLY_LOW pushing on is pointless (she runs dry before looting), so
-- she farms gold in an easy area near a waypoint until SUPPLY_OK.
local SUPPLY_LOW, SUPPLY_OK = 6, 12
local MANA_POTION_GOLD = 60
local GOLD_FARM_BELOW = 3   -- gold farms have monsters at least this many levels below us
local TRAVEL_SIGHT = 12      -- fight what is this close while travelling
local CLEAR_SIGHT = 24       -- fight everything this close while clearing or farming
local RETREAT_LIFE = 0.35
local POTIONS = { hp = 4, mp = 10, tsc = 1 }

local me0 = game.me()
local build = builds.for_class(me0.class)
local combat = combat_lib.new(build)
local loot = loot_lib.new()
local gather = loot_lib.new(30) -- wider sweep before leaving a level
local containers = require('lib.containers').new(12)
local trip = travel.new()
local mover = move.new()
-- Spell builds run on mana: buy mana potions first.
local POTION_ORDER = build.has_spells and { 'mp', 'hp' } or { 'hp', 'mp' }
local visit = services.new(POTIONS, POTION_ORDER)
local explorer
local started = charon.now()
local stall = { mark = nil, since = charon.now(), count = 0 }

local function key(name) return 'act1.' .. me0.name .. '.' .. name end
local function flag(name) return store.get(key(name)) end
local function set_flag(name, value) store.set(key(name), value) end
local function now() return charon.now() end

local function quest_done(id)
  local q = game.quest(id)
  return q and (q.completed or q.reward_pending) or false
end

-- Fight what threatens us, then pick up what is worth it. True when busy.
local function reflexes(me, sight)
  if combat:threat(me, sight) then
    combat:fight(me, sight)
    return true
  end
  if loot:nearby(me) then
    loot:tick(me)
    return true
  end
  -- Chests, urns, corpses and the like are free loot.
  if not me.in_town and containers:nearby(me) then
    containers:tick(me)
    return true
  end
  return false
end

-- Waypoint object spawn in a level, from presets and Objects.txt.
local function waypoint_spawn(level)
  for _, preset in ipairs(map.presets(level)) do
    if preset.type == 'object' then
      local info = game.object_info(preset.class_id)
      if info and info.waypoint then return preset end
    end
  end
end

local function known_waypoints()
  local history = game.waypoints()
  local known = {}
  for level = 1, 40 do
    if (history and history[level]) or flag('wp.' .. level) then known[level] = true end
  end
  return known
end

local function supplies(me)
  local count = 0
  for _, item in ipairs(game.items()) do
    local kind = item.code and item.code:sub(1, 2)
    if (item.location == 'belt' or (item.location == 'container' and item.page == 0))
      and (kind == 'mp' or kind == 'rv') then
      count = count + 1
    end
  end
  return count + math.floor((me.gold + (me.stash_gold or 0)) / MANA_POTION_GOLD)
end

-- Gold-farming areas: easy monsters, at or next to an activated waypoint so a
-- save and exit (which rejoins with full mana) brings us straight back.
-- Returns candidate levels and, per level, the waypoint to reach it by.
local function gold_farms(me, below)
  below = below or GOLD_FARM_BELOW
  local candidates, via = {}, {}
  for wp in pairs(known_waypoints()) do
    if wp ~= TOWN then
      for level, distance in pairs(leveling.hops_from(wp)) do
        local info = game.level_info(level)
        if distance <= 1 and not NOT_FARMED[level] and level ~= DEN and info
          and info.monster_level > 0 and info.monster_level <= me.level - below then
          if not via[level] then candidates[#candidates + 1] = level end
          if not via[level] or level == wp then via[level] = wp end
        end
      end
    end
  end
  return candidates, via
end

-- The activated waypoint level on the town-to-goal route with the fewest hops left.
local function best_waypoint(goal)
  local route = travel.route(TOWN, goal)
  if not route then return nil end
  local known, best, best_hops = known_waypoints(), nil, nil
  for index, hop in ipairs(route) do
    if known[hop.to] and hop.to ~= TOWN then
      local remaining = #route - index
      if not best_hops or remaining < best_hops then best, best_hops = hop.to, remaining end
    end
  end
  return best, best_hops
end

-- Where to go next, or nil and why when the character must level first.
local function next_goal(me)
  if not quest_done(QUEST_DEN) then
    -- Gargantuan Beasts hit hard: each build enters the Den at its own level.
    if me.level >= (build.den_level or 3) then return DEN end
    return nil, 'level ' .. me.level .. ' is low for the Den of Evil'
  end
  local route = travel.route(me.area, CATACOMBS_4)
  if not route or #route == 0 then return CATACOMBS_4 end
  local info = game.level_info(route[1].to)
  if info and info.monster_level > me.level + ENTER_ABOVE then
    return nil, string.format('level %d is low for %s (monster level %d)', me.level, info.name, info.monster_level)
  end
  -- A typical player takes Andariel on from about level 12.
  if me.level < ANDARIEL_LEVEL then
    return nil, string.format('level %d is low for Andariel (needs %d)', me.level, ANDARIEL_LEVEL)
  end
  return CATACOMBS_4
end

-- Clicks the waypoint once it is in sight; the client walks up to it.
local function use_waypoint_object(m, me)
  local wp = world.nearest(game.units('object'), me, function(o) return o.waypoint end)
  if wp and world.distance(me, wp) <= 20 then
    if act.ready() and now() - (m.data.used or 0) > 1500 then
      act.interact(wp)
      m.data.used = now()
    end
    return true
  end
  return false
end

local bot
bot = fsm.new('warm', {
  -- Generate the act's level layouts one per tick (breadth-first from here)
  -- so route and leveling plans never spend a whole frame generating them.
  warm = {
    enter = function(m) m.data.warm_queue, m.data.warm_seen = { game.me().area }, { [game.me().area] = true } end,
    tick = function(m)
      local queue = m.data.warm_queue
      local level = table.remove(queue, 1)
      if not level then return 'decide', 'act layout ready' end
      local ok, exits = pcall(map.exits, level)
      if ok then
        for _, exit in ipairs(exits) do
          if not m.data.warm_seen[exit.level] then
            m.data.warm_seen[exit.level] = true
            queue[#queue + 1] = exit.level
          end
        end
      end
    end,
  },

  decide = {
    tick = function(m)
      local me = game.me()
      -- The client's quest record is only refreshed when the server sends it,
      -- which talking to an NPC does: sync once per game before trusting it.
      if me.in_town and not m.data.quests_synced then return 'sync_quests' end
      -- Likewise the waypoint list is only current once a waypoint panel has
      -- opened this game (before that it is missing or left over from the last
      -- game): open the town's once per game, unless the goal is next door.
      -- Akara's reset when the points spent stray from the build order (it is
      -- unlimited with Charon's setting): they come back and are spent by the
      -- order as it stands now.
      local respec = game.quest(QUEST_RESPEC)
      if me.in_town and respec and respec.reward_pending and not m.data.respec_tried then
        local off, skill = combat:off_build()
        if off then
          print('respec: ' .. skill .. ' is off the build order')
          return 'respec'
        end
      end
      -- A Rogue hireling: extra damage, and monsters split their attention.
      local merc = town.mercenary()
      if me.in_town and not m.data.hire_tried and (not merc or merc.dead) and me.level >= 8
        and me.gold + (me.stash_gold or 0) >= HIRE_MIN_GOLD then
        return 'hire'
      end
      if me.in_town and not m.data.waypoints_synced then
        local goal = next_goal(me) or CATACOMBS_4
        local route = travel.route(me.area, goal)
        if route and #route > 1 then return 'sync_waypoints' end
      end
      if quest_done(QUEST_ANDARIEL) then return 'done', 'Andariel quest credited' end
      -- One visit per town stay, so a failed sale cannot loop.
      if not me.in_town then m.data.town_done = nil end
      if me.in_town and not m.data.town_done and visit:needs(me) then return 'town', 'heal and restock' end
      for _, unit in ipairs(game.units('player')) do
        -- Our corpse in sight (for example brought to town by save and exit).
        if unit.name == me.name and unit.id ~= me.id then set_flag('corpse_area', me.area) end
      end
      if flag('corpse_area') then return 'corpse' end
      if me.area == CATACOMBS_4 then return 'andariel' end
      if me.area == DEN and not quest_done(QUEST_DEN) then return 'clear', 'Den of Evil' end
      local supply = supplies(me)
      if flag('gold_farm') and supply >= SUPPLY_OK then set_flag('gold_farm', nil) end
      if not flag('gold_farm') and supply < SUPPLY_LOW and quest_done(QUEST_DEN) then set_flag('gold_farm', true) end
      -- The Countess: her quest kill drops a pile of gold and she respawns
      -- every game with gold and runes, so short of gold we run her, saving
      -- and exiting after each kill for a fresh one (the speedrunners' tower
      -- farm, which levels us on the way).
      local cellar = game.level_info(COUNTESS_CELLAR)
      local countess_ok = cellar and cellar.monster_level <= me.level + ENTER_ABOVE
        and travel.route(me.area, COUNTESS_CELLAR) ~= nil
      local countess = countess_ok and not m.data.countess_done
        and (flag('gold_farm') or not quest_done(QUEST_TOWER))
      if countess and me.area == COUNTESS_CELLAR then return 'countess' end
      if not countess and flag('gold_farm') and me.area ~= DEN then
        if m.data.countess_done then return 'save_exit', 'Countess run done; a new game brings her back' end
        return 'farm', string.format('gold: %d mana potions in hand or affordable (%d gold)', supply, me.gold + (me.stash_gold or 0))
      end
      local goal, why
      if countess then
        goal = COUNTESS_CELLAR
        if bot.state == 'decide' and not m.data.countess_logged then
          m.data.countess_logged = true
          print(string.format('countess run (%d mana potions in hand or affordable, %d gold)', supply, me.gold + (me.stash_gold or 0)))
        end
      else
        goal, why = next_goal(me)
        if not goal and not m.data.override then return 'farm', why end
        m.data.override = nil
        goal = goal or CATACOMBS_4
      end
      if me.in_town then
        local level = best_waypoint(goal)
        if level then
          m.data.waypoint_to = level
          return 'take_waypoint', 'waypoint to ' .. game.level_name(level)
        end
      end
      local route = travel.route(me.area, goal)
      if not route or #route == 0 then return 'failed', 'no route to ' .. game.level_name(goal) end
      -- Out in a level with an activated waypoint: jump ahead when a known
      -- waypoint is closer to the goal than walking from here. Reaching a far
      -- waypoint costs about a level's walk, so it must save two levels.
      if not me.in_town and known_waypoints()[me.area] then
        local level, hops = best_waypoint(goal)
        local spawn = waypoint_spawn(me.area)
        local saved = level and (#route - hops - 1) or 0
        if level and level ~= me.area and spawn and (saved >= 2 or (saved >= 1 and world.distance(me, spawn) < 60)) then
          m.data.waypoint_to = level
          return 'take_waypoint', 'waypoint ahead to ' .. game.level_name(level)
        end
      end
      trip:to(route[1].to)
      return 'travel', game.level_name(route[1].to)
    end,
  },

  sync_quests = {
    enter = function(m) m.data.talker = services.new() end,
    tick = function(m)
      local me = game.me()
      if m:elapsed() > 30000 then
        m.data.quests_synced = true
        return 'decide', 'could not reach an NPC; quest record may be stale'
      end
      -- The nearest NPC in sight, else Akara's spawn.
      local name = 'Akara'
      local npc = world.nearest(game.units('monster'), me, function(u) return services.NPC_CLASS[u.name] ~= nil end)
      if npc then name = npc.name end
      if m.data.talker:talk(me, name) then
        town.close()
        m.data.quests_synced = true
        return 'decide', 'quest record synced by talking to ' .. name
      end
    end,
  },

  respec = {
    enter = function(m) m.data.talker, m.data.respec_sent = services.new(), nil end,
    tick = function(m)
      local me = game.me()
      if m.data.respec_sent then
        if me.skill_points > m.data.respec_points or m:elapsed() > 15000 then
          m.data.respec_tried = true
          return 'decide', string.format('reset: %d skill and %d stat points to spend', me.skill_points, me.stat_points)
        end
        return
      end
      if m:elapsed() > 45000 then
        m.data.respec_tried = true
        return 'decide', 'could not reach Akara for the reset'
      end
      local akara = m.data.talker:talk(me, 'Akara')
      if akara then
        m.data.respec_points = me.skill_points
        town.respec(akara)
        m.data.respec_sent = true
      end
    end,
  },

  -- Hire the best affordable Rogue from Kashya (highest level, then life).
  hire = {
    enter = function(m) m.data.talker, m.data.hired_at = services.new(), nil end,
    tick = function(m)
      local me = game.me()
      local merc = town.mercenary()
      if merc and not merc.dead then
        m.data.hire_tried = true
        -- The hire leaves client UI state (the hire list) that has no safe
        -- teardown: a new game resets it, and the Rogue comes along.
        return 'save_exit', 'hired a Rogue; new game to reset the hire list'
      end
      if m:elapsed() > 45000 then
        m.data.hire_tried = true
        town.close()
        return 'decide', 'could not hire a Rogue'
      end
      if m.data.hired_at then return end
      local kashya = m.data.talker:talk(me, 'Kashya')
      if not kashya then return end
      -- Opening her menu asks the server for the offers; they arrive shortly.
      local offers = town.hire_offers() or {}
      local gold = me.gold + (me.stash_gold or 0)
      local best
      for _, offer in ipairs(offers) do
        if offer.cost <= gold and (not best or offer.level > best.level
          or (offer.level == best.level and offer.life > best.life)) then
          best = offer
        end
      end
      if not best then
        if #offers > 0 or m:elapsed() > 20000 then
          m.data.hire_tried = true
          town.close()
          return 'decide', #offers > 0 and 'no affordable Rogue' or 'no Rogues on offer'
        end
        return
      end
      print(string.format('hiring %s: level %d, life %d, %d gold', best.name, best.level, best.life, best.cost))
      town.hire(kashya, best.index)
      m.data.hired_at = now()
    end,
  },

  sync_waypoints = {
    tick = function(m)
      local me = game.me()
      if game.ui('waypoint') or m:elapsed() > 30000 then
        m.data.waypoints_synced = true
        if not game.ui('waypoint') then return 'decide', 'could not open the town waypoint' end
        local count = 0
        for _ in pairs(game.waypoints() or {}) do count = count + 1 end
        act.key(27)
        return 'decide', count .. ' waypoints known'
      end
      if use_waypoint_object(m, me) then return end
      local spawn = waypoint_spawn(me.area)
      if not spawn then
        m.data.waypoints_synced = true
        return 'decide', 'no waypoint in town'
      end
      if mover.status ~= 'moving' then mover:to(spawn.x, spawn.y) end
      mover:tick()
    end,
  },

  town = {
    tick = function(m)
      if visit:tick(game.me()) == 'done' then
        visit = services.new(POTIONS, POTION_ORDER)
        m.data.town_done = true
        return 'decide', 'town visit done'
      end
    end,
  },

  travel = {
    tick = function(m)
      local me = game.me()
      if reflexes(me, TRAVEL_SIGHT) then return end
      local info = game.level_info(me.area)
      if info and info.waypoint and not me.in_town and not known_waypoints()[me.area] and m.data.no_wp ~= me.area then
        return 'activate_waypoint'
      end
      local status = trip:tick()
      if status == 'arrived' then
        m.data.failures = 0
        set_flag('travel_resets', nil)
        return 'decide'
      end
      if status == 'failed' then
        m.data.failures = (m.data.failures or 0) + 1
        print('travel failed: ' .. tostring(trip.reason))
        for _, o in ipairs(game.units('object')) do
          if world.distance(me, o) < 25 then
            print(string.format('  nearby object %d %s at %.0f: door %s, mode %d, subclass %s, open_warp %s',
              o.class_id, tostring(o.name), world.distance(me, o), tostring(o.door), o.mode, tostring(o.subclass), tostring(o.open_warp)))
          end
        end
        if m.data.failures > 3 then
          -- Stuck on this layout (a door that will not open, a blocked
          -- passage): a new game brings new layouts. Give up only when that
          -- keeps failing.
          set_flag('travel_resets', (flag('travel_resets') or 0) + 1)
          if flag('travel_resets') > 3 then return 'failed', trip.reason end
          return 'save_exit', 'cannot get through (' .. tostring(trip.reason) .. '); trying a new game'
        end
        map.invalidate()
        trip:to(trip.target)
      end
    end,
  },

  -- Touch the waypoint of a level we pass so later trips can skip the walk.
  -- Persistent: click it once in sight (the client walks up), otherwise head
  -- for its spawn, trying spots around it when the spawn itself has no route.
  activate_waypoint = {
    enter = function(m)
      m.data.wp_since, m.data.wp_spawn = now(), waypoint_spawn(game.me().area)
      m.data.wp_mover, m.data.wp_try = move.new(), 0
    end,
    tick = function(m)
      local me = game.me()
      if reflexes(me, TRAVEL_SIGHT) then return end
      if game.ui('waypoint') then
        local history = game.waypoints()
        if history and history[me.area] then set_flag('wp.' .. me.area, true) end
        act.key(27)
        return 'travel', 'waypoint activated in ' .. game.level_name(me.area)
      end
      local spawn = m.data.wp_spawn
      if not spawn or now() - m.data.wp_since > 30000 then
        m.data.no_wp = me.area
        return 'travel', 'waypoint not reached in 30 s'
      end
      if use_waypoint_object(m, me) then return end
      local mover_ = m.data.wp_mover
      if mover_.status ~= 'moving' then
        -- Try the spawn, then spots in a ring around it.
        local try = m.data.wp_try
        local angle = try * math.pi / 4
        local radius = try == 0 and 0 or 8
        m.data.wp_try = (try + 1) % 9
        mover_:to(spawn.x + math.cos(angle) * radius, spawn.y + math.sin(angle) * radius)
      end
      mover_:tick()
    end,
  },

  take_waypoint = {
    enter = function(m) m.data.wp_since = now() end,
    tick = function(m)
      local me = game.me()
      if me.area == m.data.waypoint_to then return 'decide', 'arrived by waypoint' end
      if now() - m.data.wp_since > 45000 then
        set_flag('wp.' .. m.data.waypoint_to, nil)
        return 'decide', 'waypoint travel timed out'
      end
      if game.ui('waypoint') then
        -- With the panel open the game's own history is authoritative.
        local history = game.waypoints()
        if history and not history[m.data.waypoint_to] then
          set_flag('wp.' .. m.data.waypoint_to, nil)
          act.key(27)
          return 'decide', game.level_name(m.data.waypoint_to) .. ' waypoint is not activated'
        end
        if now() - (m.data.sent or 0) > 3000 then
          act.waypoint(m.data.waypoint_to)
          m.data.sent = now()
        end
        return
      end
      if use_waypoint_object(m, me) then return end
      local spawn = waypoint_spawn(me.area)
      if not spawn then return 'decide', 'no waypoint here' end
      if mover.status ~= 'moving' then mover:to(spawn.x, spawn.y) end
      mover:tick()
    end,
  },

  clear = {
    enter = function(m) explorer = explore.new(game.me().area) end,
    tick = function(m)
      local me = game.me()
      if me.area ~= DEN then return 'decide' end
      if quest_done(QUEST_DEN) then
        print(string.format('Den of Evil cleared (level %d, %d kills)', me.level, combat.kills))
        trip:to(TOWN)
        return 'travel', 'back to Akara for the reward'
      end
      if reflexes(me, CLEAR_SIGHT) then return end
      -- Stragglers wander: sweep the Den again until the quest credits the clear.
      if explorer:tick(me) == 'done' then explorer = explore.new(DEN, true) end
    end,
  },

  -- Pick the most valuable area nearby, go there and clear it until the next
  -- goal is safe. Experience and time spent feed leveling.record.
  farm = {
    enter = function(m)
      local me = game.me()
      m.data.spent = m.data.spent or {}
      local hops = leveling.hops_from(me.area)
      local ahead = {}
      for _, hop in ipairs(travel.route(me.area, CATACOMBS_4) or {}) do ahead[hop.to] = true end
      local candidates, bonus = {}, {}
      m.data.farm_via = nil
      if flag('gold_farm') then
        candidates, m.data.farm_via = gold_farms(me)
      end
      if #candidates == 0 then
        -- Anywhere a known waypoint reaches (one hop past it), plus what is
        -- walkable from here; the planner weighs experience per second.
        candidates, m.data.farm_via = gold_farms(me, -ENTER_ABOVE)
        local listed = {}
        for _, level in ipairs(candidates) do
          listed[level] = true
          if ahead[level] then bonus[level] = 0.3 end
        end
        for level, distance in pairs(hops) do
          if listed[level] then distance = math.huge end
          if distance <= 3 and not NOT_FARMED[level] and level ~= DEN then
            candidates[#candidates + 1] = level
            -- Clearing an area on the route also clears the way.
            if ahead[level] then bonus[level] = 0.3 end
          end
        end
      end
      local area, why = leveling.choose(me, build, { candidates = candidates, bonus = bonus, spent = m.data.spent })
      -- Stick with a choice until it is cleared or we are ready to move on;
      -- re-choosing after each waypoint hop let travel cost undo the choice.
      local kept = m.data.farm_commit
      if kept and kept.mode == (flag('gold_farm') and 'gold' or 'xp') and not m.data.spent[kept.area] then
        area, why = kept.area, game.level_name(kept.area) .. ' (continuing)'
      end
      m.data.farm_commit = area and { area = area, mode = flag('gold_farm') and 'gold' or 'xp' } or nil
      m.data.farm_area, m.data.farm_since, m.data.farm_xp, explorer = area, nil, nil, nil
      -- Out in the field and far from it: walk to a known waypoint first when
      -- that (walk + jump + the walk on from its waypoint) saves two levels.
      m.data.farm_wp_walk = nil
      local via = area and m.data.farm_via and m.data.farm_via[area]
      if area and via and not me.in_town and hops[area] then
        local onward = leveling.hops_from(via)[area] or math.huge
        local best, best_cost = nil, hops[area]
        for wp in pairs(known_waypoints()) do
          local cost = (hops[wp] or math.huge) + 1 + onward
          if wp ~= via and wp ~= TOWN and cost + 2 <= best_cost then best, best_cost = wp, cost end
        end
        m.data.farm_wp_walk = best
      end
      if area then
        print('farming ' .. why)
        if area ~= me.area then trip:to(area) end
      end
    end,
    tick = function(m)
      local me = game.me()
      local area = m.data.farm_area
      if not area then
        m.data.override = true
        return 'decide', 'nothing worth farming nearby'
      end
      -- From town, or from a waypoint on the way, a far farm is reached by
      -- its waypoint.
      local via = m.data.farm_via and m.data.farm_via[area]
      local wp_walk = m.data.farm_wp_walk
      if via and me.area ~= area and me.area ~= via and (me.in_town or me.area == wp_walk) then
        m.data.farm_wp_walk = nil
        m.data.waypoint_to = via
        return 'take_waypoint', 'waypoint to ' .. game.level_name(via) .. ' for ' .. game.level_name(area)
      end
      if wp_walk and me.area ~= wp_walk and not me.in_town then
        if reflexes(me, TRAVEL_SIGHT) then return end
        if trip.status ~= 'moving' or trip.target ~= wp_walk then trip:to(wp_walk) end
        if trip:tick() == 'failed' then m.data.farm_wp_walk = nil end
        return
      end
      if reflexes(me, CLEAR_SIGHT) then return end
      if me.area ~= area then
        if trip.status ~= 'moving' or trip.target ~= area then trip:to(area) end
        if trip:tick() == 'failed' then
          m.data.spent[area] = true
          return 'decide', 'cannot reach ' .. game.level_name(area)
        end
        return
      end
      explorer = explorer or explore.new(area)
      m.data.farm_since = m.data.farm_since or now()
      m.data.farm_xp = m.data.farm_xp or me.experience
      local goal = next_goal(me)
      if flag('gold_farm') then goal = supplies(me) >= SUPPLY_OK end
      local exhausted = explorer:tick(me) == 'done'
      if goal or exhausted then
        leveling.record(area, me.experience - m.data.farm_xp, (now() - m.data.farm_since) / 1000)
        if exhausted then m.data.spent[area] = true end
        m.data.farm_commit = nil
        return 'decide', string.format('%s at level %d', exhausted and 'area cleared' or 'ready', me.level)
      end
    end,
  },

  -- Town portal out of trouble: free heal and restock, then back through our portal.
  portal_heal = {
    enter = function(m)
      m.data.from_area, m.data.cast_at, m.data.used_at, m.data.visit_done = game.me().area, nil, 0, false
      m.data.back_since, m.data.town_portal = nil, nil
    end,
    tick = function(m)
      local me = game.me()
      -- Ours by owner name; the name arrives in a separate message, so until
      -- then a blue portal (class 59) leading back to where we came from counts.
      local function our_portal()
        return world.nearest(game.units('object'), me, function(o)
          if not o.portal then return false end
          if o.owner_name == me.name then return true end
          return o.class_id == 59 and o.owner_name == '' and (not me.in_town or o.destination == m.data.from_area)
        end)
      end
      if now() - (m.data.status_at or 0) > 5000 then
        m.data.status_at = now()
        local portal = our_portal()
        print(string.format('portal_heal: in %s, portal %s, life %.0f/%.0f, visit %s, just portaled %s',
          game.level_name(me.area),
          portal and string.format('%.1f away (mode %d)', world.distance(me, portal), portal.mode) or 'not seen',
          me.life, me.max_life, tostring(m.data.visit_done), tostring(game.state('player', me.id, 102))))
      end
      if me.in_town then
        -- Remember where our portal stands: the client only knows units near
        -- the player, and the visit takes us out of its range.
        local seen = our_portal()
        if seen then m.data.town_portal = { x = seen.x, y = seen.y } end
        if not m.data.visit_done then
          -- Tick the visit to the end: its last steps close the shop properly.
          if visit:tick(me) ~= 'done' then return end
          visit = services.new(POTIONS, POTION_ORDER)
          m.data.visit_done = true
        end
        -- Healed and restocked: back through our portal to where we were. If it
        -- will not take us, a waypoint gets us most of the way.
        local portal = our_portal()
        if not portal then
          local spot = m.data.town_portal
          if not spot then return 'decide', 'no portal back; walking' end
          -- Walk back to where it stood until it is in view again.
          if world.distance(me, spot) < 3 then
            m.data.town_portal = nil
            return 'decide', 'portal is gone; walking'
          end
          if mover.status ~= 'moving' then mover:to(spot.x, spot.y) end
          mover:tick()
          return
        end
        if world.distance(me, portal) > 3 then
          if mover.status ~= 'moving' then mover:to(portal.x, portal.y) end
          mover:tick()
        else
          m.data.back_since = m.data.back_since or now()
          local waited = now() - m.data.back_since
          if waited > 10000 then
            -- Step off so the failed click leaves no pending interaction behind.
            act.move(math.floor(me.x) + 4, math.floor(me.y) + 4, stamina.walk(me))
            return 'decide', 'portal will not take us back; using a waypoint'
          end
          if now() - m.data.used_at > 1500 then
            if waited < 3000 then
              print('portal_heal: interact (client click)')
              act.interact(portal)
            else
              print('portal_heal: interact (raw request)')
              local id = portal.id
              act.send(string.char(0x13, 2, 0, 0, 0, id % 256, math.floor(id / 256) % 256,
                math.floor(id / 65536) % 256, math.floor(id / 16777216) % 256))
            end
            m.data.used_at = now()
          end
        end
        return
      end
      if me.area == m.data.from_area and m.data.visit_done then
        return 'decide', 'back from town'
      end
      local portal = our_portal()
      if portal then
        if world.distance(me, portal) > 3 then
          if act.ready() and now() - m.data.used_at > 400 then
            act.move(math.floor(portal.x), math.floor(portal.y), stamina.walk(me))
            m.data.used_at = now()
          end
        elseif now() - m.data.used_at > 1500 then
          act.interact(portal)
          m.data.used_at = now()
        end
        return
      end
      if not m.data.cast_at then
        for _, item in ipairs(game.items()) do
          if item.location == 'container' and item.page == 0 and item.code == 'tsc' then
            act.use_item(item)
            m.data.cast_at = now()
            return
          end
        end
        return 'retreat', 'no town portal scroll'
      elseif now() - m.data.cast_at > 4000 then
        m.data.cast_at = nil -- the portal never appeared; try again
      end
      -- Keep monsters off while the portal opens.
      if combat:threat(me, 5) then combat:fight(me, 5) end
    end,
  },

  retreat = {
    enter = function(m) trip:to(TOWN) end,
    tick = function(m)
      local me = game.me()
      if me.in_town then return 'decide', 'back in town' end
      -- Fight whatever blocks or chases us; otherwise keep walking.
      if combat:threat(me, 6) then
        combat:fight(me, 6)
        return
      end
      if trip:tick() == 'failed' then trip:to(TOWN) end
    end,
  },

  -- Tower Cellar Level 5: sweep it, kill the Countess, pick up her drop.
  countess = {
    enter = function(m) explorer = explore.new(COUNTESS_CELLAR, true) end,
    tick = function(m)
      local me = game.me()
      if me.area ~= COUNTESS_CELLAR then return 'decide' end
      local q = game.quest(QUEST_TOWER)
      local her = world.nearest(game.units('monster'), me, function(u) return u.name == 'The Countess' end)
      if her and not her.alive then m.data.countess_dead = true end
      if q and q.completed_this_game then m.data.countess_dead = true end
      if m.data.countess_dead then
        -- Her gold scatters widely: sweep it all up before leaving.
        if reflexes(me, CLEAR_SIGHT) then return end
        if gather:nearby(me) then
          gather:tick(me)
          return
        end
        m.data.countess_done = true
        return 'decide', 'the Countess is dead'
      end
      if her and her.alive and world.distance(me, her) < 25 then
        combat:fight(me, 25)
        return
      end
      if reflexes(me, CLEAR_SIGHT) then return end
      if explorer:tick(me) == 'done' then
        m.data.countess_done = true
        return 'decide', 'cellar swept without finding the Countess'
      end
    end,
  },

  andariel = {
    tick = function(m)
      local me = game.me()
      if me.area ~= CATACOMBS_4 then return 'decide' end
      if quest_done(QUEST_ANDARIEL) then return 'done', 'Andariel is dead' end
      local andariel = world.nearest(game.units('monster'), me, function(u) return u.class_id == ANDARIEL_CLASS end)
      if andariel and andariel.alive and world.distance(me, andariel) < 30 then
        combat:fight(me, 40)
        return
      end
      if reflexes(me, TRAVEL_SIGHT) then return end
      if not m.data.lair then
        for _, preset in ipairs(map.presets(CATACOMBS_4)) do
          if preset.type == 'monster' and preset.class_id == ANDARIEL_CLASS then m.data.lair = preset end
        end
        if not m.data.lair then return 'failed', "Andariel's spawn is not in the map data" end
      end
      if mover:tick() ~= 'moving' then mover:to(m.data.lair.x, m.data.lair.y) end
    end,
  },

  -- Death: ESC brings us back to life in town; the corpse stays where we fell.
  -- Instead of a naked walk back, save and exit: in single player the corpse
  -- waits in town when the character re-enters (lib campaign re-enters).
  dead = {
    enter = function(m)
      local me = game.me()
      set_flag('corpse_area', me.area)
      set_flag('deaths', (flag('deaths') or 0) + 1)
      print(string.format('died in %s at level %d', game.level_name(me.area), me.level))
    end,
    tick = function(m)
      local me = game.me()
      if me.alive then return 'decide', 'back in town' end
      if me.mode == 17 and m:elapsed() > 1500 and now() - (m.data.esc or 0) > 2000 then
        act.key(27)
        m.data.esc = now()
      end
    end,
  },

  -- Save and exit: the next game starts in town (lib campaign re-enters), with
  -- waypoints kept. Used to bring a corpse to town and as the fastest retreat.
  -- Pick up everything worth having nearby, then go on to bot.data.after.
  -- Leaves early when monsters close in (no mana to fight them) or after 30 s.
  collect = {
    tick = function(m)
      local me = game.me()
      local done = not gather:nearby(me) or m:elapsed() > 30000
      if done or #combat:hostiles(me, 5) > 0 then
        return m.data.after or 'decide', (m.data.after_why or 'done') ..
          (done and '' or ', monsters closing in')
      end
      gather:tick(me)
    end,
  },

  save_exit = {
    enter = function(m, from)
      if from == 'corpse' then set_flag('corpse_exits', (flag('corpse_exits') or 0) + 1) end
      print('save and exit (' .. tostring(from) .. ')')
      if game.ui('escape_menu') then act.key(27) end
    end,
    tick = function(m)
      if m:elapsed() > 300 and not m.data.left then
        m.data.left = act.leave_game()
      end
    end,
  },

  corpse = {
    enter = function(m) m.data.grabs, explorer = 0, nil end,
    tick = function(m)
      local me = game.me()
      local area = flag('corpse_area')
      local corpse = world.nearest(game.units('player'), me, function(u) return u.name == me.name and u.id ~= me.id end)
      if not corpse then
        -- Corpse elsewhere: one save and exit should bring it to town.
        if me.in_town and me.area ~= area and (flag('corpse_exits') or 0) < 1 then return 'save_exit' end
        if me.area ~= area then
          if trip.status ~= 'moving' or trip.target ~= area then trip:to(area) end
          if reflexes(me, TRAVEL_SIGHT) then return end
          if trip:tick() == 'failed' then trip:to(area) end
          return
        end
        if m.data.grabs > 0 or m:elapsed() > 120000 then
          set_flag('corpse_area', nil)
          set_flag('corpse_exits', nil)
          return 'decide', m.data.grabs > 0 and 'corpse recovered' or 'corpse not found'
        end
        if reflexes(me, CLEAR_SIGHT) then return end
        explorer = explorer or explore.new(area)
        explorer:tick(me)
        return
      end
      if world.distance(me, corpse) > 3 then
        if reflexes(me, TRAVEL_SIGHT) then return end
        if mover.status ~= 'moving' then mover:to(corpse.x, corpse.y) end
        mover:tick()
      elseif now() - (m.data.grab or 0) > 1500 then
        act.interact(corpse)
        m.data.grab, m.data.grabs = now(), m.data.grabs + 1
        if m.data.grabs > 6 then
          set_flag('corpse_area', nil)
          return 'decide', 'could not pick up the corpse'
        end
      end
    end,
  },

  done = {
    enter = function(m)
      local me = game.me()
      print(string.format('Act 1 done: Andariel killed at level %d, %.0f s this game, %d deaths',
        me.level, (now() - started) / 1000, flag('deaths') or 0))
      store.set('act1.finished', 'done')
    end,
  },

  failed = {
    enter = function(m) store.set('act1.finished', 'failed') end,
  },
}, { name = 'act1' })

function tick()
  if not game.ready() then return end
  local me = game.me()
  if bot:is('done') or bot:is('failed') then return end
  if not me.alive then
    if not bot:is('dead') then bot:go('dead', 'died') end
    return bot:tick()
  end
  -- Frozen client: unable to act for seconds on end (stuck in an animation
  -- after a bad interaction) only ends with leaving the game.
  if me.can_act or me.in_town or bot:is('save_exit') then
    stall.frozen_since = nil
  else
    stall.frozen_since = stall.frozen_since or now()
    if now() - stall.frozen_since > FROZEN_MS then
      stall.frozen_since = nil
      print(string.format('frozen in mode %d for %d s', me.mode, FROZEN_MS / 1000))
      return bot:go('save_exit', 'client frozen')
    end
  end
  -- Stall watchdog: no movement, area change or experience for a minute means
  -- a logic hole. Re-plan; after three stalls in a row, save and exit to reset.
  local mark = string.format('%d:%d:%d:%d', me.area, math.floor(me.x / 3), math.floor(me.y / 3), me.experience)
  if mark ~= stall.mark then
    stall.mark, stall.since = mark, now()
    if now() - (stall.last or 0) > 120000 then stall.count = 0 end
  elseif now() - stall.since > 60000 then
    stall.count, stall.since, stall.last = stall.count + 1, now(), now()
    local function count(kind)
      local n = 0
      for _, item in ipairs(game.items()) do
        if item.code and item.code:sub(1, 2) == kind and (item.location == 'belt' or (item.location == 'container' and item.page == 0)) then n = n + 1 end
      end
      return n
    end
    print(string.format('stalled in state %s (%s) for a minute: life %d/%d, mana %d/%d, resting %s, potions hp %d mp %d rv %d, target %s, mover %s/%s, trip %s',
      bot.state, game.level_name(me.area), me.life, me.max_life, me.mana, me.max_mana, tostring(combat.resting),
      count('hp'), count('mp'), count('rv'), tostring(combat.target_id), tostring(mover.status), tostring(mover.reason),
      tostring(trip.status)))
    if explorer then
      local t = explorer.target
      print(string.format('  explorer: target %s, mover %s/%s, path %s, loot item %s/%s, container %s/%s',
        t and string.format('%.0f,%.0f (%.0f away)', t.x, t.y, world.distance(me, t)) or 'none',
        tostring(explorer.mover.status), tostring(explorer.mover.reason), tostring(explorer.mover.path and #explorer.mover.path),
        tostring(loot.item), tostring(loot.mover.status), tostring(containers.object), tostring(containers.mover.status)))
    end
    if stall.count >= 3 and not bot:is('save_exit') then
      stall.count = 0
      return bot:go('save_exit', 'stalled three times')
    end
    return bot:go('decide', 'stalled')
  end
  combat:upkeep(me)
  -- Melee is a fine backup in easy areas (Blood Moor, Den of Evil, monster level
  -- 2 or lower); elsewhere running dry means restocking.
  local area = game.level_info(me.area)
  combat.melee_ok = me.area == 2 or me.area == DEN or (area ~= nil and area.monster_level <= 2)
  local life = me.max_life > 0 and me.life / me.max_life or 1
  local hurt = life < RETREAT_LIFE and not world.belt_slot('hp') and not world.belt_slot('rv')
    and not world.inventory_potion('hp') and not world.inventory_potion('rv')
  local dry = combat:out_of_mana()
  -- Chicken: a fight going wrong ends with leaving the game, which takes no
  -- cast and cannot be blocked; potions and portals are too slow once a pack
  -- is on us. A new game and a waypoint bring us back.
  local t = now()
  stall.life = stall.life or {}
  table.insert(stall.life, { at = t, life = life })
  while stall.life[1] and t - stall.life[1].at > BURST_MS do table.remove(stall.life, 1) end
  local peak = life
  for _, sample in ipairs(stall.life) do peak = math.max(peak, sample.life) end
  local close, near = #combat:hostiles(me, 5), #combat:hostiles(me, 8)
  local chicken
  if life < CHICKEN_CROWDED and close >= 2 then
    chicken = string.format('%d%% life with %d monsters close', math.floor(life * 100), close)
  elseif life < CHICKEN_LOW and near >= 1 then
    chicken = string.format('%d%% life with monsters near', math.floor(life * 100))
  elseif peak - life >= CHICKEN_BURST and life < BURST_BELOW and near >= 1 then
    chicken = string.format('lost %d%% life in %d s', math.floor((peak - life) * 100), math.floor(BURST_MS / 1000))
  end
  if chicken and not me.in_town and not bot:is('save_exit') then
    stall.life = {}
    bot:go('save_exit', 'chicken: ' .. chicken)
  elseif (hurt or dry) and not me.in_town and not bot:is('retreat') and not bot:is('portal_heal')
    and not bot:is('save_exit') then
    local why = hurt and 'low life and no potions' or 'out of mana and mana potions'
    local route = travel.route(me.area, TOWN)
    local after
    combat.resting = false
    if services.portal_scrolls() > 0 then
      after = 'portal_heal'
    elseif route and #route > 1 then
      -- Town is far: save and exit is the instant way back; a waypoint returns us.
      after, why = 'save_exit', why .. ', town is far'
    else
      after = 'retreat'
    end
    -- Out of mana but healthy: first pick up what the fight dropped. That gold
    -- and loot is what buys the next round of potions.
    if not hurt and not bot:is('collect') and gather:nearby(me) then
      bot.data.after, bot.data.after_why = after, why
      bot:go('collect', why .. '; collecting loot first')
    elseif not bot:is('collect') then
      bot:go(after, why)
    end
  end
  bot:tick()
end
