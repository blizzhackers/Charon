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

-- Estimated full life of a unit snapshot (class average, elites scaled).
function value.life(unit)
  local info = value.monster(unit.class_id)
  if not info then return 0 end
  local life = (info.min_life + info.max_life) / 2
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
    if subject.unique or subject.superunique then
      experience = experience * 5
    elseif subject.champion then
      experience = experience * 3
    end
  end
  if player_level then experience = experience * value.penalty(player_level, info.level) end
  local through = value.through(info.resist[element or 'physical'] or 0)
  if through <= 0 or life <= 0 then return experience, math.huge, 0 end
  local effective = life / through
  return experience, effective, experience / effective
end

-- Average worth of an area's spawn list for a build element (and character level):
-- { experience_per_damage, density, monster_level, monsters = n }.
function value.area(level_id, element, player_level)
  local info = game.level_info(level_id)
  if not info or #info.monsters == 0 then return nil end
  local total_xp, total_effective, counted = 0, 0, 0
  for _, class_id in ipairs(info.monsters) do
    local experience, effective = value.kill(class_id, element, player_level)
    if effective < math.huge then
      total_xp, total_effective, counted = total_xp + experience, total_effective + effective, counted + 1
    end
  end
  if counted == 0 then return nil end
  return {
    experience_per_damage = total_xp / total_effective,
    density = info.density,
    monster_level = info.monster_level,
    waypoint = info.waypoint,
    monsters = counted,
  }
end

-- A target's resistance to an element (0 when unknown).
function value.resist(target, element)
  local info = value.monster(type(target) == 'table' and target.class_id or target)
  return info and info.resist[element] or 0
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
  local info = target and value.monster(target.class_id)
  local resist = info and info.resist[value.skill_element(skill.id)] or 0
  return average * value.through(resist)
end

-- Names of monsters that bring others back; killing them first saves time.
local RESURRECTORS = { ['Fallen Shaman'] = true, ['Carver Shaman'] = true, ['Devilkin Shaman'] = true, ['Dark Shaman'] = true }

-- Target priority for a unit snapshot: value per damage, resurrectors first,
-- discounted by distance so the bot does not cross the map for a good target.
function value.priority(unit, me, element)
  local _, effective, per_damage = value.kill(unit, element, me.level)
  if effective == math.huge then return -1 end -- immune: only fight it if it blocks us
  local remaining = effective * (unit.life_percent or 100) / 100
  local score = per_damage * 10 + 1 / (1 + remaining / 20)
  if RESURRECTORS[unit.name] then score = score + 50 end
  local d = math.sqrt((unit.x - me.x) ^ 2 + (unit.y - me.y) ^ 2)
  return score / (1 + d / 8)
end

return value
