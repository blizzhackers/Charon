-- Fighting, potions and point spending for in-game scripts, one tick at a time.
--
--   local combat = require('lib.combat').new(builds.for_class(game.me().class))
--   function tick()
--     local me = game.me()
--     combat:upkeep(me)                       -- potions, stat and skill points
--     if combat:threat(me, 20) then combat:fight(me, 20) end
--   end
--
-- The authored neural policy ranks legal attacks and native tactical routes.
-- This module owns shared survival, target acquisition and action readiness.

local move = require 'lib.move'
local stamina = require 'lib.stamina'
local supplies = require 'lib.supplies'
local value = require 'lib.value'
local world = require 'lib.world'

local combat = {}
combat.__index = combat

local REJUV_LIFE, POTION_MANA = 0.30, 0.25
local HEALTH_FLOOR = 0.50 -- oversized potions remain available below or at half current life
local HEAL_FIRST_MS = 1000 -- how long the policy's word that a fight waits on her life stands
local HEALTH_POTION_STATE = 100 -- STATE_healthpot, headers/ghidra/enums.h
local HEALTH_PROJECTION = 74 -- client hpregen holds projected final life percent (health orb)
-- Fit the normal dose; unpredictable critical doubling is reflected in the later projection.
local HEALTH_DOSES, HEALTH_COEFFICIENT = supplies.HEALTH_DOSES, supplies.HEALTH_COEFFICIENT
local REJUV_PERCENT = supplies.REJUV_PERCENT
local WIN_MS = 500 -- how long the policy's last projection of the fight stays current
local INTAKE_MS = 2000 -- life lost is measured over this window: long enough for a few blows
combat.INTAKE_MS = INTAKE_MS -- the policy models the same seconds
local DRY_RESUME = 0.5 -- once dry, fights are sought again from this share of the mana pool
local MANA_POTION_STATE = 106 -- STATE_manapot, headers/ghidra/enums.h
local BELT_RETRY_MS = 3000 -- retry refused inventory transfers without flooding the belt
local ACTION_MS = 80        -- selection is synchronous; animation readiness governs cast cadence
local RETARGET_MS = 400     -- keep focus without chasing a stale target through a pack
local MISSILE_LEAD = 600    -- ms of observed flight to avoid while starting the next cast
local DODGE_CHECK_MS = 120  -- short native searches, at most eight per second
local ESCAPE_RETRY_MS = 400 -- let attacks proceed between refused native paths, then reassess changing traffic
local LOOT_WALK = 12       -- match nearby loot range; only straight walks, never planned detours
local LOOT_MELEE = 7       -- melee reach plus room for enemies to close during the walk
local LOOT_SIGHT = 24      -- match the nearby missile observation radius for ranged exposure
local LOOT_DOOR = 4        -- wide doors can block a corridor away from their center

function combat.new(build)
  local self = setmetatable({
    build = build,
    ignored = {},        -- unit id -> time until which it is skipped
    kills = 0,
    last_potion = 0, last_mana = 0, last_points = 0, last_action = 0,
    losses = {}, -- recent drops in life, for intake()
    misses = 0, -- casts at the target since its life last dropped
  }, combat)
  self.neural = require('lib.neural_combat').new()
  return self
end

local function now() return charon.now() end

function combat:known_skills()
  local known = {}
  for _, skill in ipairs(game.skills()) do
    if skill.name ~= '' and (skill.owner == nil or skill.owner == -1 or skill.owner == 4294967295) then
      known[skill.name] = skill
    end
  end
  return known
end

-- Next skill to put a point in, following the build order and learning
-- prerequisites first.
function combat:next_skill(me)
  local ranks = {}
  for _, skill in pairs(self:known_skills()) do ranks[skill.id] = skill.base_level end
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
      return learnable(info, 0) -- hold the point when the next planned skill is not ready
    end
  end
end

