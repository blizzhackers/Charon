-- What to wear: compares the character as she is with the character she
-- would be with an item on, and says which change is the best one on offer.
--
--   local gear = require('lib.gear').new(build, { power = function(name, id, level) ... end })
--   local plan = gear:plan(me)          -- nil, or { item=, slot=, removes={...}, gain='dps' }
--   if plan then ... act.pick_item(plan.item); act.equip(plan.slot) ... end
--   gear:keeps(item)                    -- true for what the plan would put on: not for sale
--
-- The order of what matters is the maintainer's (2026-11-12), and it is
-- strict: a later line only decides between loadouts the earlier lines call
-- equal.
--   damage       one cast of her best attack: skill levels to it, from
--                whatever the item gives them by
--   mana_per_kill  mana after each kill ("absolutely crazy good, we always want that")
--   cast         frames a cast takes: faster cast rate counts only where it
--                reaches the next step
--   life         effective life: life over the share of a blow that gets
--                through, by the average of her resistances: fire,
--                lightning, cold, poison, and the physical one, which is
--                damage reduced with block as a second resistance behind it
--   potion_mana  mana, up to what the best potion on sale restores at a time
--   run          faster run/walk
--   recovery     faster hit recovery
--   mana         the rest of the mana pool
--   regen        mana regeneration
-- and after those, in an order of this module's choosing (the maintainer
-- has no preference): defence (which cannot be turned into blows avoided
-- until monster attack ratings are read), potion
-- slots on the belt (more carried is no help in the fight itself), gold
-- from monsters, and, for a build that fights with its weapon
-- (`melee_skill`), the weapon's own blow. Not for a caster: weighed for the
-- staff work of a dry sorceress, a bow and a javelin each looked the better
-- blow and she changed between them until the game was given up.
-- Damage is the hardest of these to come by, so it leads.
--
-- Every figure is the character's own, as the game has it with what she
-- wears now, less what would come off, plus what would go on. A figure the
-- character does not carry herself (the belt's slots, the weapon's blow) is
-- taken from what she wears, never assumed: assumed bare, the thing worn
-- and the thing carried each looked like a gain over nothing, in turn.
--
-- A loadout is judged whole. A two-handed staff takes the shield's place as
-- well as the wand's, and what comes off is counted as lost.

local world = require 'lib.world'

local gear = {}
gear.__index = gear

-- Body locations (eD2BodyLoc, headers/ghidra/enums.h).
gear.SLOT = { head = 1, amulet = 2, body = 3, right = 4, left = 5, ring_right = 6, ring_left = 7,
  belt = 8, boots = 9, gloves = 10 }
local SLOTS = 10
local RIGHT, LEFT, BELT = 4, 5, 8

-- ItemStatCost.txt rows. Life and mana are fixed point (x256).
local STAT = { energy = 1, vitality = 3, life = 7, mana = 9, block = 20, min_damage = 21, max_damage = 22,
  two_min = 23, two_max = 24, mana_regen = 27, defense = 31, damage_resist = 36, fire = 39, lightning = 41,
  cold = 43, poison = 45, gold = 79, class_skills = 83, run = 96, other_skill = 97, recovery = 99,
  cast_rate = 105, single_skill = 107, all_skills = 127, mana_per_kill = 138, skill_tab = 188 }
local RESISTS = { 'fire', 'lightning', 'cold', 'poison' }
local RESIST_CAP, BLOCK_CAP = 75, 75

-- Potion slots by belt code (Belts.txt, from memory of the data: sash and
-- light belt two rows, belt three, the heavier ones four). No belt: one row.
local BELT_SLOTS = { lbl = 8, vbl = 8, mbl = 12, tbl = 16, hbl = 16 }
local BELT_BARE, BELT_UNKNOWN = 4, 16 -- exceptional and elite belts all hold four rows

-- The lines of the comparison, in order.
gear.ORDER = { 'damage', 'mana_per_kill', 'cast', 'life', 'potion_mana', 'run', 'recovery', 'mana', 'regen',
  'defense', 'belt', 'gold', 'melee' }
-- Two figures within this share of each other are the same figure (rounding in the sums).
local SAME = 1e-9

function gear.new(build, options)
  options = options or {}
  return setmetatable({ build = build, power = options.power, class_id = options.class_id,
    potion = options.potion or 0 }, gear)
