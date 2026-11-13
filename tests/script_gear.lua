-- lib/gear: which change of gear is the best on offer, in the maintainer's order.
for name in pairs(package.loaded) do
  if name:sub(1, 4) == 'lib.' then package.loaded[name] = nil end
end

local RIGHT, LEFT = 4, 5
local items, me
local function worn_at(slot)
  for _, item in ipairs(items) do
    if item.location == 'equipped' and item.body_location == slot then return item end
  end
end
game = {
  items = function() return items end,
  skills = function() return me.skills end,
  stat = function(kind, id, stat, layer)
    if kind == 'player' then return (me.stats[stat] or {})[layer or 0] or 0 end
    for _, item in ipairs(items) do
      if item.id == id then return ((item.stats or {})[stat] or {})[layer or 0] or 0 end
    end
    return 0
  end,
}
-- The client's answer for a click on a body slot, as far as these cases need it.
act = { equip_action = function(item, slot, ignore_requirements)
  if not (item.slots or {})[slot] then return 0 end
  if not ignore_requirements and item.needs and item.needs > me.strength then return 0 end
  local other = slot == RIGHT and LEFT or slot == LEFT and RIGHT or nil
  if item.two_handed and other then
    if worn_at(slot) and worn_at(other) then return 7 end
    if worn_at(other) then return 2 end
  end
  return worn_at(slot) and 5 or 1
end }

local gear_lib = require 'lib.gear'
local build = {
  life_per_vitality = 2, mana_per_energy = 2,
  attacks = { { name = 'Charged Bolt', id = 38, tab = 9 }, { name = 'Fire Bolt', id = 36, tab = 8 } },
  cast_frames = { { 0, 13 }, { 9, 12 }, { 20, 11 } },
}
-- Charged Bolt gains a bolt with every level; Fire Bolt is the weaker spell.
local function power(name, _, level) return name == 'Charged Bolt' and (level + 2) * (2 + level) or level end
local gear = gear_lib.new(build, { power = power, class_id = 1, potion = 40 })

local SINGLE, TAB, CLASS, ALL, KILL, CAST, LIFE, VIT, FIRE, LIGHT, COLD, POISON = 107, 188, 83, 127, 138, 105, 7, 3, 39, 41, 43, 45
local RUN, DEFENSE, BLOCK, MANA, ENERGY = 96, 31, 20, 9, 1
local next_id = 0
local function item(fields)
  next_id = next_id + 1
  fields.id, fields.location, fields.page = next_id, fields.location or 'container', fields.page or 0
  fields.width, fields.height = fields.width or 1, fields.height or 1
  return fields
end
local function hands(two) return { [RIGHT] = true, [LEFT] = true }, two end
local function reset(list)
  items = list
  me = { id = 1, max_life = 60, max_mana = 40, strength = 10, stats = {},
    skills = { { id = 38, level = 3, base_level = 3, owner = -1 } } }
end

do -- damage leads: the staff that adds most to her attack
  local plain = item { code = 'sst', slots = hands(), two_handed = true, location = 'equipped', body_location = RIGHT }
  local one = item { code = 'sst', slots = hands(), two_handed = true, stats = { [SINGLE] = { [38] = 1 } } }
  local three = item { code = 'lst', slots = hands(), two_handed = true, stats = { [SINGLE] = { [38] = 3 } } }
  reset { plain, one, three }
  local plan = gear:plan(me)
  assert(plan and plan.item == three and plan.gain == 'damage' and plan.removes[1] == plain,
    'of two staves the one that adds more to her attack is worn, and the old one comes off')
  -- What an item gives her attack is read the way items give it.
  local tab = item { code = 'amu', slots = { [2] = true }, stats = { [TAB] = { [9] = 1 }, [ALL] = { [0] = 1 }, [CLASS] = { [1] = 1 } } }
  reset { tab }
  assert(gear:read(tab, { [38] = 3 }).skill[38] == 3 and gear:read(tab, { [38] = 3 }).skill[36] == 0,
    'tab, class and all-skill levels reach a skill she has a point in, and not one she has none in')
  local fire = item { code = 'sst', slots = hands(), stats = { [SINGLE] = { [36] = 2 } } }
  reset { fire }
  assert(gear:read(fire, { [38] = 3 }).skill[36] == 2, 'levels to the one skill count learned or not')
