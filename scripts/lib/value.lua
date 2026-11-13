-- What monsters and areas are worth to a build, from the game's own data.
--
-- Experience is weighed against *effective* life: average life divided by what
-- the build's element still does through the monster's resistance. Fallen and
-- Quill Rats (1-5 life, ~20 experience) are worth far more per hit than their
-- level suggests; lightning-resistant Tainted are poor for a Charged Bolt
-- sorceress whatever their level. Scores are experience per point of damage.

local value = {}

local monster_cache = {}

function value.monster(class_id)
  local info = monster_cache[class_id]
  if info == nil then
    info = game.monster_info(class_id) or false
    monster_cache[class_id] = info
    -- The game's own ranking of the class as a target, once in the log for
    -- each: to see whether it tells the ones worth killing first.
    if info and info.threat then
      print(string.format('value: class %d %s, threat %d', class_id, tostring(info.name), info.threat))
    end
  end
  return info or nil
end

-- Multiplier on damage that gets through a resistance (0 when immune).
function value.through(resist)
  if resist >= 100 then return 0 end
  return 1 - resist / 100
end

-- Share of a monster's experience a character of `player_level` receives,
-- by the game's level-gap rule (out of 256 for gaps above 5).
local BELOW = { [6] = 207, [7] = 159, [8] = 110, [9] = 61 }  -- monster lower than the player
local ABOVE = { [6] = 225, [7] = 174, [8] = 92, [9] = 38 }   -- monster higher, player below 25

function value.penalty(player_level, monster_level)
  local gap = monster_level - player_level
  if math.abs(gap) <= 5 then return 1 end
  if gap > 0 and player_level >= 25 then return player_level / monster_level end
  local table_ = gap > 0 and ABOVE or BELOW
  return (table_[math.abs(gap)] or (gap > 0 and 5 or 13)) / 256
end

-- Monster life grows by half for every player past the first (/players N).
-- Monsters keep the life they spawned with, so this is exact only for those
-- spawned under the current setting.
function value.players_life()
  local players = game.players and game.players()
  local count = players and players.supported and players.configured or 1
  return 1 + (count - 1) / 2
end

-- Estimated full life of a unit snapshot (class average, elites scaled).
-- An act boss's row is the boss itself: its numbers are not scaled again.
function value.life(unit)
  local info = value.monster(unit.class_id)
  if not info then return 0 end
  local life = (info.min_life + info.max_life) / 2 * value.players_life()
  if info.boss then return life end
  if unit.unique or unit.superunique then return life * 4 end
  if unit.champion then return life * 3 end
  return life
end

-- Expected experience, effective life and experience per damage point for a
-- monster class (or a unit snapshot, which adds champion/unique scaling).
-- With `player_level`, experience includes the level-gap penalty.
function value.kill(subject, element, player_level)
  local class_id = type(subject) == 'table' and subject.class_id or subject
  local info = value.monster(class_id)
  if not info then return 0, math.huge, 0 end
  local life = (info.min_life + info.max_life) / 2
  local experience = info.experience
  if type(subject) == 'table' then
    -- Elites carry several times the life and experience of their kind
    -- (approximate multipliers; measured area rates correct the estimate).
    life = value.life(subject)
    if info.boss then -- its row already carries the boss's own experience
    elseif subject.unique or subject.superunique then
      experience = experience * 5
    elseif subject.champion then
      experience = experience * 3
    end
  end
  if player_level then experience = experience * value.penalty(player_level, info.level) end
  local through = value.through(value.resist(subject, element or 'physical'))
  if through <= 0 or life <= 0 then return experience, math.huge, 0 end
  local effective = life / through
  return experience, effective, experience / effective
end