end

-- The skills that could be her attack: { { name=, id=, tab= }, ... }, the
-- build's own list. `tab` is the skill tab as items name it (class x 8 + page).
function gear:attacks()
  return self.build.attacks or {}
end

-- What one item adds, read from its own stats: a table of plain numbers.
-- `skills` maps skill id to the base level she has in it (nil when unlearned).
function gear:read(item, skills)
  local function stat(id, layer) return game.stat('item', item.id, id, layer) or 0 end
  local build = self.build
  local added = { skill = {}, fire = stat(STAT.fire), lightning = stat(STAT.lightning), cold = stat(STAT.cold),
    poison = stat(STAT.poison), block = stat(STAT.block), damage_resist = stat(STAT.damage_resist),
    defense = stat(STAT.defense), run = stat(STAT.run), recovery = stat(STAT.recovery),
    cast_rate = stat(STAT.cast_rate), mana_regen = stat(STAT.mana_regen),
    mana_per_kill = stat(STAT.mana_per_kill), gold = stat(STAT.gold) }
  added.life = stat(STAT.life) / 256 + stat(STAT.vitality) * (build.life_per_vitality or 0)
  added.mana = stat(STAT.mana) / 256 + stat(STAT.energy) * (build.mana_per_energy or 0)
  -- Levels to each attack. A bonus to the one skill counts whether she has
  -- learned it or not; the wider ones (its tab, her class, all skills) only
  -- reach a skill with a point in it.
  local wide = stat(STAT.all_skills) + (self.class_id and stat(STAT.class_skills, self.class_id) or 0)
  for _, attack in ipairs(self:attacks()) do
    local levels = stat(STAT.single_skill, attack.id) + stat(STAT.other_skill, attack.id)
    if skills and (skills[attack.id] or 0) > 0 then
      levels = levels + wide + (attack.tab and stat(STAT.skill_tab, attack.tab) or 0)
    end
    added.skill[attack.id] = levels
  end
  local one, two = (stat(STAT.min_damage) + stat(STAT.max_damage)) / 2, (stat(STAT.two_min) + stat(STAT.two_max)) / 2
  added.melee = math.max(one, two)
  added.belt = item.code and (BELT_SLOTS[item.code] or (item.body_location == BELT or item.belt) and BELT_UNKNOWN) or nil
  return added
end

-- The character as she stands: the figures the comparison is made on.
function gear:sheet(me)
  local function stat(id, layer) return game.stat('player', me.id, id, layer) or 0 end
  local sheet = { life = me.max_life or 0, mana = me.max_mana or 0, skill = {}, base = {},
    fire = stat(STAT.fire), lightning = stat(STAT.lightning), cold = stat(STAT.cold), poison = stat(STAT.poison),
    block = stat(STAT.block), damage_resist = stat(STAT.damage_resist), defense = stat(STAT.defense),
    run = stat(STAT.run), recovery = stat(STAT.recovery), cast_rate = stat(STAT.cast_rate),
    mana_regen = stat(STAT.mana_regen), mana_per_kill = stat(STAT.mana_per_kill), gold = stat(STAT.gold),
    melee = 0, belt = BELT_BARE }
  for _, skill in ipairs(game.skills()) do
    if (skill.owner or -1) == -1 then
      sheet.skill[skill.id] = math.max(sheet.skill[skill.id] or 0, skill.level or 0)
      sheet.base[skill.id] = skill.base_level or 0
    end
  end
  return sheet
end

-- `sheet` with the items of `removed` off and those of `added` on (lists of gear:read results).
function gear.change(sheet, removed, added)
  local result = { skill = {}, base = sheet.base }
  for name, value in pairs(sheet) do
    if type(value) == 'number' then result[name] = value end
  end
  for id, level in pairs(sheet.skill) do result.skill[id] = level end
  local function apply(list, sign)
    for _, item in ipairs(list or {}) do
      for name, value in pairs(item) do
        if name == 'skill' then
          for id, levels in pairs(value) do result.skill[id] = (result.skill[id] or 0) + sign * levels end
        elseif name == 'belt' then
          result.belt = sign > 0 and value or BELT_BARE
        elseif name == 'melee' then
          -- One weapon's blow at a time: what comes off takes it along.
          if sign > 0 then result.melee = math.max(result.melee, value)
          elseif value >= result.melee then result.melee = 0 end
        elseif type(value) == 'number' then
          result[name] = (result[name] or 0) + sign * value
        end
      end
    end
  end
  apply(removed, -1)
  apply(added, 1)
  return result