end

do -- both hands are one choice, and what comes off is lost
  local wand = item { code = 'wnd', slots = hands(), location = 'equipped', body_location = RIGHT, stats = { [SINGLE] = { [38] = 2 } } }
  local shield = item { code = 'buc', slots = hands(), location = 'equipped', body_location = LEFT,
    stats = { [FIRE] = { [0] = 20 }, [LIGHT] = { [0] = 20 }, [COLD] = { [0] = 20 }, [POISON] = { [0] = 20 } } }
  local staff = item { code = 'sst', slots = hands(), two_handed = true, stats = { [SINGLE] = { [38] = 2 } } }
  reset { wand, shield, staff }
  me.skills[1].level = 5 -- with the wand's two
  me.stats = { [FIRE] = { [0] = 20 }, [LIGHT] = { [0] = 20 }, [COLD] = { [0] = 20 }, [POISON] = { [0] = 20 } }
  assert(gear:plan(me) == nil, 'a staff that adds no more than the wand does not take the shield off')
  staff.stats[SINGLE][38] = 3
  local plan = gear:plan(me)
  assert(plan and plan.item == staff and #plan.removes == 2 and plan.gain == 'damage',
    'one that adds more does, shield and all: damage comes first')
end

do -- mana after each kill stands with the damage, above cast rate and life
  local kill = item { code = 'rin', slots = { [6] = true, [7] = true }, stats = { [KILL] = { [0] = 2 } } }
  local cast = item { code = 'rin', slots = { [6] = true, [7] = true }, location = 'equipped', body_location = 6,
    stats = { [CAST] = { [0] = 10 }, [LIFE] = { [0] = 40 * 256 } } }
  local other = item { code = 'rin', slots = { [6] = true, [7] = true }, location = 'equipped', body_location = 7,
    stats = { [LIFE] = { [0] = 30 * 256 } } }
  reset { kill, cast, other }
  me.stats = { [CAST] = { [0] = 10 } }
  local plan = gear:plan(me)
  assert(plan and plan.item == kill and plan.gain == 'mana_per_kill' and plan.slot == 7,
    'it is worn, in place of the ring that costs her least')
end

do -- faster cast rate counts only where it reaches a step
  local slow = item { code = 'cap', slots = { [1] = true }, stats = { [CAST] = { [0] = 5 } } }
  reset { slow }
  local sheet = gear:sheet(me)
  local with = gear:figures(gear_lib.change(sheet, {}, { gear:read(slow, sheet.base) }))
  assert(with.cast == gear:figures(sheet).cast, 'five percent reaches no step: no upgrade in itself')
  slow.stats[CAST][0] = 9
  with = gear:figures(gear_lib.change(sheet, {}, { gear:read(slow, sheet.base) }))
  assert(with.cast > gear:figures(sheet).cast, 'nine percent takes a frame off the cast')
end

do -- effective life is life by the average of her resistances
  reset {}
  me.stats = { [FIRE] = { [0] = 40 }, [LIGHT] = { [0] = 0 }, [COLD] = { [0] = 0 }, [POISON] = { [0] = 0 } }
  local figures = gear:figures(gear:sheet(me))
  assert(math.abs(figures.life - 60 / 0.92) < 1e-9, 'sixty life at eight percent average resistance over the five')
  me.stats[BLOCK] = { [0] = 50 }
  assert(math.abs(gear:figures(gear:sheet(me)).life - 60 / 0.82) < 1e-9, 'block is resistance to blows')
  local vitality = item { code = 'cap', slots = { [1] = true }, stats = { [VIT] = { [0] = 5 } } }
  local resists = item { code = 'skp', slots = { [1] = true }, stats = { [LIGHT] = { [0] = 40 } } }
  reset { vitality, resists }
  me.stats = { [FIRE] = { [0] = 40 } }
  local plan = gear:plan(me)
  -- +10 life: 70 / 0.92 = 76.1; +40 lightning: 60 / 0.84 = 71.4.
  assert(plan and plan.item == vitality and plan.gain == 'life', 'the cap that leaves her more effective life is worn')
end