-- Whether the points already spent stray from the build order: a skill the
-- order never takes, or more ranks than it wants by now. A reset fixes that.
function combat:off_build()
  local ranks, spent = {}, 0
  for _, skill in pairs(self:known_skills()) do
    -- base_level counts spent points only (item bonuses excluded); class -1
    -- is the shared basics (Attack, Throw, scrolls).
    local info = game.skill_info(skill.id)
    if info and info.class_id >= 0 then
      ranks[skill.name] = skill.base_level
      spent = spent + skill.base_level
    end
  end
  local wanted = {}
  for i = 1, math.min(spent, #self.build.skills) do
    local name = self.build.skills[i]
    wanted[name] = (wanted[name] or 0) + 1
  end
  for name, rank in pairs(ranks) do
    if rank > (wanted[name] or 0) then return true, name end
  end
  return false
end

local STRENGTH = 0 -- act.add_stat's and the game's number for it
function combat:spend_points(me)
  -- A backlog (after a reset) is spent quickly; single level-up points slowly.
  local gap = (me.stat_points + me.skill_points) > 4 and 250 or 1500
  if now() - self.last_points < gap then return end
  if me.stat_points > 0 then
    local stats = self.build.stats
    -- Strength first, to what the build's armour asks for: by her own
    -- points, since what gear adds goes when the gear does.
    local goal = self.build.strength
    local own = goal and me.level >= (self.build.strength_level or 1)
      and game.base_stat and game.base_stat('player', me.id, STRENGTH) or nil
    if own and own < goal then
      act.add_stat(STRENGTH, 1)
    else
      act.add_stat(stats[(me.stat_points - 1) % #stats + 1], 1)
    end
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

-- Largest complete dose that fits, with emergency fallbacks when none does.
local function health_potion(me, gap, emergency, critical)
  local hp, strongest, first_hp, rv, first_rv
  local function before(item, other)
    if not other then return true end
    if item.location ~= other.location then return item.location == 'belt' end
    if item.location == 'belt' then return item.grid_x < other.grid_x end
    return item.id < other.id
  end
  local function better(candidate, current)
    return not current or candidate.amount > current.amount
      or (candidate.amount == current.amount and before(candidate.item, current.item))
  end
  for _, item in ipairs(game.items()) do
    if item.location == 'belt' or (item.location == 'container' and item.page == 0) then
      -- Only potions fit a belt, so an unlisted tier there is still a dose;
      -- loose inventory items must be exact potion codes.
      local kind = item.location == 'belt' and item.code and item.code:sub(1, 2) or world.potion_kind(item.code)
      if kind == 'hp' or kind == 'rv' then
        local amount
        if HEALTH_DOSES[item.code] and HEALTH_COEFFICIENT[me.class] then
          amount = HEALTH_DOSES[item.code] * HEALTH_COEFFICIENT[me.class]
        elseif REJUV_PERCENT[item.code] then
          amount = me.max_life * REJUV_PERCENT[item.code] / 100
        end
        local candidate = {item=item, amount=amount}
        -- Legacy emergency ordering: lowest belt slot, otherwise the first inventory item.
        local first
        if kind == 'hp' then first = first_hp else first = first_rv end
        if not first or (item.location == 'belt' and before(item, first.item)) then
          if kind == 'hp' then first_hp = candidate else first_rv = candidate end
        end
        if amount then
          if kind == 'hp' then
            if better(candidate, strongest) then strongest = candidate end
            if amount <= gap and better(candidate, hp) then hp = candidate end
          elseif amount <= gap and better(candidate, rv) then rv = candidate end
        end
      end
    end
  end
  local selected = (critical and first_rv) or hp or (emergency and (strongest or first_hp))
    or (emergency and first_rv) or rv
  if not selected then return false end
  local item = selected.item
  if item.location == 'belt' then act.use_belt(item.grid_x) else act.use_item(item) end
  print(string.format('combat: health potion request %s (normal %s, unfilled %.1f, %s)', item.code,
    selected.amount and string.format('%.1f', selected.amount) or 'unknown', gap,
    item.location == 'belt' and ('belt slot ' .. item.grid_x) or 'inventory'))
  return true
end

function combat:drink(me)
  -- Town is safe and the healer (services) restores life and mana for free.
  if me.in_town then return end
  local life = me.max_life > 0 and me.life / me.max_life or 1
  local health_active = (life < 1 or self.health_wait_logged)
    and game.state('player', me.id, HEALTH_POTION_STATE)
  if not health_active then self.health_wait_logged = nil end
  if life < 1 and now() - self.last_potion > 1200 then
    -- Below half, or when the staff work in hand waits on her life (the
    -- policy marks it each tick): then a potion though it heals more than
    -- she lacks. One at a time.
    local for_fight = self.heal_first ~= nil and now() - self.heal_first <= HEAL_FIRST_MS and not health_active
    local emergency = life <= HEALTH_FLOOR or for_fight
    local gap = math.max(0, me.max_life - me.life)
    if health_active and not emergency then
      local projected = game.stat('player', me.id, HEALTH_PROJECTION)
      -- State may arrive before its projection; inactive projections can remain stale.
      if type(projected) == 'number' and projected >= 0 and projected <= 100
        and math.min(100, projected + 1) >= life * 100 then
        local final = me.max_life * math.min(100, projected + 1) / 100
        gap = math.max(0, me.max_life - math.max(me.life, final))
      else
        gap = nil
      end
    end
    if gap and health_potion(me, gap, emergency, life < REJUV_LIFE) then
      self.last_potion = now()
    elseif health_active and not emergency and not self.health_wait_logged then
      print('combat: waiting for queued health recovery (' .. (gap and string.format('unfilled %.1f', gap) or 'projection pending') .. ')')
      self.health_wait_logged = true
    end
  end
  local mana = me.max_mana > 0 and me.mana / me.max_mana or 1
  -- A potion restores mana over seconds: the fight in hand that needs more than
  -- we hold is fed from its first cast, not from the moment the mana is gone.
  local ahead = self.mana_need and now() - (self.outlast_at or 0) <= WIN_MS and me.mana < self.mana_need
    and mana < 0.8
  local low = mana < POTION_MANA or ahead
  -- A boss in sight (the campaign says so each tick) takes more mana than the
  -- pool holds: Andariel was met at 38% mana with ten potions in the belt,
  -- and two casts of Static Field later there was nothing to cast with.
  local boss = self.boss_at and now() - self.boss_at <= WIN_MS and mana < 0.8
  -- More mana potions extend regeneration; rejuvenations restore mana
  -- instantly and are kept for when it is low.
  if (low or boss) and now() - self.last_mana > 1500
    and ((not game.state('player', me.id, MANA_POTION_STATE) and use_potion('mp')) or (low and use_potion('rv'))) then
    self.last_mana = now()
  end
end

-- Moves inventory potions into the belt between fights (the server fills free slots).
function combat:refill_belt(me)
  if now() - (self.last_refill or 0) < 3000 then return end
  self.last_refill = now()
  if world.starter_belt_full() then return end
  self.belted = self.belted or {}
  local next_item, next_retry
  for _, item in ipairs(game.items()) do
    local kind = world.potion_kind(item.code)
    local retry = self.belted[item.id] or 0
    if item.location == 'container' and item.page == 0 and now() >= retry
      and (kind == 'hp' or kind == 'mp' or kind == 'rv') then
      -- A refused potion must not monopolize every later refill tick.
      if not next_item or retry < next_retry then next_item, next_retry = item, retry end
    end
  end
  if next_item then
    self.belted[next_item.id] = now() + BELT_RETRY_MS
    act.to_belt(next_item)
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
          if not act.select_skill('right', skill.id) then
            self.buff_tried[name] = now() + BUFF_RETRY
            print('combat: could not select buff ' .. name)
          end
        else
          act.cast('right', math.floor(me.x), math.floor(me.y))
          self.escape_progress = nil
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

-- Life lost per second over the last INTAKE_MS. Only the drops between ticks
-- count: potions and regeneration raise life and must not hide the blows.
function combat:observe_life(me)
  local t = now()
  if self.life_seen and me.life < self.life_seen and not me.in_town then
    table.insert(self.losses, { at = t, loss = self.life_seen - me.life })
    self.lost_total = (self.lost_total or 0) + self.life_seen - me.life -- all of it, for value.observe
  end
  self.life_seen = me.life
  while self.losses[1] and t - self.losses[1].at > INTAKE_MS do table.remove(self.losses, 1) end
end

function combat:intake()
  local t, sum = now(), 0
  for _, entry in ipairs(self.losses) do
    if t - entry.at <= INTAKE_MS then sum = sum + entry.loss end
  end
  return sum / (INTAKE_MS / 1000)
end

function combat:upkeep(me)
  self:observe_life(me)
  if me.in_town then
    -- A town visit ends the field engagement, including its no-progress clocks.
    self.target_id, self.target_life, self.target_progress = nil, nil, nil
    self.plan, self.escape_progress, self.resting = nil, nil, false
    self.neural.wait_at, self.neural.wait_target = nil, nil
  end
  if not me.alive then return end
  self:observe_missiles(me)
  self:spend_points(me)
  self:drink(me)
  if not self:threat(me, 12) then self:refill_belt(me) end
  self:buff(me)
end

-- Whether the fight in hand is being won where we stand: the policy's own
-- projection leaves at least `margin` of what the fight may cost us.
function combat:winning(margin)
  return self.outlast ~= nil and now() - self.outlast_at <= WIN_MS and self.outlast >= margin
end

-- How much of our remaining life the monsters standing around `unit` would
-- take at close quarters, on the policy's scale (x/(1+x) of the share x lost
-- over PACK_SECONDS): what picking a fight with that pack costs before a cast.
local PACK_RADIUS, PACK_SECONDS = 8, 4
function combat:pack_lethality(me, unit)
  local rate = 0
  for _, other in ipairs(self:hostiles(me, 30)) do
    if world.distance(other, unit) <= PACK_RADIUS then rate = rate + value.dps(other) end
  end
  local share = rate * PACK_SECONDS / math.max(1, me.life)
  return share / (1 + share)
end

-- Whether the build has nothing to fight with: its attack is unaffordable and
-- there is no mana potion to drink or at work. Once dry it stays dry until
-- half the pool is back, so that the trickle of regeneration is not spent a
-- cast at a time in the middle of what it cannot kill.
function combat:dry(me)
  local stocked = supplies.mana() > 0 or game.state('player', me.id, MANA_POTION_STATE)
  if stocked or not self.attack_mana or (me.max_mana or 0) <= 0 then
    self.ran_dry = false
  elseif me.mana < self.attack_mana then
    self.ran_dry = true
  elseif self.ran_dry and me.mana >= me.max_mana * DRY_RESUME then
    self.ran_dry = false
  end
  return self.ran_dry or false
end

-- Spells are out of reach: resting for mana with no mana potion left to drink.
function combat:out_of_mana()
  local me = game.me()
  return self.resting and supplies.mana() == 0
    and not (me and game.state('player', me.id, MANA_POTION_STATE))
end

-- Whether `unit` can reach us or be hit now: in line of sight (walls and
-- closed doors block it). A pack behind a wall is
-- not a threat until it walks round into view; counting it made us flee from,
-- and chase, monsters we could neither hit nor be hit by.
local SIGHT_CACHE_MS = 250
function combat:in_sight(unit)
  if not unit.x then return false end
  local t = now()
  if not self.me_at or t - self.me_at > 50 then self.me, self.me_at = game.me(), t end
  local me = self.me
  self.sight = self.sight or {}
  local cached = self.sight[unit.id]
  if cached and t - cached.at < SIGHT_CACHE_MS then return cached.seen end
  local ok, seen = pcall(map.line_of_sight, me.x, me.y, unit.x, unit.y)
  seen = not ok or seen -- no answer (between rooms): count it, as before
  self.sight[unit.id] = { at = t, seen = seen }
  return seen
end

function combat:usable(unit)
  if not world.hostile(unit) then return false end
  if (self.ignored[unit.id] or 0) >= now() then
    -- One given up from beyond her reach (see the policy's wait) is hers
    -- again when it is within it: the search of a level walks up to what is
    -- left, and with the monster still set aside she arrived beside the last
    -- one in the Den, did nothing, and walked off, for four minutes.
    local me = self.unreached and self.unreached[unit.id] and unit.x and game.me()
    if not me or world.distance(me, unit) > self.build.range then return false end
    self.ignored[unit.id], self.unreached[unit.id] = nil, nil
  end
  return self:in_sight(unit)
end

-- Reuse one client snapshot throughout a tick; ignored targets still threaten us.
function combat:monsters()
  local frame = charon.frame and charon.frame() or now()
  if self.monster_frame ~= frame then
    self.monster_frame, self.monster_list = frame, game.units('monster')
  end
  return self.monster_list
end

-- Best hostile to fight within `radius` (see value.priority), or nil.
function combat:threat(me, radius, accept)
  local best, best_score
  for _, unit in ipairs(self:monsters()) do
    if self:usable(unit) and unit.x and world.distance(me, unit) <= radius and (not accept or accept(unit)) then
      local score = value.priority(unit, me, self.build.element)
      -- Immune monsters only matter when they are in our face.
      if score >= 0 or world.distance(me, unit) < 4 then
        if not best_score or score > best_score then best, best_score = unit, score end
      end
    end
  end
  return best
end

local SAFER_GAIN = 0.175    -- minimum danger reduction logged as a safer completed route
local EVADE_TIMEOUT = 600    -- moving enemies invalidate a route quickly
local ESCAPE_PROGRESS = 0.75 -- actual subtiles traveled before renewing a tactical movement budget
local STALL_BREAK_MS = 600   -- time for a blow at what stands in the way to land (lib.move's BREAK_MS)

-- Accepted requests can still be blocked by live traffic; replacement plans
-- share the same progress budget until the player actually moves.
local function stalled_escape(self, me)
  local progress, t = self.escape_progress, now()
  if not progress or not act.ready() then return false end
  if t-progress.requested > EVADE_TIMEOUT+ESCAPE_RETRY_MS then
    self.escape_progress = nil -- an inactive escape must not poison the next fight
  elseif world.distance(me, progress) >= ESCAPE_PROGRESS then
    progress.x, progress.y, progress.at = me.x, me.y, t
  elseif t-progress.at >= EVADE_TIMEOUT then
    local plan = self.plan
    local point = plan and plan.path and plan.path[plan.index or 1]
    self.plan, self.escape_progress = nil, nil
    self.escape_retry_until = t+ESCAPE_RETRY_MS
    print(string.format('evade: accepted movement made no progress at (%.1f,%.1f)%s; yielding until retry', me.x, me.y,
      point and string.format(' toward (%d,%d) (%s)', point.x, point.y, tostring(plan.why)) or ''))
    -- What breaks and stands in the way is broken, as on a route (lib.move).
    local blocker = point and move.breakable_ahead and move.breakable_ahead(me, point)
    if blocker then
      print('evade: breaking ' .. tostring(blocker.name or blocker.id) .. ', which stands in the way')
      act.interact(blocker)
      self.last_action, self.escape_retry_until = t, t+STALL_BREAK_MS
    end
    -- What stands there, once for each place: the log is where a blocker gets its name.
    local place = math.floor(me.x/4)..','..math.floor(me.y/4)
    if self.stall_place ~= place then
      self.stall_place = place
      for _, object in ipairs(game.units('object')) do
        if object.x and world.distance(me, object) <= 6 then
          print(string.format('evade:   beside %s (class %s, operate %s, mode %s) at (%d,%d), %.1f away', tostring(object.name),
            tostring(object.class_id), tostring(object.operate), tostring(object.mode), object.x, object.y,
            world.distance(me, object)))
        end
      end
    end
    return true
  end
  return false
end

-- Hostiles within `radius`.
function combat:hostiles(me, radius)
  local list = {}
  for _, unit in ipairs(self:monsters()) do
    if world.hostile(unit) and unit.x and self:in_sight(unit) and world.distance(me, unit) <= radius then list[#list + 1] = unit end
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

-- Estimate flight from consecutive snapshots. Verified ground fire is dangerous
-- on first sight, including environmental fire without a monster owner.
function combat:observe_missiles(me)
  local t = now()
  local frame = charon.frame and charon.frame() or t
  if self.missile_frame == frame and self.missile_area == me.area then return end
  local previous = self.missile_area == me.area and self.missile_samples or {}
  local samples, lanes, owners, hits, hazards = {}, {}, {}, {}, {}
  if not me.in_town then
    for _, unit in ipairs(game.units('missile')) do
      if unit.ground_fire and unit.x and unit.y and world.distance(me, unit) <= 24 then
        local owner = unit.owner_type == 1 and game.unit('monster', unit.owner)
        -- Player spells and known hirelings/pets cannot damage us. Unknown or
        -- environmental owners remain dangerous until the patch disappears.
        if unit.owner_type ~= 0 and (not owner or (owner.owner == -1 and not owner.npc and not owner.mercenary)) then
          hazards[#hazards+1] = {x=unit.x,y=unit.y,size=unit.ground_size}
        end
      end
      if unit.owner_type == 1 and unit.x and unit.y and world.distance(me, unit) <= 24 then
        local old = previous and previous[unit.id]
        if old and (old.owner ~= unit.owner or old.class_id ~= unit.class_id
          or t-(old.seen or old.at)>200) then old = nil end
        if owners[unit.owner] == nil then
          local owner = game.unit('monster', unit.owner)
          owners[unit.owner] = owner and world.hostile(owner) or false
          -- What one of its missiles takes from us, by its owner's damage.
          hits[unit.owner] = owners[unit.owner] and value.dps(owner) or nil
        end
        if owners[unit.owner] or (old and old.hostile) then
          local sample = { x=unit.x, y=unit.y, at=t, seen=t,
            owner=unit.owner, class_id=unit.class_id, hostile=true }
          samples[unit.id] = sample
          local dt = old and t-old.at or 0
          if dt > 0 and dt <= 200 then
            local dx, dy = unit.x-old.x, unit.y-old.y
            local moved = dx*dx+dy*dy
            if moved <= 0.0025 then
              -- Repeated positions do not erase measured flight or shorten its time baseline.
              sample.x, sample.y, sample.at = old.x, old.y, old.at
              sample.vx, sample.vy = old.vx, old.vy
            elseif moved < (dt*0.2)^2 then
              sample.vx, sample.vy = dx/dt, dy/dt
            end
          end
          if sample.vx and #lanes < 64 and map.line_of_sight(me.x, me.y, unit.x, unit.y) then
            lanes[#lanes+1] = {x=unit.x, y=unit.y, hit=hits[unit.owner],
              to_x=unit.x+sample.vx*MISSILE_LEAD, to_y=unit.y+sample.vy*MISSILE_LEAD}
          end
        end
      end
    end
  end
  if #hazards > 128 then
    table.sort(hazards, function(a,b) return world.distance(me,a)<world.distance(me,b) end)
    for i = #hazards, 129, -1 do hazards[i] = nil end
  end
  self.ground_hazards = hazards
  self.missile_samples, self.missile_lanes, self.missile_frame, self.missile_area = samples, lanes, frame, me.area
end

-- Shared native footprint checks for staying, repositioning and short loot walks.
function combat:ground_clear(from, to)
  to = to or from
  return #(self.ground_hazards or {}) == 0
    or map.hazard_clear(math.floor(from.x)+0.5, math.floor(from.y)+0.5,
      math.floor(to.x)+0.5, math.floor(to.y)+0.5, self.ground_hazards)
end

-- Closest point on a straight loot approach; no path search or movement request.
local function loot_segment(me, item, point)
  local dx, dy = item.x-me.x, item.y-me.y
  local length = dx*dx+dy*dy
  local t = length > 0 and math.max(0, math.min(1,
    ((point.x-me.x)*dx+(point.y-me.y)*dy)/length)) or 0
  local x, y = me.x+t*dx, me.y+t*dy
  return x, y, (point.x-x)^2+(point.y-y)^2
end

-- Permission for a short opportunistic walk, not for looting through a fight.
-- Sample fresh visibility along the whole short walk: walls can conceal an
-- offset doorway from both endpoints and the closest point beside a threat.
function combat:loot_safe(me, item)
  if not me or not me.alive or not item or not me.x or not me.y or not item.x or not item.y
    or me.life < me.max_life*0.45 or world.distance(me, item) > LOOT_WALK
    or (self.plan and (self.plan.pressure or self.plan.missile)
      and (not self.plan.since or now()-self.plan.since <= EVADE_TIMEOUT)) then return false end
  local ok, clear = pcall(map.line_clear, me.x, me.y, item.x, item.y)
  if not ok or not clear then return false end
  ok, clear = pcall(map.line_of_sight, me.x, me.y, item.x, item.y)
  if not ok or not clear then return false end
  for _, object in ipairs(game.units('object')) do
    if object.door and object.mode == 0 then
      if not object.x or not object.y then return false end
      local _, _, distance2 = loot_segment(me, item, object)
      if distance2 <= LOOT_DOOR^2 then return false end
    end
  end
  local corridor = {}
  local steps = math.max(1, math.ceil(world.distance(me, item)))
  for i = 0, steps do
    corridor[#corridor+1] = {x=me.x+(item.x-me.x)*i/steps, y=me.y+(item.y-me.y)*i/steps}
  end
  ok = pcall(self.observe_missiles, self, me)
  if not ok or #(self.missile_lanes or {}) > 0 then return false end
  if not self:ground_clear(me, item) then return false end
  for _, sample in pairs(self.missile_samples or {}) do
    local _, _, distance2 = loot_segment(me, item, sample)
    if distance2 <= LOOT_SIGHT^2 then
      for _, point in ipairs(corridor) do
        if world.distance(point, sample) <= LOOT_SIGHT then
          ok, clear = pcall(map.line_of_sight, point.x, point.y, sample.x, sample.y)
          if not ok or clear then return false end
        end
      end
    end
  end
  for _, unit in ipairs(self:monsters()) do
    -- Untargetable/immune and ignored enemies can still make this walk unsafe.
    if unit.type == 'monster' and unit.alive and not unit.npc and unit.owner == -1 and not unit.mercenary then
      if not unit.x or not unit.y then return false end
      local _, _, distance2 = loot_segment(me, item, unit)
      local radius = unit.ranged and LOOT_SIGHT or LOOT_MELEE
      if distance2 <= radius^2 then
        for _, point in ipairs(corridor) do
          if world.distance(point, unit) <= radius then
            ok, clear = pcall(map.line_of_sight, point.x, point.y, unit.x, unit.y)
            if not ok or clear then return false end
          end
        end
      end
    end
  end
  return true
end

-- A short sidestep out of incoming fire, also usable during travel. Native
-- scoring keeps the entire route off walls and monster footprints.
-- passage_heading(me) supplies optional onward intent lazily, without a retained-target leash.
-- retain_engagement lets an active fight keep its approach target beyond firing range.
function combat:dodge(me, target, passage_heading, retain_engagement)
  self:observe_missiles(me)
  local lanes = self.missile_lanes
  if me.in_town or not me.alive then self.dodge_cast = nil; return false end
  local burning = not self:ground_clear(me)
  if now() < (self.escape_retry_until or 0) then return burning end
  if not act.ready() or now()-self.last_action < ACTION_MS then
    return burning or self.dodge_cast or self.plan and self.plan.missile or false
  end
  self.dodge_cast = nil -- travel may resume only after the defensive cast is ready again
  if stalled_escape(self, me) then return burning end
  -- Leave damaging floor effects before starting another spell animation. Replan
  -- against fresh patches, without a target leash pulling us back into the fire.
  if burning then
    if now()-(self.fire_checked or 0) < DODGE_CHECK_MS then return true end
    self.fire_checked = now()
    local options = {threats=self:threats(me),missiles=lanes,hazards=self.ground_hazards,
      radius=8,occupied=2.5,clearance=1,lead=0.5}
    local spot = map.safe_spot(options)
    if not spot.found or #spot.path == 0 or (spot.hazard_there or 0) > 0 then
      options.occupied, options.lead = 1, 0
      spot = map.safe_spot(options)
    end
    if spot.found and #spot.path > 0 and spot.hazard_there == 0 then
      if not self.plan or not self.plan.ground then
        print(string.format('combat: leaving ground fire (risk %.2f -> %.2f)', spot.hazard_here, spot.hazard_there))
      end
      self.plan = {x=spot.x,y=spot.y,path=spot.path,since=now(),from=spot.danger_here,
        start={x=me.x,y=me.y},lead=options.lead,occupied=options.occupied,
        why='ground fire',missile=true,ground=true}
      self:follow_plan(me)
    end
    return true -- readiness, refusal and trapped routes never authorize more casting in fire
  end
  if self.plan and self.plan.ground then self.plan = nil end
  return self.neural:dodge(self, me, target, passage_heading, retain_engagement)
end

-- Steps along the planned route to the safe spot. False once the plan is over.
function combat:follow_plan(me)
  if stalled_escape(self, me) then return false end
  local plan = self.plan
  if not plan then return false end
  -- Native waypoints name cells; game.me has fractional positions (often +0.5).
  local cell_x, cell_y = math.floor(me.x), math.floor(me.y)
  local function reached(point) return point.x == cell_x and point.y == cell_y end
  local arrived = reached(plan)
  if arrived or now() - plan.since > EVADE_TIMEOUT then
    self.plan = nil
    -- Report whether the completed tactical route reduced danger.
    local spot = map.safe_spot({ threats = self:threats(me), radius = 2,
      lead = plan.lead, occupied = plan.occupied, missiles = plan.missile and self.missile_lanes or nil,
      hazards = self.ground_hazards })
    local danger_now = spot and spot.danger_here or plan.from
    local worked = danger_now <= plan.from - SAFER_GAIN
    print(string.format('evade (%s): danger %.2f -> %.2f, moved %.1f (%s)', plan.why, plan.from, danger_now,
      plan.start and world.distance(me, plan.start) or 0, worked and 'safer' or 'reassessing'))
    return false
  end
  -- Follow the tactical route's corners: static line_clear alone cannot prove
  -- that a shortcut avoids the monsters the planner routed around.
  plan.index = plan.index or 1
  while plan.index < #plan.path and reached(plan.path[plan.index]) do
    plan.index = plan.index + 1
  end
  local point = plan.path[plan.index]
  if not point then self.plan = nil; return false end
  local waypoint_index = plan.index
  local previous = point
  local before = plan.path[plan.index-1] or {x=math.floor(me.x), y=math.floor(me.y)}
  local dx, dy = point.x-before.x, point.y-before.y
  for i = plan.index + 1, #plan.path do
    local p = plan.path[i]
    local nx, ny = p.x-previous.x, p.y-previous.y
    if (dx and (nx ~= dx or ny ~= dy)) or world.distance(me,p)>4 then break end
    dx, dy, previous, point = nx, ny, p, p
    waypoint_index = i
  end
  local due = now() - self.last_action >= (plan.issued and 160 or ACTION_MS)
  if due and act.ready() then
    if self:ground_clear(me) and not self:ground_clear(me, point) then
      self.plan = nil -- a new patch invalidated this previously safe route
      return false
    end
    local accepted = act.move(point.x, point.y, stamina.walk(me))
    if accepted == false then
      self.plan, self.escape_progress = nil, nil
      self.escape_retry_until = now() + ESCAPE_RETRY_MS
      print(string.format('evade: movement refused at (%.2f,%.2f) to (%d,%d), path %d/%d, mode %s (%s)',
        me.x, me.y, point.x, point.y, waypoint_index, #plan.path, tostring(me.mode), plan.why))
      return false
    end
    self.escape_progress = self.escape_progress or {x=me.x,y=me.y,at=now()}
    self.escape_progress.requested = now()
    self.last_action = now()
    plan.index, plan.issued = waypoint_index, true
  end
  return true
end

-- One step of fighting within `radius`, preferring a valid quest focus when supplied.
-- Returns 'fighting', 'resting' (waiting for mana) or 'clear'.
function combat:fight(me, radius, accept, focus_id)
  local target = self.target_id and game.unit('monster', self.target_id)
  if target and not target.alive then
    self.kills = self.kills + 1
    target = nil
  end
  local focus = focus_id and game.unit('monster', focus_id)
  if focus then
    if self.sight then self.sight[focus.id] = nil end -- quest focus needs current line of sight
    if not self:usable(focus) or world.distance(me, focus) > radius or (accept and not accept(focus)) then
      focus = nil
    end
  end
  -- The source of endlessly renewed Fallen comes first, hurt or not (the
  -- maintainer's rule: it saves the work of killing them twice). Kept for
  -- when she had nine tenths of her life, it was off for most of every Den:
  -- she drinks at seven tenths.
  local shaman_priority = not focus_id
  if shaman_priority then
    for _, unit in ipairs(self:monsters()) do
      if value.is_resurrector(unit) and self.sight then self.sight[unit.id] = nil end
    end
  end
  local candidate = focus or self:threat(me, radius, accept)
  if shaman_priority and (value.is_resurrectable(candidate) or value.is_resurrector(candidate)) then
    local shaman = self:threat(me, radius, function(unit)
      if not value.is_resurrector(unit) or (accept and not accept(unit)) then return false end
      local ok, seen = pcall(map.line_of_sight, me.x, me.y, unit.x, unit.y)
      return ok and seen -- bypassing a blocker requires confirmed sight, not a failed query
    end)
    candidate, shaman_priority = shaman or candidate, shaman ~= nil
  else
    shaman_priority = false
  end
  local target_distance = target and world.distance(me, target)
  -- Clear blockers on approach; a boss already in close range keeps damage focus.
  -- Retain an acquired blocker through the wider release radius.
  if focus and target and target_distance <= 4.5 and target_distance <= radius
    and self:usable(target) and (not accept or accept(target)) then
    candidate = target
  elseif (focus and world.distance(me, focus) > 4.5) or (not focus and target and target_distance > 4.5)
    or (shaman_priority and value.is_resurrector(candidate)) then
    local bypass_fallen = shaman_priority and value.is_resurrector(candidate)
    local blocking = world.nearest(self:hostiles(me, math.min(3.5, radius)), me,
      function(u) return self:usable(u) and (not accept or accept(u))
        and (not bypass_fallen or not value.is_resurrectable(u)) end)
    candidate = blocking or candidate
  end
  local replace = not target or not self:usable(target) or world.distance(me, target) > radius
    or (accept and not accept(target))
  if not replace and candidate and candidate.id ~= target.id and now() - (self.retargeted or 0) >= RETARGET_MS then
    local d, next_d = world.distance(me,target), world.distance(me,candidate)
    replace = focus ~= nil or (shaman_priority and value.is_resurrector(candidate) and value.is_resurrectable(target))
      or (next_d < 3.5 and d > 4.5) or ((target.life_percent or 100)>25
      and value.priority(candidate,me,self.build.element)>value.priority(target,me,self.build.element)*1.5)
  end
  if replace then
    target = candidate
    if not target then
      self.target_id = nil
      self.escape_progress = nil
      self.plan = nil
      self.resting = false
      return 'clear'
    end
    self.target_id = target.id
    self.retargeted = now()
    if not (self.plan and (self.plan.missile or self.plan.pressure)) then self.plan = nil end
    self.target_life, self.target_progress = target.life_percent, now()
    self.misses = 0
  end
  self.engagement_radius = radius
  -- Actual damage resets the neural policy's failed-cast and no-progress history.
  if (target.life_percent or 100) < (self.target_life or 100) - 0.5 then
    self.target_life, self.target_progress = target.life_percent, now()
    self.misses = 0
  end

  -- Never cancel an animation with a movement or another attack request.
  if not act.ready() or now() - self.last_action < ACTION_MS then return 'fighting' end
  return self.neural:fight(self, me, target)
end

return combat
