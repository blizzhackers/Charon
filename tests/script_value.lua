-- Elite resistance estimates follow the client's ordered modifier snapshot.
local previous_game, previous_value = game, package.loaded['lib.value']
package.loaded['lib.value'] = nil
local rows, next_class = {}, 0
game = {
  monster_info = function(id) return rows[id] end,
  skill_info = function() return { element = 2 } end,
  skill_damage = function()
    return { physical_min = 0, physical_max = 0, elemental_min = 10, elemental_max = 10 }
  end,
}
local value = require('lib.value')
local function monster(resists, enchants, flags)
  next_class = next_class + 1
  local base = { physical = 0, magic = 0, fire = 0, lightning = 0, cold = 0, poison = 0 }
  for element, amount in pairs(resists or {}) do base[element] = amount end
  rows[next_class] = { resist = base, min_life = 100, max_life = 100, experience = 100, level = 10 }
  local unit = { class_id = next_class, enchants = enchants or {} }
  for flag, enabled in pairs(flags or { unique = true }) do unit[flag] = enabled end
  return unit, base
end

for _, flag in ipairs({ 'unique', 'superunique', 'champion' }) do
  local unit, base = monster({ lightning = 70 }, { 17 }, { [flag] = true })
  assert(value.resist(unit, 'lightning') == 145, flag .. ' lightning enchant grants immunity')
  assert(value.resist(unit.class_id, 'lightning') == 70 and base.lightning == 70,
    'an elite estimate must not alter the cached class resistance')
end
local minion = monster({ lightning = 70 }, { 8, 17 }, { minion = true })
assert(value.resist(minion, 'lightning') == 70, 'transferred minion enchants do not grant owner resistance')
assert(value.resist({ class_id = minion.class_id }, 'lightning') == 70, 'ordinary snapshots retain base resistance')
assert(value.resist({ class_id = -1 }, 'lightning') == 0, 'unknown monster retains the zero fallback')

for _, bonus in ipairs({ {9, 'fire', 75}, {17, 'lightning', 75}, {18, 'cold', 75},
    {23, 'poison', 75}, {25, 'magic', 20}, {28, 'physical', 50} }) do
  local unit = monster({ [bonus[2]] = 10 }, { bonus[1] })
  assert(value.resist(unit, bonus[2]) == 10 + bonus[3], 'wrong single-element modifier bonus')
end
local capped = monster({ physical = 100, magic = 100, lightning = 70 }, { 17, 28 })
assert(value.resist(capped, 'lightning') == 70 and value.resist(capped, 'physical') == 100,
  'two existing immunities stop all MagicResist bonuses')
local priority = monster({ cold = 60, fire = 60, lightning = 70 }, { 8 })
assert(value.resist(priority, 'cold') == 100 and value.resist(priority, 'fire') == 100
    and value.resist(priority, 'lightning') == 70, 'magic resistance stops between elements in cold/fire/light order')
local already_immune = monster({ cold = 120 }, { 8 })
assert(value.resist(already_immune, 'cold') == 120 and value.resist(already_immune, 'fire') == 40
    and value.resist(already_immune, 'lightning') == 40, 'magic resistance leaves existing elemental immunity alone')
local first = monster({ cold = 60, lightning = 70 }, { 17, 8 })
local second = monster({ cold = 60, lightning = 70 }, { 8, 17 })
assert(value.resist(first, 'lightning') == 145 and value.resist(first, 'fire') == 0
    and value.resist(second, 'lightning') == 110, 'snapshot enchant order controls immunity limits')
local spectral = monster({ cold = 74, fire = 75, lightning = 90 }, { 27 })
assert(value.resist(spectral, 'cold') == 94 and value.resist(spectral, 'fire') == 75
    and value.resist(spectral, 'lightning') == 90, 'spectral hit boosts only resistances below 75')
local ghostly = monster({ physical = 120, magic = 100 }, { 36 }, { champion = true })
assert(value.resist(ghostly, 'physical') == 80, 'ghostly sets physical resistance independently of immunity cap')
local ordinary_champion = monster({ lightning = 70 }, { 16 }, { champion = true })
assert(value.resist(ordinary_champion, 'lightning') == 70, 'ordinary champion grants no elemental resistance')

