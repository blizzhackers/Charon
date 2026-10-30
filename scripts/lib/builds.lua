-- Class builds: how each class spends points and picks its attack.
--
-- A build has:
--   element     main damage type, for weighing monsters against resistances
--   ranged      fight from a distance and keep monsters away
--   range       preferred attack distance in subtiles
--   stats       stat ids spent in rotation (0 str, 1 energy, 2 dex, 3 vit)
--   skills      skill names in the order points are spent (repeat a name to rank it up)
--   den_level   character level at which the Den of Evil is worth entering
--   buffs       self-buff skill names kept up outside of close combat
--   has_spells(known) -> true when the build relies on spells it has learned
--   attack(me, target, known, distance, crowd, nearby)
--     -> skill id, hand ('left'/'right'), at ('unit'/'self')
--     `known` maps skill name -> { id, level, mana, usable } for learned skills;
--     `nearby` lists the hostiles around us (the target included).
--   on_cast(skill id, known)   optional; called after each cast

local value = require 'lib.value'

local builds = {}

local function can_cast(skill, me)
  return skill and skill.level > 0 and skill.usable and me.mana >= skill.mana
end

builds.barbarian = {
  element = 'physical',
  den_level = 3,
  ranged = false,
  range = 2.8,
  stats = { 3, 3, 3, 0, 0 },
  skills = { 'Bash', 'Bash', 'Bash', 'Sword Mastery', 'Axe Mastery', 'Howl', 'Double Swing' },
  attack = function(me, target, known)
    if can_cast(known['Bash'], me) then return known['Bash'].id, 'left', 'unit' end
    return 0, 'left', 'unit'
  end,
}

-- Skill choice, in order:
--   1. Frost Nova (one point, for its chill, not its damage) when hostiles in
--      its reach are not chilled yet and can be (not cold immune): two or
--      more, or one that is close or elite.
--   2. Static Field when its percentage damage (a quarter of current life,
--      down to the difficulty's floor, through lightning resistance), summed
--      over every hostile in its radius, beats the best bolt on the target.
--      Everything is compared per cast: in a fight, time under attack is what
--      runs short, and mana is bought (mana efficiency is for choosing areas).
--   3. The bolt doing the most damage per cast through the target's resistance:
--      Charged Bolt, where the points go, or Fire Bolt against fire-weak
--      targets such as Andariel.
local BOLTS = { ['Charged Bolt'] = 3, ['Fire Bolt'] = 1, ['Ice Bolt'] = 1 } -- rough hits per cast on one target
local CHILLED = 11         -- States.txt 'cold'
local NOVA_REACH = 7       -- subtiles the nova ring reliably covers
local NOVA_CLOSE = 4.5     -- a lone unchilled monster this close is worth a nova
local NOVA_COOLDOWN = 1200 -- ms; the chill state can take a moment to show
local static_floor
local nova_at = 0

-- Static Field damage on one unit: 25% of current life, never below the floor.
local function static_damage(unit)
  if static_floor == nil then
    local info = game.difficulty_info()
    static_floor = info and info.static_field_min or 0
  end
  local full = value.life(unit)
  local current = full * (unit.life_percent or 100) / 100
  local damage = math.min(current * 0.25, math.max(0, current - full * static_floor / 100))
  return damage * value.through(value.resist(unit, 'lightning'))
end

local function distance(a, b)
  return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2)
end

builds.sorceress = {
  element = 'lightning',
  den_level = 6,
  has_spells = function(known)
    for name in pairs(BOLTS) do
      if known[name] then return true end
    end
    return false
  end,
  ranged = true,
  range = 7,
  buffs = { 'Frozen Armor' },
  stats = { 3 },
  -- The speedrun line for Act 1 (MrLlamaSC, Maxroll, DiabloBytes): every
  -- point into Charged Bolt, one into Frost Nova at level 6 for its chill and
  -- one into Static Field around level 11 to shave elites and bosses. No
  -- utility points (Telekinesis, armors) before the respec.
  skills = {
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
    'Frost Nova',
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
    'Static Field',
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
  },
  on_cast = function(skill, known)
    if known['Frost Nova'] and skill == known['Frost Nova'].id then nova_at = charon.now() end
  end,
  attack = function(me, target, known, _, _, nearby)
    nearby = nearby or { target }
    local nova, static = known['Frost Nova'], known['Static Field']
    if nova and can_cast(nova, me) and charon.now() - nova_at > NOVA_COOLDOWN then
      -- Worth the mana when it chills a group, or one that is on us or elite;
      -- one slow monster at range is cheaper to bolt.
      local unchilled, pressing = 0, false
      for _, unit in ipairs(nearby) do
        if distance(me, unit) <= NOVA_REACH and value.through(value.resist(unit, 'cold')) > 0
          and not game.state('monster', unit.id, CHILLED) then
          unchilled = unchilled + 1
          if distance(me, unit) <= NOVA_CLOSE or unit.champion or unit.unique or unit.superunique or unit.boss then
            pressing = true
          end
        end
      end
      if unchilled >= 2 or pressing then return nova.id, 'right', 'self' end
    end
    -- Bolts by damage per cast: casting time is what a fight runs short of,
    -- and per mana a one-point Fire Bolt would beat a maxed Charged Bolt.
    local best, best_damage
    for name, hits in pairs(BOLTS) do
      local skill = known[name]
      if can_cast(skill, me) then
        local damage = value.cast_damage(skill, target) * hits
        if damage > 0 and (not best_damage or damage > best_damage) then best, best_damage = skill, damage end
      end
    end
    if static and can_cast(static, me) then
      local radius = 5 + (static.level - 1) -- Skills.txt: 5 subtiles at level 1, +1 per level
      local total = 0
      for _, unit in ipairs(nearby) do
        if distance(me, unit) <= radius then total = total + static_damage(unit) end
      end
      -- Per cast, like the bolts: each Static cast takes a quarter of what is
      -- left, so it wins on fresh packs and the bolt takes over as they weaken.
      if total > (best_damage or 0) then return static.id, 'right', 'self' end
    end
    if best then return best.id, 'right', 'unit' end
    return 0, 'left', 'unit'
  end,
}

-- Any other class fights with its basic attack until it gets a build.
builds.default = {
  element = 'physical',
  den_level = 3,
  ranged = false,
  range = 2.8,
  stats = { 3, 0 },
  skills = {},
  attack = function() return 0, 'left', 'unit' end,
}

function builds.for_class(class)
  return builds[class] or builds.default
end

return builds
