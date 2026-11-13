-- Class build data: point allocation, supplies and neural action configuration.
--
-- A build has:
--   element     main damage type, for weighing monsters against resistances
--   range       preferred attack distance in subtiles
--   stats       stat ids spent in rotation (0 str, 1 energy, 2 dex, 3 vit)
--   skills      skill names in the order points are spent (repeat a name to rank it up)
--   den_level   character level at which the Den of Evil is worth entering
--   den_marker  true: open with the starting scroll's portal beside the Den,
--               clear on to the Cold Plains waypoint and level 2, and come
--               back through the portal (game/act1.lua, OPENING)
--   enter_gap   levels an area's monsters may be above the character before
--               it levels up nearby instead of walking in (nil: no such gate)
--   follow_pay  true: no area is gated by level. After the Den she is always
--               where the optimizer says a kill pays best (lib/leveling),
--               and falling pay in out-levelled areas moves her on through
--               the act (game/act1.lua, REFRESH). enter_gap is then unused
--   players_by_gold  true: the players setting follows her gold at all times
--               after the Den (game/act1.lua, PX), and nothing else sets it
--   gold_farm   false: never detour to an easy area for gold when the mana
--               reserve is short (default: do)
--   tower_prep  whether the optional Forgotten Tower preparation is considered:
--               true (as often as the strategy network chooses), 'once' (until
--               the Countess has been killed), or false
--   buffs       self-buff skill names kept up outside of close combat
--   mana_first  buy mana before healing supplies for a spell-dependent build
--   melee_skill optional learned melee skill offered alongside basic attacks

local builds = {}

builds.barbarian = {
  element = 'physical',
  den_level = 3,
  range = 2.8,
  stats = { 3, 3, 3, 0, 0 },
  skills = { 'Bash', 'Bash', 'Bash', 'Sword Mastery', 'Axe Mastery', 'Howl', 'Double Swing' },
  melee_skill = 'Bash',
}

builds.sorceress = {
  element = 'lightning',
  den_level = 1, -- the Den's monsters are the Moor's level and denser; without the opening below she goes straight in
  den_marker = true, -- with it she comes back at level 2, with her first spell
  -- Whether the Blood Moor is searched for gold, until she can pay for six
  -- mana potions, before she goes on to the Cold Plains. Off: the
  -- maintainer's target is the Den beaten by 4:30, and the search alone
  -- took 300 to 600 seconds in four runs and on an unlucky map left her as
  -- poor as before. What she finds on the way is sold in town before the
  -- portal as it is (306 gold and ten mana potions in the one run where the
  -- stage ended at once, and that Den was cleared in 142 seconds).
  moor_first = false,
  mana_first = true,
  -- One level behind an area's monsters the route holds. Two behind (7 in the
  -- Barracks, 9 in the Catacombs) nearly every pack forced an emergency
  -- portal or a chicken; three behind, with the Tower skipped as well, she
  -- reached the Barracks at level 6 four minutes sooner and could neither
  -- survive nor pay for potions there (seven exits in five minutes).
  enter_gap = 1,
  -- The Tower is where the first gold and levels come from: without it she
  -- arrives at the monastery broke. The strategy network decides how long.
  players_by_gold = true,
  follow_pay = true, -- the maintainer's experiment of 2026-11-12; enter_gap above is the margin it replaces
  -- No detour to an easy area for gold: where she goes is the optimizer's
  -- choice alone. With both rules she rode the waypoint between the Black
  -- Marsh (short of mana: go and earn in the Cold Plains) and the Cold
  -- Plains (resources will do: go where a kill pays best, the Tower) for as
  -- long as the game ran. Being broke means one player and opened chests.
  gold_farm = false,
  tower_prep = false, -- with it the Tower is the optimizer's to choose, not a rule's
  range = 7,
  buffs = { 'Frozen Armor' },
  stats = { 3 },
  -- Strength to 15 (the maintainer's figure: "so we can wear decent
  -- armors"; she starts at 10, and leather armour, a skull cap and a buckler
  -- ask for 12 to 15), with the points of level 4: those of levels 2 and 3
  -- go to Vitality ("we need that early vitality to help protect against
  -- chickens"; moved a level later on 2026-11-13, when the Den was still
  -- going badly), and so does everything after strength is there.
  strength = 15,
  strength_level = 4,
  -- What lib/gear reckons with. Life and mana a point gives (CharStats.txt
  -- LifePerVitality and ManaPerMagic, in quarters there: 8 and 8).
  life_per_vitality = 2,
  mana_per_energy = 2,
  -- The skills that can be her attack, with the skill tab items name them by
  -- (class x 8 + page: fire 8, lightning 9, cold 10).
  attacks = {
    { name = 'Charged Bolt', id = 38, tab = 9 },
    { name = 'Fire Bolt', id = 36, tab = 8 },
    { name = 'Ice Bolt', id = 39, tab = 10 },
  },
  -- Frames a cast takes by faster cast rate (her casting animation's steps,
  -- from memory of the data like the potion table in lib/supplies: used to
  -- rank gear, never to time a cast).
  cast_frames = { { 0, 13 }, { 9, 12 }, { 20, 11 }, { 37, 10 }, { 63, 9 }, { 105, 8 }, { 200, 7 } },
  -- Icy Veins' fastest-leveling opening: four Charged Bolts with the Den reward,
  -- save level 5 for Frost Nova and Static Field together at 6, then more bolts.
  -- https://www.icy-veins.com/d2/sorceress-fastest-leveling-build
  skills = {
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
    'Frost Nova', 'Static Field',
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
    'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt', 'Charged Bolt',
  },
}

-- Any other class fights with its basic attack until it gets a build.
builds.default = {
  element = 'physical',
  den_level = 3,
  range = 2.8,
  stats = { 3, 0 },
  skills = {},
}

function builds.for_class(class)
  return builds[class] or builds.default
end

return builds