local immune = monster({ lightning = 70 }, { 17 })
assert(value.cast_damage({ id = 38, level = 8 }, immune) == 0, 'cast ranking uses actual elite resistance')
local experience, effective, score = value.kill(immune, 'lightning')
assert(experience == 500 and effective == math.huge and score == 0, 'unit kill estimates honor elite immunity')
local _, class_effective = value.kill(immune.class_id, 'lightning')
assert(math.abs(class_effective - 100 / 0.3) < 0.0001, 'class kill estimates retain base resistance')
game.level_info = function() return { monsters = { immune.class_id }, density = 100, monster_level = 10 } end
assert(value.area(1, 'lightning').monsters == 1, 'area scoring remains class based')
do -- kinds that shoot are no part of what an area pays, and it counts as that much thinner
  local walker, shooter = monster({}, {}, {}), monster({}, {}, {})
  rows[shooter.class_id].ranged = true
  rows[shooter.class_id].experience = 900
  local kept_info = game.level_info
  game.level_info = function() return { monsters = { walker.class_id, shooter.class_id }, density = 100, monster_level = 5 } end
  local area = value.area(1, 'lightning')
  assert(area.monsters == 1 and area.experience_per_damage == 1 and area.density == 50,
    'only the kind she fights is counted, and half the area is its')
  game.level_info = function() return { monsters = { shooter.class_id }, density = 100, monster_level = 5 } end
  assert(value.area(1, 'lightning') == nil, 'an area of shooters alone pays nothing')
  game.level_info = kept_info
end
do -- one cast is the least a kill costs: overkill is not paid for
  local weak = monster({}, {}, {})
  rows[weak.class_id].min_life, rows[weak.class_id].max_life, rows[weak.class_id].experience = 4, 4, 20
  local kept_info = game.level_info
  game.level_info = function() return { monsters = { weak.class_id }, density = 100, monster_level = 1 } end
  assert(value.area(1, 'lightning').experience_per_damage == 5, 'by damage alone a weak monster looks rich')
  assert(value.area(1, 'lightning', nil, 20).experience_per_damage == 1, 'a volley of twenty into four life pays for twenty')
  assert(value.area(1, 'lightning', nil, 2).experience_per_damage == 5, 'a cast smaller than its life changes nothing')
  game.level_info = kept_info
end

do
  -- An act boss's row is the boss: neither its life nor its blows are scaled as an elite's.
  local boss = monster({}, {}, { unique = true })
  rows[boss.class_id].boss, rows[boss.class_id].attack_min, rows[boss.class_id].attack_max = true, 10, 14
  rows[boss.class_id].attack2_min, rows[boss.class_id].attack2_max = 0, 0
  assert(value.life(boss) == 100 and value.dps(boss) == 12, 'a boss keeps its own life and damage')
  local elite = monster({}, {}, { unique = true })
  rows[elite.class_id].attack_min, rows[elite.class_id].attack_max = 10, 14
  rows[elite.class_id].attack2_min, rows[elite.class_id].attack2_max = 0, 0
  assert(value.life(elite) == 400 and value.dps(elite) == 24, 'another unique is scaled from its class')
end
do
  -- Where a build may walk in it may also level (it once entered the Barracks and left to farm lower).
  local previous_leveling, previous_store = package.loaded['lib.leveling'], store
  package.loaded['lib.leveling'] = nil
  store = { get = function() return nil end, set = function() end }
  local levels = { [28] = 9, [5] = 5 }
  local grunt = monster({}, {}, {})
  game.level_info = function(id)
    return levels[id] and { monsters = { grunt.class_id }, density = 100, monster_level = levels[id], name = 'area' .. id }
  end
  game.level_name = function(id) return 'area' .. id end
  local old_map = map
  map = { exits = function() return {} end }
  local leveling = require('lib.leveling')
  local old_hops = leveling.hops_from
  leveling.hops_from = function() return { [28] = 0, [5] = 4 } end
  local me = { area = 28, level = 6 }
  assert(leveling.choose(me, { element = 'lightning', enter_gap = 3 }, { candidates = { 28, 5 } }) == 28,
    'an area the build may enter is one it may level in')
  assert(leveling.choose(me, { element = 'lightning', enter_gap = 1 }, { candidates = { 28, 5 } }) == 5,
    'a narrower gap keeps to areas near its level')
  leveling.hops_from, map, store = old_hops, old_map, previous_store
  package.loaded['lib.leveling'] = previous_leveling
end
do -- what a monster is expected to take from her uses the game's own chance that its blow lands
  local kept_now = charon and charon.now
  charon = charon or {}
  local t = 1000
  charon.now = function() return t end
  local unit = { class_id = 9001 }
  local table_rate = value.table_dps(unit)
  assert(table_rate > 0 and value.dps(unit) == table_rate, 'without the figure from the game a blow is taken to land')
  game.hit_chance = function(class_id) assert(class_id == 9001); return 40, 25 end
  t = t + 3000
  assert(math.abs(value.dps(unit) - table_rate * 0.4 * 0.75) < 1e-9, 'four blows in ten land, and a quarter of those are blocked')
  game.hit_chance = function() return 95, 0 end
  assert(math.abs(value.dps(unit) - table_rate * 0.4 * 0.75) < 1e-9, 'the figure is kept a moment')
  t = t + 3000
  assert(math.abs(value.dps(unit) - table_rate * 0.95) < 1e-9, 'and asked again: her defence changes with her gear')
  game.hit_chance = nil
  charon.now = kept_now
end
game, package.loaded['lib.value'] = previous_game, previous_value
print('monster resistance regressions passed')
