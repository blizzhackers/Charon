-- Fighting, potions and point spending for in-game scripts, one tick at a time.
--
--   local combat = require('lib.combat').new(builds.for_class(game.me().class))
--   function tick()
--     local me = game.me()
--     combat:upkeep(me)                       -- potions, stat and skill points
--     if combat:threat(me, 20) then combat:fight(me, 20) end
--   end
--
-- Melee builds walk up and hit. Ranged builds cast from `build.range` and, when
-- monsters close in, move to the safest reachable spot (map.safe_spot) if one
-- is clearly safer; when trapped, or when moving gains nothing, they fight.

local move = require 'lib.move'
local stamina = require 'lib.stamina'
local value = require 'lib.value'
local world = require 'lib.world'

local combat = {}
combat.__index = combat

local POTION_LIFE, REJUV_LIFE, POTION_MANA = 0.70, 0.30, 0.25
local STUCK_PROGRESS = 15000 -- target life unchanged this long: try another angle
local MISSED_CASTS = 3       -- missiles in a row that left the target's life unchanged
local REPOSITION_MS = 3000

function combat.new(build)
  return setmetatable({
    build = build,
    ignored = {},        -- unit id -> time until which it is skipped
    kills = 0,
    last_potion = 0, last_mana = 0, last_points = 0, last_action = 0,
    misses = 0, miss_rounds = 0, -- casts at the target since its life last dropped
    mover = move.new({ step = 10 }),
  }, combat)
end

local function now() return charon.now() end

function combat:known_skills()
  local known = {}
  for _, skill in ipairs(game.skills()) do
    if skill.name ~= '' then known[skill.name] = skill end
  end
  return known
end

-- Next skill to put a point in, following the build order and learning
-- prerequisites first.
function combat:next_skill(me)
  local ranks = {}
  for _, skill in ipairs(game.skills()) do ranks[skill.id] = skill.base_level end
  local by_name, by_id = {}, {}
  for _, info in ipairs(game.class_skills()) do by_name[info.name], by_id[info.id] = info, info end
  local function learnable(info, depth)
    if depth > 4 or info.required_level > me.level or (ranks[info.id] or 0) >= info.max_level then return nil end
    for _, required in ipairs(info.prerequisites) do
      if (ranks[required] or 0) == 0 then
        return by_id[required] and learnable(by_id[required], depth + 1)
      end
    end
    return info
  end
  local wanted = {}
  for _, name in ipairs(self.build.skills) do
    wanted[name] = (wanted[name] or 0) + 1
    local info = by_name[name]
    if info and (ranks[info.id] or 0) < wanted[name] then
      local pick = learnable(info, 0)
      if pick then return pick end
    end
  end
end

-- Whether the points already spent stray from the build order: a skill the
-- order never takes, or more ranks than it wants by now. A reset fixes that.
function combat:off_build()
  local wanted = {}
  for _, name in ipairs(self.build.skills) do wanted[name] = (wanted[name] or 0) + 1 end
  for _, skill in ipairs(game.skills()) do
    -- base_level counts spent points only (item bonuses excluded); class -1
    -- is the shared basics (Attack, Throw, scrolls).
    local info = game.skill_info(skill.id)
    if info and info.class_id >= 0 and skill.base_level > (wanted[skill.name] or 0) then return true, skill.name end
  end
  return false
end