-- Average worth of an area's spawn list for a build element (and character level):
-- { experience_per_damage, density, monster_level, monsters = n }.
-- With `cast`, the damage one cast of the build's attack does, a monster
-- costs at least one cast however little life it has: a volley of fourteen
-- into a Fallen with three is mostly spent on nothing, and by the plain
-- figure the Blood Moor out-paid the crypts for a level-3 sorceress, who
-- then spent nine minutes there for one level.
-- Kinds that shoot are left out: they are gone around, not fought (the
-- maintainer's rule; dodging them cost more time than they paid), so what
-- they would pay is no part of what the area pays, and the area counts as
-- that much thinner.
function value.area(level_id, element, player_level, cast)
  local info = game.level_info(level_id)
  if not info or #info.monsters == 0 then return nil end
  local total_xp, total_effective, counted = 0, 0, 0
  for _, class_id in ipairs(info.monsters) do
    local experience, effective = value.kill(class_id, element, player_level)
    local kind = value.monster(class_id)
    if effective < math.huge and not (kind and kind.ranged) then
      if cast and cast > 0 then effective = math.max(effective, cast) end
      total_xp, total_effective, counted = total_xp + experience, total_effective + effective, counted + 1
    end
  end
  if counted == 0 then return nil end
  return {
    experience_per_damage = total_xp / total_effective,
    density = info.density * counted / #info.monsters,
    monster_level = info.monster_level,
    waypoint = info.waypoint,
    monsters = counted,
  }
end

local RESIST_ELEMENTS = { 'physical', 'magic', 'fire', 'lightning', 'cold', 'poison' }
local ENCHANT_ELEMENTS = { [9] = 'fire', [17] = 'lightning', [18] = 'cold', [23] = 'poison',
  [25] = 'magic', [28] = 'physical' }
local ENCHANT_BONUSES = { [9] = 75, [17] = 75, [18] = 75, [23] = 75, [25] = 20, [28] = 50 }
local ELEMENTAL_RESISTS = { 'cold', 'fire', 'lightning' }

-- Class-base resistance plus visible owner modifiers; minions do not inherit
-- their owner's resistance bonuses (Game.exe 0x5A2120 dispatches apply=false).
function value.resist(target, element)
  local info = value.monster(type(target) == 'table' and target.class_id or target)
  if not info then return 0 end
  local base = info.resist
  if type(target) ~= 'table' or not (target.unique or target.superunique or target.champion)
      or not target.enchants or #target.enchants == 0 then return base[element] or 0 end
  local resists = {}
  for _, name in ipairs(RESIST_ELEMENTS) do resists[name] = base[name] or 0 end
  -- 0x5A1370 stops at two immunities, including between ID8's ordered additions.
  for _, enchant in ipairs(target.enchants) do
    if enchant == 36 then
      resists.physical = 80 -- Ghostly's separate handler, 0x5A1080, has no immunity cap.
    else
      local immunities = 0
      for _, name in ipairs(RESIST_ELEMENTS) do
        if resists[name] >= 100 then immunities = immunities + 1 end
      end
      if immunities < 2 then
        if enchant == 8 or enchant == 27 then
          local threshold, bonus = enchant == 8 and 100 or 75, enchant == 8 and 40 or 20
          for _, name in ipairs(ELEMENTAL_RESISTS) do
            if immunities < 2 and resists[name] < threshold then
              resists[name] = resists[name] + bonus
              if resists[name] >= 100 then immunities = immunities + 1 end
            end
          end
        elseif ENCHANT_ELEMENTS[enchant] then
          local name = ENCHANT_ELEMENTS[enchant]
          resists[name] = resists[name] + ENCHANT_BONUSES[enchant]
        end
      end
    end
  end
  return resists[element] or 0
end

-- Damage element of a skill (Skills.txt EType) as a resistance name.
local ELEMENTS = { [0] = 'physical', [1] = 'fire', [2] = 'lightning', [3] = 'magic', [4] = 'cold', [5] = 'poison' }
local skill_cache = {}

function value.skill_element(skill_id)
  local cached = skill_cache[skill_id]
  if not cached then
    local info = game.skill_info(skill_id)
    cached = ELEMENTS[info and info.element or 0] or 'physical'
    skill_cache[skill_id] = cached
  end
  return cached
end

-- Average damage of one cast of a learned skill against a target, after its
-- resistance. `skill` is a game.skills() entry.
function value.cast_damage(skill, target)
  local damage = game.skill_damage(skill.id, skill.level)
  if not damage then return 0 end
  local average = (damage.physical_min + damage.physical_max + damage.elemental_min + damage.elemental_max) / 2
  local resist = target and value.resist(target, value.skill_element(skill.id)) or 0
  return average * value.through(resist)
end

-- What a monster class has shown beyond its table damage (spells, poison,
-- auras): a factor learned from the life it took, kept for the session so
-- the next meeting starts from what the last one cost.
local LEARNED_CAP = 4
-- What has been learned the same way in every session is known from the
-- start. The Tainted and the Afflicted spit lightning, a missile with damage
-- of its own that the attack columns do not hold: between 2.2 and 4 times
-- the table in each of six runs, each of which found it out by walking up
-- to an elite pack of them beside the Catacombs waypoint or Andariel's stairs.
-- Their kin spit the same: two elite packs of Misshapen inside the door of
-- the Underground Passage took half a level-4 life in two seconds.
-- MonStats rows: Afflicted, Tainted, Misshapen, Disfigured, Damned
local MEASURED = { [10] = 3, [11] = 3, [12] = 3, [13] = 3, [14] = 3 }
function value.harder(class_id)
  return math.max(MEASURED[class_id] or 1, store and store.get('value.harder.' .. class_id) or 1)
end

function value.learn(class_id, factor)
  factor = math.min(LEARNED_CAP, factor)
  if not store or factor <= value.harder(class_id) then return false end
  store.set('value.harder.' .. class_id, factor)
  return true
end

-- And the other way. The tables say what a blow does when it lands, once a
-- second, from everything in reach; they do not say how often it lands, and
-- weak monsters miss, wander and wait their turn. What a class has landed
-- of what the tables promised, while she stood in its reach, scales it down
-- (never under LANDS_FLOOR, and back up toward 1 when it lands more). With
-- every Fallen counted at its full table she would not take a hit from
-- monsters that "don't hit that hard" (the maintainer, watching her kite
-- the Den for most of ten minutes): over six clears the tables promised
-- some 2,700 points of damage for the ground she stood on, and 315 landed.
--   LANDS_EVIDENCE  the promised damage, as a share of her full life, that
--                   must have been stood through before a class is re-rated
local LANDS_FLOOR, LANDS_EVIDENCE = 0.2, 0.5
local stood = {}
function value.lands(class_id)
  return class_id and store and store.get('value.lands.' .. class_id) or 1
end

-- `promised`: what the tables (as now rated) said `class_id` would take
-- from her over a stretch she stood in its reach; `lost`: the life she lost
-- in it. Returns the new rating when the evidence is in and it has changed.
function value.observe(class_id, promised, lost, max_life)
  if not store or not class_id or promised <= 0 then return nil end
  local seen = stood[class_id] or { promised = 0, lost = 0 }
  stood[class_id] = seen
  seen.promised, seen.lost = seen.promised + promised, seen.lost + lost
  if seen.promised < LANDS_EVIDENCE * math.max(1, max_life) then return nil end
  local before = value.lands(class_id)
  local rated = math.max(LANDS_FLOOR, math.min(1, before * seen.lost / seen.promised))
  seen.promised, seen.lost = 0, 0
  if math.abs(rated - before) < 0.05 then return nil end
  store.set('value.lands.' .. class_id, rated)
  return rated
end

-- Expected life lost per second while a monster is in reach of us: its
-- stronger physical attack (MonStats A1/A2, scaled by MonLvl) landing about
-- once a second, times what the class has been seen to add. Attack rating
-- and our defense are not modelled; elites hit harder by an approximate factor.
local ELITE_DAMAGE, CHAMPION_DAMAGE = 2, 1.5
function value.dps(unit)
  return value.table_dps(unit) * value.harder(unit.class_id) * value.lands(unit.class_id) * value.hits(unit.class_id)
end

-- The share of a class's blows that land on her, by the game's own
-- arithmetic (game.hit_chance: its to-hit against her defence and both
-- levels, and then her block): asked of the game instead of being found out
-- blow by blow (the maintainer's question). What that arithmetic does not
-- hold, how often a monster swings at all, is still what value.lands
-- measures. Asked again every HITS_MS, since her defence changes with her gear.
local HITS_MS = 2000
local hits = {}
function value.hits(class_id)
  if not class_id or not game.hit_chance then return 1 end
  local t = charon.now()
  local known = hits[class_id]
  if known and t - known.at < HITS_MS and t >= known.at then return known.share end
  local chance, block = game.hit_chance(class_id)
  local share = chance and chance / 100 * (1 - (block or 0) / 100) or 1
  if chance and not known then
    print(string.format('value: class %d lands %d%% of its blows on her as the game reckons it (block %d%%)',
      class_id, chance, block or 0))
  end
  hits[class_id] = { at = t, share = share }
  return share
end

function value.table_dps(unit)
  local info = value.monster(unit.class_id)
  local hit = info and info.attack_max and math.max((info.attack_min + info.attack_max) / 2,
    ((info.attack2_min or 0) + (info.attack2_max or 0)) / 2) or 0
  -- No table damage (an older host, or a monster that only casts): scale with level.
  if hit <= 0 then hit = 1 + 0.75 * (info and info.level or 1) end
  if info and info.boss then return hit end
  if unit.unique or unit.superunique then return hit * ELITE_DAMAGE end
  if unit.champion then return hit * CHAMPION_DAMAGE end
  return hit
end

-- Names of monsters that bring others back; killing them first saves time.
local RESURRECTORS = { ['Fallen Shaman'] = 'Fallen', ['Carver Shaman'] = 'Carver', ['Devilkin Shaman'] = 'Devilkin', ['Dark Shaman'] = 'Dark One' }

function value.is_resurrector(unit)
  return unit and RESURRECTORS[unit.name] ~= nil
end

function value.is_resurrectable(unit)
  if not unit or unit.unique or unit.champion or unit.superunique or unit.boss or unit.minion then return false end
  for _, name in pairs(RESURRECTORS) do
    if unit.name == name then return true end
  end
  return false
end

-- Target priority for a unit snapshot: value per damage, resurrectors first,
-- discounted by distance so the bot does not cross the map for a good target.
function value.priority(unit, me, element)
  local _, effective, per_damage = value.kill(unit, element, me.level)
  if effective == math.huge then return -1 end -- immune: only fight it if it blocks us
  local remaining = effective * (unit.life_percent or 100) / 100
  local score = per_damage * 10 + 1 / (1 + remaining / 20)
  if value.is_resurrector(unit) then score = score + 50 end
  -- The game's own ranking of the class as a target (MonStats.txt threat:
  -- ten for most of Act 1, twelve for a Fallen Shaman) scales the rest.
  local info = unit.class_id and value.monster(unit.class_id)
  local threat = info and info.threat or 0
  if threat > 0 then score = score * math.max(0.5, math.min(2, threat / 10)) end
  local d = math.sqrt((unit.x - me.x) ^ 2 + (unit.y - me.y) ^ 2)
  return score / (1 + d / 8)
end

return value