do -- the order below life: mana to a potion's size, then speed, then the rest
  local boots = { [9] = true }
  local fast = item { code = 'lbt', slots = boots, stats = { [RUN] = { [0] = 10 } } }
  local hard = item { code = 'vbt', slots = boots, stats = { [DEFENSE] = { [0] = 30 } } }
  reset { fast, hard }
  assert(gear:plan(me).item == fast, 'run speed is worth more than defence')
  local mana = item { code = 'tbt', slots = boots, stats = { [MANA] = { [0] = 30 * 256 } } }
  reset { fast, mana }
  me.max_mana = 20
  assert(gear:plan(me).item == mana, 'mana she lacks to hold a potion is worth more than speed')
  me.max_mana = 40
  assert(gear:plan(me).item == fast, 'mana beyond a potion is worth less')
  local energy = item { code = 'tbt', slots = boots, stats = { [ENERGY] = { [0] = 5 } } }
  reset { energy }
  assert(gear:plan(me).gain == 'mana', 'energy is mana at her class rate')
  local sash = item { code = 'lbl', slots = { [8] = true } }
  reset { sash }
  assert(gear:plan(me).gain == 'belt', 'a sash is worn for its potion slots when nothing else speaks')
end

do -- what cannot be judged or worn is left alone
  local unknown = item { code = 'sst', slots = hands(), two_handed = true, identified = false, stats = { [SINGLE] = { [38] = 3 } } }
  local heavy = item { code = 'lea', slots = { [3] = true }, needs = 15, stats = { [LIFE] = { [0] = 20 * 256 } } }
  local stashed = item { code = 'cap', slots = { [1] = true }, page = 4, stats = { [LIFE] = { [0] = 20 * 256 } } }
  reset { unknown, heavy, stashed }
  assert(gear:plan(me) == nil, 'an unidentified item, one she lacks the strength for and one in the stash change nothing')
  me.strength = 15
  assert(gear:plan(me).item == heavy, 'with the strength for it the armour is worn')
  local same = item { code = 'cap', slots = { [1] = true } }
  reset { same }
  assert(gear:plan(me) == nil, 'an item that changes no figure is not worth the clicks')
end