function combat:spend_points(me)
  -- A backlog (after a reset) is spent quickly; single level-up points slowly.
  local gap = (me.stat_points + me.skill_points) > 4 and 250 or 1500
  if now() - self.last_points < gap then return end
  if me.stat_points > 0 then
    local stats = self.build.stats
    act.add_stat(stats[(me.stat_points - 1) % #stats + 1], 1)
    self.last_points = now()
  elseif me.skill_points > 0 then
    local info = self:next_skill(me)
    if info then
      print('learning ' .. info.name)
      act.add_skill(info.id)
      self.last_points = now()
    end
  end
end

-- Uses a belt slot, or the inventory when the belt has none of that kind.
local function use_potion(prefix)
  local slot = world.belt_slot(prefix)
  if slot then
    act.use_belt(slot)
    return true
  end
  local item = world.inventory_potion(prefix)
  if item then
    act.use_item(item)
    return true
  end
  return false
end

function combat:drink(me)
  -- Town is safe and the healer (services) restores life and mana for free.
  if me.in_town then return end
  local life = me.max_life > 0 and me.life / me.max_life or 1
  if life < POTION_LIFE and now() - self.last_potion > 1200 then
    if (life < REJUV_LIFE and use_potion('rv')) or use_potion('hp') or use_potion('rv') then
      self.last_potion = now()
    end
  end
  local mana = me.max_mana > 0 and me.mana / me.max_mana or 1
  -- Rejuvenations refill mana too: drink one once the mana potions are gone.
  if mana < POTION_MANA and now() - self.last_mana > 1500 and (use_potion('mp') or use_potion('rv')) then
    self.last_mana = now()
  end
end

-- Moves inventory potions into the belt between fights (the server fills free slots).
function combat:refill_belt(me)
  if now() - (self.last_refill or 0) < 3000 then return end
  self.last_refill = now()
  self.belted = self.belted or {}
  for _, item in ipairs(game.items()) do
    local kind = item.code and #item.code == 3 and item.code:sub(1, 2)
    if item.location == 'container' and item.page == 0 and not self.belted[item.id]
      and (kind == 'hp' or kind == 'mp' or kind == 'rv') then
      self.belted[item.id] = true
      act.to_belt(item)
      return
    end
  end
end

-- Recasts a self-buff (build.buffs) whose state has run out, when nothing is
-- in our face and the mana is there. True when a cast (or the skill swap
-- before it) was issued this tick.
local BUFF_RETRY = 2000 -- after a cast, wait this long for the state to show
function combat:buff(me)
  local buffs = self.build.buffs
  if not buffs or me.in_town or self.resting then return false end
  if not act.ready() or now() - self.last_action < 150 then return false end
  if #self:hostiles(me, 5) > 0 then return false end
  self.buff_tried = self.buff_tried or {}
  local known
  for _, name in ipairs(buffs) do
    known = known or self:known_skills()
    local skill = known[name]
    if skill and skill.level > 0 and skill.usable and me.mana >= skill.mana
      and now() >= (self.buff_tried[name] or 0) then
      local info = game.skill_info(skill.id)
      local state = info and info.state or 0
      if state > 0 and not game.state('player', me.id, state) then
        if me.right_skill ~= skill.id then
          act.select_skill('right', skill.id)
        else
          act.cast('right', math.floor(me.x), math.floor(me.y))
          self.buff_tried[name] = now() + BUFF_RETRY
          print(string.format('combat: casting %s', name))
        end
        self.last_action = now()
        return true
      end
    end
  end
  return false
end

function combat:upkeep(me)
  if not me.alive then return end
  self:spend_points(me)
  self:drink(me)
  if not self:threat(me, 12) then self:refill_belt(me) end
  self:buff(me)
end

-- Spells are out of reach: resting for mana with no mana potion left to drink.
function combat:out_of_mana()
  return self.resting and not world.belt_slot('mp') and not world.inventory_potion('mp')
    and not world.belt_slot('rv') and not world.inventory_potion('rv')
end

-- Whether `unit` can reach us or be hit now: in line of sight (walls and
-- closed doors block it), or practically touching. A pack behind a wall is
-- not a threat until it walks round into view; counting it made us flee from,
-- and chase, monsters we could neither hit nor be hit by.
local SIGHT_CACHE_MS = 250
local TOUCHING = 3
function combat:in_sight(unit)
  if not unit.x then return false end
  local t = now()
  if not self.me_at or t - self.me_at > 50 then self.me, self.me_at = game.me(), t end
  local me = self.me
  if world.distance(me, unit) <= TOUCHING then return true end
  self.sight = self.sight or {}
  local cached = self.sight[unit.id]
  if cached and t - cached.at < SIGHT_CACHE_MS then return cached.seen end
  local ok, seen = pcall(map.line_of_sight, me.x, me.y, unit.x, unit.y)
  seen = not ok or seen -- no answer (between rooms): count it, as before
  self.sight[unit.id] = { at = t, seen = seen }
  return seen
end

function combat:usable(unit)
  return world.hostile(unit) and (self.ignored[unit.id] or 0) < now() and self:in_sight(unit)
end

-- Best hostile to fight within `radius` (see value.priority), or nil.
function combat:threat(me, radius)
  local best, best_score
  for _, unit in ipairs(game.units('monster')) do
    if self:usable(unit) and unit.x and world.distance(me, unit) <= radius then
      local score = value.priority(unit, me, self.build.element)
      -- Immune monsters only matter when they are in our face.
      if score >= 0 or world.distance(me, unit) < 4 then
        if not best_score or score > best_score then best, best_score = unit, score end
      end
    end
  end
  return best
end

local EVADE_GAIN = 0.35      -- move only when the spot is at least this much safer
local EVADE_TIMEOUT = 2500   -- give up on a planned move after this long
local EVADE_STREAK = 3       -- evasions in a row before casting again
local EVADE_COOLDOWN = 1500  -- stand and fight this long after an evasion that gained nothing
local OPEN_WAYS = 4          -- move early once this few directions are still open

-- Hostiles within `radius`.
function combat:hostiles(me, radius)
  local list = {}
  for _, unit in ipairs(game.units('monster')) do
    if self:usable(unit) and unit.x and world.distance(me, unit) <= radius then list[#list + 1] = unit end
  end
  return list
end

-- Threats for map.safe_spot: elites count for more; ranged attackers less,
-- since stepping away from an archer does not take us out of its reach.
function combat:threats(me)
  local threats = {}
  for _, unit in ipairs(self:hostiles(me, 16)) do
    local weight = (unit.unique or unit.superunique or unit.boss) and 3 or unit.champion and 2 or 1
    if unit.ranged then weight = weight * 0.5 end
    threats[#threats + 1] = { x = unit.x, y = unit.y, weight = weight }
  end
  return threats
end

-- Steps along the planned route to the safe spot. False once the plan is over.
function combat:follow_plan(me)
  local plan = self.plan
  if not plan then return false end
  local arrived = world.distance(me, plan) < 1.8
  if arrived or now() - plan.since > EVADE_TIMEOUT then
    self.plan = nil
    -- Judge the move: if danger did not drop (monsters kept up, or the way
    -- closed), stand and fight for a while instead of running again.
    local spot = map.safe_spot({ threats = self:threats(me), radius = 2 })
    local danger_now = spot and spot.danger_here or plan.from
    local worked = danger_now <= plan.from - EVADE_GAIN / 2
    if not worked then self.no_evade_until = now() + EVADE_COOLDOWN end
    print(string.format('evade (%s): danger %.2f -> %.2f (%s)', plan.why, plan.from, danger_now,
      worked and 'safer' or 'no gain, standing to fight'))
    return false
  end
  -- Head for the furthest route cell with a clear straight line.
  local point = plan.path[#plan.path]
  for i = #plan.path, 1, -1 do
    local p = plan.path[i]
    if world.distance(me, p) <= 8 and map.line_clear(me.x, me.y, p.x, p.y) then
      point = p
      break
    end
  end
  if now() - self.last_action > 250 then
    act.move(point.x, point.y, stamina.walk(me))
    self.last_action = now()
  end
  return true
end

-- Moves to the safest reachable spot nearby (map.safe_spot: a danger field
-- around every hostile, cells beside them and slim gaps excluded, monsters
-- assumed to keep closing in), keeping `target` in range. Moves when crowded,
-- or early when few directions are still open, so the way out is not cut off
-- first. False when trapped or when moving gains nothing: then the caller fights.
function combat:evade(me, target, nearby, close, hurt)
  if self:follow_plan(me) then return true end
  if not map.safe_spot or now() < (self.no_evade_until or 0) or (self.evade_streak or 0) >= EVADE_STREAK then
    return false
  end
  if nearby < 2 and close == 0 then return false end
  if now() - (self.last_check or 0) < 300 then return false end
  self.last_check = now()
  local spot = map.safe_spot({
    threats = self:threats(me),
    target = target and { x = target.x, y = target.y },
    range = self.build.range,
  })
  if not spot or not spot.found or #spot.path == 0 then return false end
  local crowded = close >= 2 or (close == 1 and hurt)
  local closing = nearby >= 2 and spot.open_sectors <= OPEN_WAYS
  if not crowded and not closing then return false end
  local gain = spot.danger_here - spot.danger_there
  if gain < (crowded and EVADE_GAIN or 0.15) then return false end
  self.plan = { x = spot.x, y = spot.y, path = spot.path, since = now(), from = spot.danger_here,
    why = crowded and 'crowded' or ('closing in, ' .. spot.open_sectors .. ' ways out') }
  self.evade_streak = (self.evade_streak or 0) + 1
  return self:follow_plan(me)
end

-- A spot to shoot `target` from after missing it: on a ring around it with a
-- clear line to it, nearer each time we keep missing, away from other
-- hostiles, and as close to us as possible. Nil when no such spot exists.
function combat:firing_spot(me, target)
  local base = math.min(world.distance(me, target), self.build.range)
  -- Bolts wander, so closer is what helps: halfway in at once, then point blank.
  local radius = math.max(2.5, base - 2.5 * self.miss_rounds)
  local others = self:hostiles(me, 20)
  local best, best_cost
  for i = 0, 15 do
    local angle = i * math.pi / 8
    local spot = { x = target.x + math.cos(angle) * radius, y = target.y + math.sin(angle) * radius }
    if world.distance(me, spot) >= 1.5 and map.walkable(spot.x, spot.y)
      and map.line_of_sight(spot.x, spot.y, target.x, target.y) then
      local cost, crowded = world.distance(me, spot), false
      for _, unit in ipairs(others) do
        if unit.id ~= target.id then
          local d = world.distance(unit, spot)
          if d < 2.5 then crowded = true elseif d < 5 then cost = cost + 3 end
        end
      end
      if not crowded and (not best_cost or cost < best_cost) then best, best_cost = spot, cost end
    end
  end
  return best
end

-- One step of fighting the nearest hostile within `radius`.
-- Returns 'fighting', 'resting' (waiting for mana) or 'clear'.
function combat:fight(me, radius)
  local target = self.target_id and game.unit('monster', self.target_id)
  if target and not target.alive then
    self.kills = self.kills + 1
    target = nil
  end
  if not target or not self:usable(target) then
    target = self:threat(me, radius)
    if not target then
      self.target_id = nil
      return 'clear'
    end
    self.target_id = target.id
    self.target_life, self.target_progress = target.life_percent, now()
    self.misses, self.miss_rounds = 0, 0
    self.mover:stop()
  end
  -- Stubborn targets (champions, uniques, bosses) just take longer; never give
  -- up on one we can hurt. If several missiles in a row (or a long while) left
  -- its life unchanged, they are being blocked or are scattering wide: move to
  -- a closer spot with a clear line and keep at it.
  if (target.life_percent or 100) < (self.target_life or 100) - 0.5 then
    self.target_life, self.target_progress = target.life_percent, now()
    self.misses, self.miss_rounds = 0, 0
  elseif self.misses >= MISSED_CASTS or now() - self.target_progress > STUCK_PROGRESS then
    self.target_progress, self.misses = now(), 0
    self.miss_rounds = self.miss_rounds + 1
    local spot = self:firing_spot(me, target)
    if spot and self.mover:to(spot.x, spot.y) == 'moving' then
      print(string.format('combat: missing %s (class %d, life %.0f%%, %s resist %d, mana %.0f/%.0f); moving to a closer angle (%.0f away)',
        tostring(target.name), target.class_id, target.life_percent or -1, self.build.element,
        value.resist(target, self.build.element), me.mana, me.max_mana, world.distance(spot, target)))
      self.reposition_until = now() + REPOSITION_MS
    end
  end
  if now() < (self.reposition_until or 0) then
    if self.mover:tick() == 'moving' then return 'fighting' end
    self.reposition_until = nil
  end

  local build = self.build
  local distance = world.distance(me, target)
  local close = {}
  for _, unit in ipairs(game.units('monster')) do
    if self:usable(unit) and world.distance(me, unit) < 4.5 then close[#close + 1] = unit end
  end
  local known = self:known_skills()
  local skill, hand, at = build.attack(me, target, known, distance, #close, self:hostiles(me, 12))
  -- Out of mana with spells learned: back off and let mana return rather than
  -- meleeing with a staff. Kites from anything close; otherwise holds still.
  -- Out of mana with spells learned. Where it is safe (combat.melee_ok, set by
  -- the caller per area) keep fighting in melee; elsewhere wait for mana, and the
  -- caller sends us to restock (combat:out_of_mana).
  if skill == 0 and build.has_spells and build.has_spells(known) then
    -- No spell chosen with mana to spare means the target resists them all.
    local affordable = false
    for _, skill_ in pairs(known) do
      if skill_.level > 0 and skill_.mana > 0 and skill_.usable and me.mana >= skill_.mana then affordable = true end
    end
    if affordable and not self.melee_ok then
      print(string.format('combat: skipping %s (immune to our spells)', tostring(target.name)))
      self.ignored[target.id] = now() + 60000
      self.target_id = nil
      return 'fighting'
    end
  end
  if self.melee_ok then
    self.resting = false
  elseif skill == 0 and build.has_spells and build.has_spells(known) then
    self.resting = true
  end
  if self.resting then
    if me.mana >= me.max_mana * 0.6 then
      self.resting = false
    elseif #close > 0 then
      if self:evade(me, nil, #self:hostiles(me, 10), #close, true) then return 'resting' end
    else
      -- Standing still under arrows only gets us shot: go and hit the archers.
      local shooter = world.nearest(self:hostiles(me, 14), me, function(u)
        return u.ranged and map.line_of_sight(u.x, u.y, me.x, me.y)
      end)
      if not shooter then return 'resting' end
      target, distance = shooter, world.distance(me, shooter)
      self.target_id = shooter.id
    end
  end
  -- Without a spell yet (no points spent) a ranged build fights in melee.
  local ranged = build.ranged and skill ~= 0
  local range = ranged and build.range or 2.8
  local hurt = me.life < me.max_life * 0.9
  if ranged and self:evade(me, target, #self:hostiles(me, 10), #close, hurt) then
    return 'fighting'
  end

  -- Point blank needs no check; otherwise a spell needs a missile path.
  local clear_line = distance <= 3 or map.line_of_sight(me.x, me.y, target.x, target.y)
  if distance > range or not clear_line then
    if clear_line then
      if now() - self.last_action > 300 and act.ready() then
        act.move(math.floor(target.x), math.floor(target.y), stamina.walk(me))
        self.last_action = now()
      end
    else
      if self.mover.status ~= 'moving' or now() - (self.planned or 0) > 1500 then
        self.mover:to(target.x, target.y)
        self.planned = now()
      end
      if self.mover:tick() == 'failed' then
        print(string.format('combat: skipping %s for 30 s (no way to reach it: %s)', tostring(target.name), tostring(self.mover.reason)))
        self.ignored[target.id] = now() + 30000
        self.target_id = nil
      end
    end
    return 'fighting'
  end

  if not act.ready() or now() - self.last_action < 150 then return 'fighting' end
  self.mover:stop()
  local current = hand == 'right' and me.right_skill or me.left_skill
  self.evade_streak = 0
  if current ~= skill then
    local name = 'attack'
    for name_, info in pairs(known) do
      if info.id == skill then name = name_ end
    end
    local near, chilled = self:hostiles(me, 12), 0
    for _, unit in ipairs(near) do
      if game.state('monster', unit.id, 11) then chilled = chilled + 1 end
    end
    print(string.format('combat: %s on %s (%d close, %d near, %d chilled, mana %.0f)', name, tostring(target.name),
      #close, #near, chilled, me.mana))
    act.select_skill(hand, skill)
  elseif at == 'self' then
    act.cast(hand, math.floor(me.x), math.floor(me.y))
    if build.on_cast then build.on_cast(skill, known) end
  else
    act.cast_at(hand, target)
    if ranged then self.misses = self.misses + 1 end
    if build.on_cast then build.on_cast(skill, known) end
  end
  self.last_action = now()
  return 'fighting'
end

return combat