end

-- Frames one cast takes at a faster cast rate, from the build's steps
-- ({ { rate, frames }, ... } ascending): a rate that reaches no further step
-- changes nothing. Without the table every cast takes the same time.
function gear:cast_frames(rate)
  local frames = 0
  for _, step in ipairs(self.build.cast_frames or {}) do
    if rate >= step[1] then frames = step[2] end
  end
  return frames
end

-- The lines of the comparison for a sheet.
function gear:figures(sheet)
  local best = 0
  for _, attack in ipairs(self:attacks()) do
    local level = sheet.skill[attack.id] or 0
    if level > 0 and self.power then best = math.max(best, self.power(attack.name, attack.id, level) or 0) end
  end
  local block = math.max(0, math.min(BLOCK_CAP, sheet.block or 0))
  local reduced = math.max(0, math.min(RESIST_CAP, sheet.damage_resist or 0))
  -- A blow blocked is a blow resisted: what gets past the block is reduced.
  local resist = 100 - (100 - reduced) * (100 - block) / 100
  for _, name in ipairs(RESISTS) do resist = resist + math.max(-100, math.min(RESIST_CAP, sheet[name] or 0)) end
  resist = resist / (#RESISTS + 1)
  return {
    damage = best,
    mana_per_kill = sheet.mana_per_kill or 0,
    cast = -self:cast_frames(sheet.cast_rate or 0), -- fewer frames is the better figure
    life = math.max(0, sheet.life) / (1 - resist / 100),
    potion_mana = math.min(sheet.mana, self.potion > 0 and self.potion or sheet.mana),
    run = sheet.run or 0,
    recovery = sheet.recovery or 0,
    mana = sheet.mana,
    regen = sheet.mana * (100 + (sheet.mana_regen or 0)) / 100,
    defense = sheet.defense or 0,
    belt = sheet.belt or BELT_BARE,
    gold = sheet.gold or 0,
    melee = sheet.melee or 0,
  }
end

-- 1 when figures `a` are the better, -1 when `b` are, 0 when they are the
-- same; and the line that decided. `skip` names lines left out.
function gear.compare(a, b, skip)
  for _, name in ipairs(gear.ORDER) do
    local x, y = a[name] or 0, b[name] or 0
    if not (skip and skip[name]) and math.abs(x - y) > SAME * math.max(1, math.abs(x), math.abs(y)) then
      return x > y and 1 or -1, name
    end
  end
  return 0
end

-- What she wears, by body location.
function gear.worn()
  local worn = {}
  for _, item in ipairs(game.items()) do
    if item.location == 'equipped' and item.body_location and item.body_location >= 1
      and item.body_location <= SLOTS then
      worn[item.body_location] = item
    end
  end
  return worn
end

-- What comes off when `action` (act.equip_action) puts an item into `slot`.
local function displaced(worn, slot, action)
  local other = slot == RIGHT and LEFT or slot == LEFT and RIGHT or nil
  if action == 1 then return {} end
  if action == 5 then return { worn[slot] } end
  if action == 2 and other then return { worn[other] } end           -- two-handed, the other hand held something
  if action == 7 and other then return { worn[slot], worn[other] } end -- two-handed, both hands did
  return nil
end

-- The best single change among what she carries, or nil when nothing she
-- carries would improve her. Unidentified items are not judged: what they
-- give is not known yet. `exclude` is a set of item ids not to consider
-- (what has already come off once).
function gear:plan(me, exclude)
  local weigh = self:scales(me)
  local best
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.identified ~= false
      and not (exclude and exclude[item.id]) then
      best = weigh(item, best) or best
    end
  end
  return best
end

-- The weighing itself, set up once for the character as she stands:
-- weigh(item [, best]) returns the change that puts `item` on when that
-- beats her as she is (and beats `best`, a change already found), or nil.
-- The item may lie anywhere the client knows it.
function gear:scales(me)
  local sheet = self:sheet(me)
  local worn = gear.worn()
  local contribution = {}
  local function read(item)
    if not contribution[item.id] then contribution[item.id] = self:read(item, sheet.base) end
    return contribution[item.id]
  end
  -- What the character does not carry in her own figures comes from what she wears.
  sheet.belt = worn[BELT] and read(worn[BELT]).belt or BELT_BARE
  sheet.melee = math.max(worn[RIGHT] and read(worn[RIGHT]).melee or 0, worn[LEFT] and read(worn[LEFT]).melee or 0)
  local now = self:figures(sheet)
  local skip = not self.build.melee_skill and { melee = true } or nil
  return function(item, best)
    local found
    for slot = 1, SLOTS do
      local action = act.equip_action(item, slot)
      local off = action ~= 0 and displaced(worn, slot, action)
      if off then
        local removed = {}
        for _, old in ipairs(off) do removed[#removed + 1] = read(old) end
        local figures = self:figures(gear.change(sheet, removed, { read(item) }))
        local against = found and found.figures or best and best.figures or now
        if gear.compare(figures, against, skip) > 0 then
          local _, gain = gear.compare(figures, now, skip)
          found = { item = item, slot = slot, removes = off, gain = gain, figures = figures,
            was = gain and now[gain], becomes = gain and figures[gain] }
        end
      end
    end
    return found
  end
end

-- How an item's quality ranks for the question "could this be better than
-- what is in the slot" (the maintainer's rule): unique, set and rare
-- together at the top (a good rare can beat a bad unique, but the three
-- almost always beat a magic item; crafted is made as rare is), then magic.
-- Plain items rank below all of them.
local RANK = { [5] = 2, [6] = 2, [7] = 2, [8] = 2, [4] = 1 }
local function rank(item) return item and RANK[item.quality or 0] or 0 end

-- Whether `item` (on the ground, in the pack, in a shop) is worth having to
-- wear. What is known about it decides how that is asked:
--   identified   everything it gives can be read where it lies, so it is
--                weighed as it would be in the pack: worth having when
--                putting it on would be a gain, and she can wear it. (By
--                quality alone, a short bow was picked up for a sorceress
--                with a hand free.)
--   unidentified its quality against that of everything it would take the
--                place of: it may turn out the better once it is read. The
--                slot alone is asked, since what it needs is not known yet.
function gear:candidate(me, item, worn)
  if item.identified ~= false then return self:scales(me)(item) ~= nil end
  worn = worn or gear.worn()
  for slot = 1, SLOTS do
    local action = act.equip_action(item, slot, true)
    local off = action ~= 0 and displaced(worn, slot, action)
    if off then
      local beaten = true
      for _, old in ipairs(off) do
        if rank(old) > rank(item) then beaten = false end
      end
      if beaten then return true, slot end
    end
  end
  return false
end

-- Whether an item is gear at all: something that goes in a body slot.
function gear.wearable(item)
  for slot = 1, SLOTS do
    if act.equip_action(item, slot, true) ~= 0 then return true end
  end
  return false
end

-- First free cell of the inventory for a w x h item, or nil (the pack is 10 x 4).
function gear.free_cell(w, h, ignore)
  local cols, rows = 10, 4
  local used = {}
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.grid_x and item.id ~= ignore then
      for dy = 0, (item.height or 1) - 1 do
        for dx = 0, (item.width or 1) - 1 do used[(item.grid_y + dy) * cols + item.grid_x + dx] = true end
      end
    end
  end
  for y = 0, rows - h do
    for x = 0, cols - w do
      local fits = true
      for dy = 0, h - 1 do
        for dx = 0, w - 1 do
          if used[(y + dy) * cols + x + dx] then fits = false end
        end
      end
      if fits then return x, y end
    end
  end
end

-- Whether what `plan` takes off will find room once its item has left the
-- pack. Two items coming off (a two-handed weapon for wand and shield) each
-- need a place.
function gear.room(plan)
  local taken = {}
  for _, old in ipairs(plan.removes) do
    local x, y = gear.free_cell(old.width or 1, old.height or 1, plan.item.id)
    if not x then return false end
    taken[#taken + 1] = old
  end
  if #taken < 2 then return true end
  -- Two to place: count cells, which is enough for the shapes hands hold.
  local free = 40
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.id ~= plan.item.id then
      free = free - (item.width or 1) * (item.height or 1)
    end
  end
  local need = 0
  for _, old in ipairs(taken) do need = need + (old.width or 1) * (old.height or 1) end
  return free >= need
end

return gear