do -- room for what comes off
  local wand = item { code = 'wnd', slots = hands(), location = 'equipped', body_location = RIGHT, width = 1, height = 2 }
  local staff = item { code = 'sst', slots = hands(), two_handed = true, width = 1, height = 3, grid_x = 0, grid_y = 0,
    stats = { [SINGLE] = { [38] = 1 } } }
  reset { wand, staff }
  local plan = gear:plan(me)
  assert(plan and gear_lib.room(plan), 'the wand goes where the staff lay')
  local filler = {}
  for x = 0, 9 do
    for y = 0, 3 do
      if not (x == 0 and y < 3) then filler[#filler + 1] = item { code = 'gld', grid_x = x, grid_y = y } end
    end
  end
  local full = { wand, staff }
  for _, cell in ipairs(filler) do full[#full + 1] = cell end
  reset(full)
  plan = gear:plan(me)
  assert(plan and gear_lib.room(plan), 'in a full pack the staff leaves just the cells the wand needs')
  wand.width = 2
  assert(not gear_lib.room(plan), 'and none for a wider item')
end

do -- what is worth picking up
  local head = { [1] = true }
  local LOW, NORMAL, SUPERIOR, MAGIC, SET, RARE, UNIQUE = 1, 2, 3, 4, 5, 6, 7
  local function worn(quality, stats)
    return item { code = 'cap', slots = head, location = 'equipped', body_location = 1, quality = quality, stats = stats }
  end
  local function found(quality, extra)
    local it = item { code = 'skp', slots = head, location = 'ground', quality = quality, identified = false }
    for k, v in pairs(extra or {}) do it[k] = v end
    return it
  end
  local function wanted(drop, on)
    reset(on and { on, drop } or { drop })
    return (gear:candidate(me, drop))
  end
  -- Unread: by quality against what it would take the place of.
  assert(wanted(found(MAGIC), worn(MAGIC)) and wanted(found(MAGIC), worn(NORMAL)), 'a magic find for a slot that holds magic or less')
  assert(not wanted(found(MAGIC), worn(RARE)) and not wanted(found(MAGIC), worn(SET)), 'not for a slot that holds better')
  assert(wanted(found(RARE), worn(RARE)) and wanted(found(RARE), worn(MAGIC)), 'a rare find for a slot that holds rare or less')
  assert(wanted(found(UNIQUE), worn(SET)) and wanted(found(SET), worn(UNIQUE)) and wanted(found(RARE), worn(UNIQUE))
    and wanted(found(UNIQUE), worn(RARE)), 'unique, set and rare stand together at the top')
  assert(wanted(found(MAGIC, { needs = 30 })), 'a magic one may ask for less than it seems: taken to be read')
  local charm = item { code = 'cm1', location = 'ground', quality = MAGIC, identified = false }
  assert(not wanted(charm), 'a charm goes in no slot: it is for sale, not for wearing')
  -- A two-handed find is set against both hands.
  local wand = item { code = 'wnd', slots = hands(), location = 'equipped', body_location = RIGHT, quality = MAGIC }
  local shield = item { code = 'buc', slots = hands(), location = 'equipped', body_location = LEFT, quality = RARE }
  local staff = item { code = 'sst', slots = hands(), two_handed = true, location = 'ground', quality = MAGIC, identified = false }
  reset { wand, shield, staff }
  assert(not gear:candidate(me, staff), 'a magic staff is not picked up to displace a rare shield')
  shield.quality = MAGIC
  assert(gear:candidate(me, staff), 'against a magic one it is')
  -- Known where it lies: weighed as it would be in the pack.
  local DEFENSE = 31
  local plain = item { code = 'skp', slots = head, location = 'ground', quality = NORMAL, stats = { [DEFENSE] = { [0] = 8 } } }
  assert(wanted(plain), 'a plain cap for a bare head is a gain')
  assert(not wanted(plain, worn(NORMAL, { [DEFENSE] = { [0] = 8 } })), 'none over a cap as good')
  assert(wanted(plain, worn(LOW, { [DEFENSE] = { [0] = 3 } })), 'one over a worse cap')
  plain.needs = 30
  assert(not wanted(plain), 'and none while she cannot wear it')
  local bow = item { code = 'sbw', slots = hands(), two_handed = true, location = 'ground', quality = NORMAL,
    stats = { [21] = { [0] = 1 }, [22] = { [0] = 4 } } }
  local held = item { code = 'sst', slots = hands(), two_handed = true, location = 'equipped', body_location = RIGHT, quality = NORMAL }
  reset { held, bow }
  assert(not gear:candidate(me, bow), 'a short bow gives a sorceress nothing: left where it lies')
  reset { bow }
  assert(not gear:candidate(me, bow), 'even with her hands empty')
  assert(gear_lib.wearable(bow) and not gear_lib.wearable(charm), 'gear is what goes in a body slot')
end

do -- no change is ever a gain over nothing: the thing worn is counted as worn
  local sash = item { code = 'lbl', slots = { [8] = true }, location = 'equipped', body_location = 8 }
  local other = item { code = 'lbl', slots = { [8] = true } }
  reset { sash, other }
  assert(gear:plan(me) == nil, 'a second sash is no gain over the one she wears')
  local belt = item { code = 'mbl', slots = { [8] = true } }
  reset { sash, belt }
  assert(gear:plan(me).item == belt, 'a belt with more rows is')
  -- A caster's weapon is not weighed by its blow: a bow and a staff are the same to her.
  local MIN, MAX = 21, 22
  local staff = item { code = 'sst', slots = hands(), two_handed = true, location = 'equipped', body_location = RIGHT,
    stats = { [MIN] = { [0] = 1 }, [MAX] = { [0] = 5 } } }
  local bow = item { code = 'sbw', slots = hands(), two_handed = true, stats = { [MIN] = { [0] = 1 }, [MAX] = { [0] = 9 } } }
  reset { staff, bow }
  assert(gear:plan(me) == nil, 'she does not change her staff for a bow')
  -- A build that fights with its weapon does weigh it, and does not change back.
  local fighter = gear_lib.new({ melee_skill = 'Bash', attacks = {} }, { power = power })
  local plan = fighter:plan(me)
  assert(plan and plan.item == bow and plan.gain == 'melee', 'the harder blow is worn by one who strikes')
  staff.location, staff.body_location, bow.location, bow.body_location = 'container', nil, 'equipped', RIGHT
  assert(fighter:plan(me) == nil, 'and what came off is no gain over it in turn')
  assert(gear:plan(me, { [staff.id] = true }) == nil, 'nor is anything on the list of what has come off considered')
end

print('gear comparison regressions passed')
