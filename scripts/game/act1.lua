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
--   5. A strategy network chooses optional Tower preparation from level 5, capped at 11.
--      Otherwise farm only for the first skill or supplies, fighting useful packs en route.
--   6. Andariel: found from the level's preset spawns; done when the quest
--      record credits her kill.

local fsm = require 'lib.fsm'
local builds = require 'lib.builds'
local combat_lib = require 'lib.combat'
local explore = require 'lib.explore'
local leveling = require 'lib.leveling'
local gear_lib = require 'lib.gear'
local loot_lib = require 'lib.loot'
local move = require 'lib.move'
local services = require 'lib.services'
local stamina = require 'lib.stamina'
local stock = require 'lib.supplies'
local strategy_lib = require 'lib.strategy'
local travel = require 'lib.travel'
local world = require 'lib.world'
local value = require 'lib.value'

local TOWN, DEN, CATACOMBS_4 = 1, 8, 37
local COLD_PLAINS = 3 -- earn early recovery supplies on the main route after the Den
local ANDARIEL_CLASS = 156
local QUEST_DEN, QUEST_ANDARIEL = 1, 6
local QUEST_TOWER, COUNTESS_CELLAR = 5, 25 -- Forgotten Tower; Tower Cellar Level 5
local TOWER_PREP_START, TOWER_MAX_LEVEL = 5, 11 -- optional preparation starts at five; eleven is a hard ceiling
local TOWER_FAILURE_LIMIT = 3 -- a missing Countess must not restart preparation forever
local QUEST_RESPEC = 41      -- Akara's free reset, armed by the Den of Evil
local QUEST_SYNC_MS = 10000  -- reset if the client never receives a fresh quest record
local QUEST_RETRY_MS = 1000 -- allow a UI action/reply before retrying, at most three opens
local QUEST_REOPEN_MS = 250 -- the native quest panel rejects reopening within 250 ms
-- Black Marsh, Outer Cloister, Jail 1 (a chicken in the Jail otherwise re-walks
-- the Barracks: two of them cost six minutes), Inner Cloister (its waypoint
-- stands at the way in from the Jail), Catacombs 2
local WAYPOINT_ANCHORS = { [6] = true, [27] = true, [29] = true, [32] = true, [35] = true }
local ANCHOR_WAYPOINT_MS, OPTIONAL_WAYPOINT_MS = 90000, 30000 -- restart anchors must survive nearby fights and long approaches
-- A waypoint she passes this close to and does not have is taken on the way,
-- wherever she is going and whatever she is doing: a few steps now against a
-- walk across the act later.
local WAYPOINT_NEAR = 25
local RECOVERY_WAYPOINT_MS = 45000 -- bound the early supply detour while avoiding repeated walks through the Moor
-- The opening (builds with den_marker). The scroll every character starts with
-- opens a town portal beside the Den's entrance; she fights on to the Cold
-- Plains waypoint, which brings level 2 and her first spell, rides it to town
-- and steps through the portal to clear the Den with both. Then a new game:
-- Akara, the shop, and the waypoint back to the Cold Plains, with no ground
-- walked twice.
--   reach      subtiles from the entrance at which the portal is opened
--   cast_ms    a scroll that showed no portal in this long is read again
--   casts      readings before the plan is dropped
--   level      the level of her first spell: until it she fights everything in sight
--   tough      but on the way out nothing that would take more than this many
--              casts, unless it is upon her: at eight players a Blood Moor
--              zombie takes ten Fire Bolts or twenty blows, and Fallen are
--              the same experience for a quarter of either
--   level_first  whether the Den waits for that level (off: the maintainer's
--              trial; she is level 2 soon enough, and the wait by the waypoint
--              was where she ran dry and opened a second portal)
--   moor_players  the players setting in the Blood Moor on the way out (the
--              runner the maintainer watched: eight there, one for the Den).
--              Experience is four and a half times as much a kill, so her
--              first spell comes a handful of Fallen in, and more drops:
--              the gold and potions the Den is otherwise fought without
--   level_ms   longest stay in the Cold Plains for that level before going anyway
--   waypoint_ms  time for the walk across the Cold Plains to its waypoint, fights included
--   return_ms  time to find the portal in town and step through
local OPENING = { reach = 8, cast_ms = 4000, casts = 3, level = 2, tough = 3, level_first = false, moor_players = 8,
  level_ms = 120000,
  waypoint_ms = 150000, return_ms = 45000 }
local WAYPOINT_ABSENT_REACH, WAYPOINT_ABSENT_MS = 8, 2000 -- this close for this long without the object: it is not there
local HIRE_MIN_GOLD = 300    -- a Normal Act 1 Rogue costs a few hundred
local ENTER_ABOVE = 1        -- resource farms stay near our level
-- Chicken: leave the game (instant, unlike a portal cast) when a fight turns.
local CHICKEN_CROWDED, CHICKEN_LOW = 0.25, 0.15 -- with 2+ monsters close / any monster near
local CHICKEN_BURST, BURST_BELOW = 0.30, 0.40    -- that much life lost fast, ending below this
local BURST_MS = 2000
local PORTAL_DANGER_LIFE, PORTAL_DANGER_LOSS = 0.60, 0.20 -- portal before damage reaches chicken thresholds
-- A fight the combat policy projects to end with this much of our life budget
-- is finished where we stand; leaving it for town hands the monsters the time.
local COMMIT_OUTLAST = 0.4
-- A boss does not heal while we visit town, but a new game restores her, and
-- every exit costs the walk back: one that has less of her life left than
-- this share of ours is finished rather than left. Leaving for want of
-- potions asks for the least lead, a portal under fire for more, the
-- early chicken for the most; the last-ditch chickens ask for nothing.
-- And a boss with a tenth of her life left is a cast or two from dead: only
-- the lowest threshold still leaves. Twice in one run Andariel was at 8%
-- when a chicken at 21% and at 36% of our life gave her all of hers back.
local RACE = { hurt = 1, portal = 0.5, chicken = 0.3, finish = 0.1 }
-- In a boss fight life is bought back in town below this. Potions that are
-- not keeping up end in a chicken, and a new game gives the boss her life
-- back: Andariel was at 17% when a slide from 72% to 18% over fourteen
-- seconds, six healing potions deep, left the game. (At 0.4 a blow on the
-- way out took her from 50% to 28%, too little to open a portal on.)
local BOSS_PORTAL_LIFE = 0.5
-- The third emergency portal from one place is a new game instead. A portal
-- comes back to the pack that caused it: a lightning pack beside the Inner
-- Cloister waypoint sent her home seven times in ninety seconds, and the new
-- game that ended it had other monsters there.
local EMERGENCY_REACH, EMERGENCY_LIMIT = 30, 3
local PORTAL_ESCAPE_MS = 4000 -- an urgent outbound attempt must not become an endless fight
local PORTAL_CAST_RETRY_MS = 500 -- throttle a refused item request without pretending a portal is opening
local UNSAFE_RETURN_HOPS = 1 -- walk at most this many levels to avoid returning through a contested portal
-- A boss left with her company in sight is rejoined on foot from this many
-- levels away: they gather at the portal while we are in town. Andariel was
-- at 25% when a portal opened beside her hall came back to five Afflicted,
-- five Dark Ones and their shamans: 68% of a full life in two seconds, a new
-- game, and her life back. Without the boss in sight the portal is the way
-- back as usual: the walk passes the same monsters and more besides.
local BOSS_RETURN_HOPS, BOSS_CROWD = 2, 2
local FROZEN_MS = 5000      -- unable to act this long: leave the game
local NO_GAIN_MS = 240000   -- no experience, gold, kill, new area or target damage this long: start a new game
local NOT_FARMED = { [1] = true, [38] = true, [39] = true } -- town, Tristram, Cow Level
-- Supplies: mana potions and rejuvenations carried plus those the gold buys.
-- Below SUPPLY_LOW pushing on is pointless (she runs dry before looting), so
-- she farms gold in an easy area near a waypoint until SUPPLY_OK is carried.
local SUPPLY_LOW, SUPPLY_OK = 2, 6 -- a resource detour needs an actual shortage
local EARLY_FORWARD_AREAS = { [3] = true, [4] = true, [10] = true, [5] = true } -- main route before Black Marsh
local EARLY_FORWARD_LIFE, EARLY_FORWARD_MANA = 0.85, 0.75 -- current resources can bridge one early travel reserve
local MANA_POTION_GOLD = 60
local HEALING_POTION_GOLD = 30 -- Normal Akara's minor healing potion
local SCROLL_GOLD = 100        -- a town portal scroll, with room to spare
local GOLD_FARM_BELOW = 3   -- gold farms have monsters at least this many levels below us
local FARM_DRY_MS = 90000   -- abandon a money sweep that cannot add one mana potion's budget
local TRAVEL_SIGHT = 12      -- worthwhile packs in reach; close threats always count
local ELITE_RISK = 0.3       -- an elite pack at or past the policy's escape threshold is not sought out on the way
-- What a fight pays is experience per point of damage her element gets
-- through (value.kill: resistance, an elite's life and experience, the level
-- penalty), which for a caster is experience per mana. The measure of good
-- and bad is what the monsters around her level pay: the average over the
-- act's areas whose monster level is within WORTH.below under hers and the
-- build's enter_gap over it (the current area alone would call everything
-- usual where everything is poor). A monster that pays less than WORTH.share
-- of that is not worth the mana: it is fought when it is upon her and left
-- alone otherwise, on the way and while levelling. Where everything must die
-- (the Den, a boss and her company) the rule does not apply. The share is
-- the maintainer's: nine tenths of the average, so that little below it is
-- fought (0.4 was "still too much"). For scale, a unique pays 1.25 of its
-- kind (five times the experience over four times the life), a champion 1,
-- a kind half resistant 0.5, a Lightning Enchanted unique 0.31.
local WORTH = { share = 0.9, below = 2, first = 2, last = 39 } -- first, last: the act's level ids outside town
local CLEAR_SIGHT = 24       -- fight everything this close while clearing or farming
local RETREAT_LIFE = 0.50   -- leave time to open a portal after the last healing potion
local RETREAT_MS = 60000   -- a blocked walk home must not replace a quick reset with endless combat
local DEN_RETREAT_MS = 150000 -- out of the Den a reset is not quick: it undoes the clear
local RECOVERED_LIFE, RECOVERED_MANA = 0.8, 0.5 -- a level gained on the walk home refills both: turn back
local POTIONS = { hp = 4, mp = 10, vps = 4, yps = 2, tsc = 1 }
-- In the Catacombs the fights are paid for in life: twice the healing.
local DEEP_POTIONS = { hp = 8, mp = 10, vps = 4, yps = 2, tsc = 1 }
local CATACOMBS_1 = 34
local BOSS_HEALING = 3 -- healing doses to walk up to a boss with
local ADDS_SIGHT = 16 -- a boss's company this close is fought before the boss is singled out
-- Andariel is met back along the way in, not in her hall: she outruns her
-- company, which then arrives through the door a few at a time.
local LURE_BACK, LURE_MIN, LURE_MS = 30, 12, 6000 -- subtiles walked back; least worth walking; give up after
local TRAIL_STEP, TRAIL_POINTS = 4, 60 -- breadcrumbs of the approach

local me0 = game.me()
local build = builds.for_class(me0.class)
local combat = combat_lib.new(build)
local planner = strategy_lib.new()
print('act1: combat controller neural ' .. combat.neural.version)
local loot = loot_lib.new()
local gather = loot_lib.new(30) -- wider sweep before leaving a level
local containers = require('lib.containers').new(12)
-- A pack that is not worth the fight is gone around, not walked into: a town
-- portal out of one and a new game after it cost the whole walk again. An
-- elite whose pack would take ELITE_RISK of her life is remembered where it
-- was seen, and travel plans around that ground while a way round exists
-- that is not much longer (map.path's avoid option).
-- Ground that only a shooter reaches is another matter (the maintainer's
-- rule): she may walk through it, and slips past what shoots, but does not
-- stand and fight on it. Planned around like the rest, whole stretches of a
-- level were closed to her, and a level full of Quill Rats or archers was
-- hardly searched at all. Such a zone is marked `pass`.
--   sight     how far such packs are judged (pack_lethality sees this far)
--   melee, ranged  subtiles kept from one that must close in, and from one that shoots
--   ms        how long a pack out of sight is still taken to stand where it was
--   moved     a pack that has moved this far is planned around afresh
--   check_ms  how often the monsters in sight are judged
--   leave_ms  how long she tries to walk out of ground kept off before fighting where she is
--   step_ms   between the steps of that walk
--   cross_ms  how long she keeps to her route across a shooter's ground with
--             something upon her before she turns on it (a zone is crossed in four seconds)
--   replan_ms  least time between two plannings of a route for what packs
--             have done: a pack that shifts makes a way round worth it one
--             moment and not the next, and planned afresh each time the
--             route went round and through by turns, six times in nine seconds
local AVOID = { sight = 30, melee = 12, ranged = 18, ms = 45000, moved = 6, check_ms = 400,
  leave_ms = 3000, step_ms = 300, cross_ms = 6000, replan_ms = 2500 }
local avoided, avoid_checked = {}, 0
local function avoid_zones()
  local me, t, zones = game.me(), charon.now(), {}
  for id, pack in pairs(avoided) do
    local unit = game.unit('monster', id)
    if pack.area ~= me.area or t - pack.at > AVOID.ms or (unit and unit.alive == false) then
      avoided[id] = nil
    else
      zones[#zones + 1] = pack
    end
  end
  return zones
end
-- The ground routes go around: not what a shooter merely reaches.
local function solid_zones()
  local zones = {}
  for _, zone in ipairs(avoid_zones()) do
    if not zone.pass then zones[#zones + 1] = zone end
  end
  return zones
end

local trip = travel.new({ avoid = solid_zones })
local mover = move.new()
-- Spell builds run on mana: buy mana potions first.
local POTION_ORDER = build.mana_first and { 'mp', 'vps', 'hp', 'yps' } or { 'hp', 'vps', 'mp', 'yps' }
local visit
local explorer
local tower_px_exposure = 1 -- highest acknowledged Tower XP count this game; lowering does not unscale monsters
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

-- What a town visit stocks up to. A town portal scroll comes before any
-- potion, before the Den as after it (the maintainer's rule): in the Den
-- town is a three-minute walk there and back without one. Left out before
-- the Den, she came home through a portal with 1,141 gold, bought sixteen
-- potions and went back with no scroll for the next time.
local unread -- forward: what she carries that is worth reading (see below)
local function new_visit()
  local wanted = {}
  for kind, count in pairs(flag('deep') and DEEP_POTIONS or POTIONS) do wanted[kind] = count end
  -- A scroll of identify for each find she means to read.
  local me = game.me and game.me()
  wanted.isc = me and unread and #unread(me) or 0
  return services.new(wanted, POTION_ORDER)
end
visit = new_visit()

-- Who is around, for the log lines that explain a retreat: "Dark Ranger x4, Bone Ash*".
local function crowd(me)
  local counts, order = {}, {}
  for _, unit in ipairs(combat:hostiles(me, CLEAR_SIGHT)) do
    local name = (unit.name or '?') .. ((unit.unique or unit.superunique) and '*' or unit.champion and '+' or '')
    if not counts[name] then counts[name], order[#order + 1] = 0, name end
    counts[name] = counts[name] + 1
  end
  for i, name in ipairs(order) do order[i] = counts[name] > 1 and name .. ' x' .. counts[name] or name end
  return #order > 0 and table.concat(order, ', ') or 'nothing in sight'
end

-- What a kill pays at this character level (see WORTH), or false.
local level_pay = {}
local function usual_pay(me)
  local usual = level_pay[me.level]
  if usual ~= nil then return usual end
  local total, count, names = 0, 0, {}
  local function add(id)
    local area = value.area(id, build.element, me.level)
    if area and area.experience_per_damage > 0 then
      total, count = total + area.experience_per_damage, count + 1
      names[#names + 1] = game.level_name(id)
    end
  end
  for id = WORTH.first, WORTH.last do
    local info = game.level_info(id)
    local level = info and info.monster_level
    if level and level >= me.level - WORTH.below and level <= me.level + (build.enter_gap or 1) then add(id) end
  end
  if count == 0 then add(me.area) end -- nothing around her level: where she is must do
  usual = count > 0 and total / count or false
  level_pay[me.level] = usual
  if usual then
    print(string.format('act1: at level %d a kill pays %.3f experience per point of %s damage (%s)',
      me.level, usual, build.element, table.concat(names, ', ')))
  end
  return usual
end

-- The share of that which this monster pays, or nil when it cannot be judged.
local function pay(me, unit)
  if not unit.class_id or not value.monster(unit.class_id) then return nil end
  local usual = usual_pay(me)
  if not usual then return nil end
  local _, _, per_damage = value.kill(unit, build.element, me.level)
  -- A unit's life counts the players setting; the areas' averages do not.
  return per_damage * value.players_life() / usual
end

-- What shoots is never worth it: it is gone around (see AVOID), and fought
-- only when it is upon her. Nor is anything that stands where a shooter
-- reaches, or on other ground kept off: a fight there is paid for in what
-- lands on her meanwhile, whatever the monster itself would pay.
local function pays(me, unit)
  if unit.ranged then return false end
  local share = pay(me, unit)
  return share == nil or share >= WORTH.share
end
local function worth(me, unit)
  if not pays(me, unit) then return false end
  if unit.x then
    for _, zone in ipairs(avoid_zones()) do
      if (unit.x - zone.x) ^ 2 + (unit.y - zone.y) ^ 2 <= zone.radius ^ 2 then return false end
    end
  end
  return true
end

-- Standing on ground a dangerous pack holds, with nothing upon her, she
-- walks straight out of it before anything else. With `shooters`, the same
-- for ground a shooter reaches, and whether or not something is upon her:
-- that is asked when she is about to fight there (the maintainer's rule:
-- one in range makes her dodge through any fight, and what is upon her
-- comes along when she steps out). True while she walks.
local leaving_solid, leaving_pass = {}, {}
local function leave_reach(me, shooters)
  local leaving = shooters and leaving_pass or leaving_solid
  local worst, depth
  for _, zone in ipairs(avoid_zones()) do
    local inside = zone.radius - world.distance(me, zone)
    if (zone.pass or false) == (shooters or false) and inside > 0 and (not depth or inside > depth) then
      worst, depth = zone, inside
    end
  end
  if not worst or (not shooters and #combat:hostiles(me, 5) > 0) then
    leaving.since = nil
    return false
  end
  local t = now()
  leaving.since = leaving.since or t
  if t - leaving.since > AVOID.leave_ms then return false end -- no way out this way: deal with what is here
  if act.ready() and t - (leaving.stepped or 0) >= AVOID.step_ms then
    leaving.stepped = t
    local away = math.max(0.1, world.distance(me, worst))
    local x = me.x + (me.x - worst.x) / away * (depth + 4)
    local y = me.y + (me.y - worst.y) / away * (depth + 4)
    if act.move(math.floor(x), math.floor(y), stamina.walk(me)) == false then leaving.since = t - AVOID.leave_ms - 1 end
  end
  return true
end

-- On her way across ground a shooter reaches she keeps to her route with
-- something upon her, for AVOID.cross_ms: that is how she slips past. True
-- while she should walk on and not fight.
local crossing = {}
local function cross_reach(me)
  local t, inside = now(), false
  for _, zone in ipairs(avoid_zones()) do
    if zone.pass and world.distance(me, zone) < zone.radius then inside = true; break end
  end
  if not inside or t - (crossing.asked or 0) > 2000 then crossing.since = nil end
  crossing.asked = t
  if not inside then return false end
  crossing.since = crossing.since or t
  return t - crossing.since <= AVOID.cross_ms
end

-- Whom to fight while levelling: what pays, and whatever is upon her.
local function worth_filter(me)
  return function(unit) return world.distance(me, unit) <= 5 or worth(me, unit) end
end

-- Judges the elites in sight. True when there is something new to plan around.
local function note_packs(me)
  local t = now()
  if t - avoid_checked < AVOID.check_ms then return false end
  avoid_checked = t
  local changed = false
  for _, unit in ipairs(combat:hostiles(me, AVOID.sight)) do
    -- Elites, and anything that shoots: a pack of Tainted is no elite, and
    -- its lightning took most of a level-11 life from across the room.
    if unit.x and (unit.unique or unit.champion or unit.minion or unit.superunique or unit.ranged) then
      local known = avoided[unit.id]
      local risk = not known and combat:pack_lethality(me, unit)
      -- By what it pays alone: an elite is not kept off for standing on
      -- ground kept off, or every zone would grow by its neighbours.
      local poor = not known and not pays(me, unit)
      if known then
        known.at = t
        if world.distance(known, unit) >= AVOID.moved then
          known.x, known.y = unit.x, unit.y
          if not known.pass then changed = true end
        end
      elseif risk >= ELITE_RISK or poor or unit.ranged then
        -- A shooter whose pack is no danger is passed, not gone around.
        local pass = unit.ranged and risk < ELITE_RISK or nil
        avoided[unit.id] = { x = unit.x, y = unit.y, radius = unit.ranged and AVOID.ranged or AVOID.melee,
          area = me.area, at = t, pass = pass }
        if not pass then changed = true end
        print(string.format('act1: keeping off %s%s at %.0f (%s)', tostring(unit.name), unit.ranged and ' (ranged)' or '',
          world.distance(me, unit), pass and 'what shoots is passed by, not fought'
            or risk < ELITE_RISK and poor and string.format('it pays %.0f%% of what a kill at her level does, resisting %d%% of %s',
              (pay(me, unit) or 0) * 100, value.resist(unit, build.element), build.element)
            or string.format('its pack would take %.0f%% of what is left', risk / (1 - risk) * 100)))
      end
    end
  end
  return changed
end

-- Plans the route in hand afresh for what the packs in sight have done, and
-- says in the log whether it goes round them or through. `who` names the state.
local replanned = { at = 0 }
local function replan_around(m, me, who)
  local changed = note_packs(me)
  local t = now()
  replanned.due = replanned.due or changed
  if not replanned.due or (t - replanned.at < AVOID.replan_ms and t >= replanned.at) then return end
  replanned.due, replanned.at = false, t
  trip:replan()
  local state = trip.mover.detour and 'around' or trip.mover.through and 'through' or nil
  if state and state ~= m.data.avoid_logged then
    print(state == 'around' and who .. ': going around what is kept off'
      or who .. ': no way around what is kept off; fighting it from range')
  end
  m.data.avoid_logged = state
end

local function travel_filter(me)
  local pack = #combat:hostiles(me, 8) >= 4
  local path = trip.hop and trip.hop.from == me.area and trip.mover.path
  local onward = path and path[trip.mover.index or 1]
  return function(unit)
    local distance = world.distance(me, unit)
    if distance <= 5 then return true end -- anything that can block or hit us
    if unit.id == combat.target_id and distance <= build.range and (unit.life_percent or 100) < 25 then return true end
    -- A pack kept off that the route cannot go around is fought from range,
    -- on purpose, rather than walked into: that comes before what it pays.
    if trip.mover.through and avoided[unit.id] and not avoided[unit.id].pass then return true end
    if not worth(me, unit) then return false end
    -- Elites are worth several kills, but only where their pack is one we
    -- can stand in: Bone Ash's took half a level-12 life bar in two seconds.
    if (unit.unique or unit.superunique or unit.boss or unit.champion or unit.minion)
      and combat:pack_lethality(me, unit) < ELITE_RISK then return true end
    if onward then
      local dx, dy = onward.x - me.x, onward.y - me.y
      local forward = (unit.x - me.x) * dx + (unit.y - me.y) * dy
      if forward < 0 then return false end
      -- Start fighting blockers before travel walks into their attack range.
      local length = math.sqrt(dx * dx + dy * dy)
      if length > 0 and distance <= 9
        and math.abs((unit.x - me.x) * dy - (unit.y - me.y) * dx) / length <= 3.5 then return true end
    end
    return pack and distance <= 8
  end
end

-- Where she is going, for a dodge to step toward: the next point of the
-- route between levels, or of the search within one.
local function passage_heading(me)
  local heading = trip:dodge_heading(me)
  if heading then return heading end
  local walker = explorer and explorer.mover
  local point = walker and walker.status == 'moving' and walker.path and walker.path[walker.index or 1]
  if point and (point.x ~= math.floor(me.x) or point.y ~= math.floor(me.y)) then
    return { x = point.x + 0.5 - me.x, y = point.y + 0.5 - me.y }
  end
end

-- Mana doses carried, plus (unless carried_only) those the gold on hand buys.
local function supplies(me, carried_only)
  local count = stock.mana()
  if carried_only then return count end
  return count + math.floor(stock.gold(me) / MANA_POTION_GOLD)
end

local function tower_descent_ready(me)
  return flag('tower_prep') and not flag('tower_prep_done')
    and me.area >= 20 and me.area <= COUNTESS_CELLAR
    and (supplies(me, true) >= 1 or ((me.max_mana or 0) > 0 and (me.mana or 0) >= me.max_mana * 0.5))
end

local function early_forward_ready(me)
  if me.class ~= 'sorceress' or me.difficulty ~= 0 or me.level < 3 or me.level > 5
    or me.in_town or not EARLY_FORWARD_AREAS[me.area] or not quest_done(QUEST_DEN) or flag('tower_prep')
    or me.max_life <= 0 or me.life < me.max_life * EARLY_FORWARD_LIFE
    or (me.max_mana or 0) <= 0 or (me.mana or 0) < me.max_mana * EARLY_FORWARD_MANA then return false end
  local carried = stock.carried()
  return stock.mana() >= 1 and stock.healing() >= 1
    and carried.mp + carried.hp + carried.rv >= 2 -- one rejuvenation cannot cover both reserves
end

-- `kept`: the call that keeps the count by her gold. With such a build no
-- other call changes it: a state that set one player on entering, or the
-- hop-by-hop rule for the Tower, undid the count between every two steps.
local function players(m, count, kept)
  if build.players_by_gold and not kept then return end
  if count == 1 then m.data.px_from, m.data.px_to = nil, nil end
  if type(game.players) ~= 'function' then error('PX requires a DLL with game.players and act.players') end
  local current = game.players()
  if not current.supported then
    if not m.data.px_unsupported then print('act1: PX inactive; player-count commands unsupported in this game') end
    m.data.px_unsupported = true
    return
  end
  if type(act.players) ~= 'function' then error('PX requires a DLL with act.players') end
  if current.configured == count then return true end
  if not act.players(count) then error('PX: player-count request failed for P' .. count) end
  local applied = game.players()
  if applied.configured ~= count or not applied.revision or not current.revision or applied.revision == current.revision then
    error('PX: player-count request was not acknowledged for P' .. count)
  end
  print('act1: PX set P' .. count)
  return true
end

local function travel_players(m, me)
  if not trip.target or me.area == trip.target then return end
  local hop = trip.hop
  local destination = hop and hop.from == me.area and hop.to
  if not destination then
    local route = travel.route(me.area, trip.target)
    destination = route and route[1] and route[1].to
  end
  if not destination or (m.data.px_from == me.area and m.data.px_to == destination) then return end
  local xp = me.class == 'sorceress' and me.difficulty == 0
    and ((destination == DEN and me.level >= 2 and not quest_done(QUEST_DEN))
      or (destination >= 20 and destination < COUNTESS_CELLAR and flag('tower_prep') and me.level < TOWER_MAX_LEVEL))
  if xp then combat:observe_missiles(me) end
  local healing = stock.healing()
  local ready = xp and me.max_life > 0 and me.life >= me.max_life * 0.85
    and (me.max_mana or 0) > 0 and (me.mana or 0) >= me.max_mana * 0.75
    and healing >= 2 and supplies(me, true) >= SUPPLY_OK and services.portal_scrolls() > 0
    and #combat:hostiles(me, 12) == 0 and #(combat.missile_lanes or {}) == 0 and act.ready()
  local count = ready and 8 or 1
  local tower = destination >= 20 and destination < COUNTESS_CELLAR
  if tower and ready then
    local static = combat:known_skills()['Static Field']
    -- Supplies alone do not make early Tower packs economical at P8.
    if not static or (static.base_level or 0) <= 0 or (static.level or 0) <= 0 then count = 3 end
  end
  if tower then count = math.min(count, flag('tower_px_cap') or 8) end
  if players(m, count) and tower then tower_px_exposure = math.max(tower_px_exposure, count) end
  m.data.px_from, m.data.px_to = me.area, destination
end

-- Chests, urns and the like are opened for the gold in them, so only while
-- she cannot pay for a full restock: with the gold in hand each one is a
-- stop for nothing (the maintainer's rule).
local function restock_gold()
  local wanted = flag('deep') and DEEP_POTIONS or POTIONS
  return wanted.mp * MANA_POTION_GOLD + wanted.hp * HEALING_POTION_GOLD + SCROLL_GOLD
end
local function broke(me)
  return stock.gold(me) < restock_gold()
end

-- Fight what threatens us, then pick up what is worth it. True when busy.
-- `worth`: this is levelling, not a place where everything must die.
local function reflexes(me, sight, worth, only)
  loot.radius = sight == TRAVEL_SIGHT and 6 or 12
  loot.valuable_radius = sight == TRAVEL_SIGHT and 12 or nil
  if loot:approach(me, function(item) return combat:loot_safe(me, item) end) then return true end
  local travelling = sight == TRAVEL_SIGHT
  -- Not where everything must die, nor where the only way leads through.
  if (travelling or worth) and not trip.mover.through and leave_reach(me) then return true end
  local accept = travelling and travel_filter(me) or worth and worth_filter(me) or nil
  -- `only`: the caller's own limit on what is fought, on top of that.
  if only then
    local usual = accept
    accept = function(unit) return only(unit) and (usual == nil or usual(unit)) end
  end
  -- On the way somewhere with nothing to fight with, keep moving: monsters
  -- are outrun, and leaving the game for mana cost the whole walk again.
  if travelling and not combat.melee_ok and combat:dry(me) then
    if combat.target_id then
      combat.target_id, combat.plan, combat.resting = nil, nil, false
    end
    return false
  end
  if combat:threat(me, sight, accept) then
    -- Not on ground a shooter reaches: on her way she walks on, and where
    -- she is levelling she steps out of it first.
    if travelling and cross_reach(me) then
      if combat.target_id then
        combat.target_id, combat.plan, combat.resting = nil, nil, false
      end
      return false
    end
    if worth and not travelling and leave_reach(me, true) then return true end
    combat:fight(me, sight, accept)
    return true
  end
  -- Releasing a skipped target also releases any old mana-rest/evade state.
  if accept and combat.target_id then combat:fight(me, sight, accept) end
  -- Keep moving past skipped threats instead of stopping under their fire.
  if travelling and #combat:hostiles(me, sight) > 0 then return false end
  -- Opening a nearby container can fund the next potions; distant ones cost travel.
  containers.radius = sight == TRAVEL_SIGHT and 5 or 12
  if not me.in_town and broke(me) and (sight ~= TRAVEL_SIGHT or supplies(me) < SUPPLY_OK) and containers:nearby(me) then
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

local function portal_scroll()
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.code == 'tsc' then return item end
  end
end

-- The portal by the Den is the way back to it, and a character has one
-- portal: a second scroll read on the way to the waypoint closes it (she
-- restocked through one from the Cold Plains, and walked to the Den from
-- town after all). While it stands, scrolls are not for restocking; only
-- the emergency portal, which comes before leaving the game, still is.
local bot
local function marker_stands()
  return bot ~= nil and bot.data.den_marker ~= nil and not bot.data.opening_off and not quest_done(QUEST_DEN)
end
-- And before it stands, the scroll it takes is spoken for: a restock from
-- the Blood Moor is a walk of one level, and even the emergency portal
-- gives way to leaving the game, which there costs ten seconds and keeps
-- the scroll. (Dry at eight players on her way to the entrance, she read it
-- to restock, and the opening was over before the portal was placed.)
local function marker_pending()
  if bot == nil or build.den_marker ~= true or quest_done(QUEST_DEN) or bot.data.den_marker ~= nil
    or bot.data.opening_off or known_waypoints()[COLD_PLAINS] then return false end
  -- Only where the portal is still to be placed: in town and in the Moor.
  local me = game.me()
  return me.in_town or me.area == 2
end
-- Scrolls she may read to restock, and to get out of trouble.
local function spare_scrolls()
  if marker_stands() then return 0 end
  return math.max(0, services.portal_scrolls() - (marker_pending() and 1 or 0))
end
local function emergency_scrolls()
  return math.max(0, services.portal_scrolls() - (marker_pending() and 1 or 0))
end

-- Progression (see `objective`): the act boss is what she is after. It is
-- put off, at her present level, once the way to the boss has ended in
-- PROGRESS.tries chickens: she levels where it pays until the next level and
-- tries again. Nine chickens in a row went to the elite Afflicted by the
-- stairs to Andariel at level 11, a quarter of an hour, without a level
-- gained or a step nearer.
local PROGRESS = { tries = 2 }
local function progressing(me)
  return flag('progress') and (flag('progress_fails.' .. me.level) or 0) < PROGRESS.tries or false
end
local function progress_failed(me)
  if not progressing(me) then return end
  local key = 'progress_fails.' .. me.level
  set_flag(key, (flag(key) or 0) + 1)
  if not progressing(me) then
    print(string.format('act1: %d chickens on the way to the boss at level %d; levelling until the next', PROGRESS.tries, me.level))
  end
end

-- The players setting, kept at all times once the Den is done (builds with
-- players_by_gold): on the way as well as where she levels, since what she
-- kills while travelling pays the same, and a level has spawned its monsters
-- by the time a count set on arrival would apply. Only a boss's level, and
-- the one that leads into it, is entered at one player. More players is
-- more experience from each monster for the same mana per point of it, and
-- more potions drunk, so the count follows her gold (the maintainer's rule): one player,
-- and chests opened, with less than one restock in hand; one more for each
-- further restock she could pay for, up to `most`. Each emergency portal
-- and each chicken out of a stint at more than one player takes one off, at
-- her present level, in every area whose monsters are at least the level of
-- those that caused it: it does nothing for the monsters she left, which
-- keep the life they spawned with, but what spawns next is lighter. (By
-- area alone the next area as hard would be met at full count again; by
-- character level alone the easy crypts would pay for the Tower's packs.)
local PX = { most = 8, quiet = 12 }
local function px_wanted(me)
  local count = math.min(1 + math.floor(stock.gold(me) / restock_gold()), PX.most)
  local info = game.level_info(me.area)
  local down = 0
  for level = 1, info and info.monster_level or 0 do
    down = down + (flag('px_down.' .. me.level .. '.' .. level) or 0)
  end
  return math.max(1, count - down), down
end
-- Called every tick out of town. The count moves only when what it rests on
-- does: her gold, her level, an escape, the monsters of the level she is in.
local function keep_players(m, me)
  if not build.players_by_gold or me.in_town then return end
  -- On the way to a boss nothing is being levelled: one player throughout
  -- (the maintainer's rule), and on her level in any case.
  local boss = progressing(me) or me.area == CATACOMBS_4 or me.area == CATACOMBS_4 - 1
  -- Before the Den is done: eight in the Blood Moor on the way out (see
  -- OPENING.moor_players), one everywhere else, the Den above all. On the
  -- way back in through the portal the Moor is at one again, or the Den's
  -- first rooms would spawn at eight.
  local outbound = build.den_marker and me.area == 2 and not m.data.opening_off
    and not known_waypoints()[COLD_PLAINS]
  local wanted = boss and 1
    or not quest_done(QUEST_DEN) and (outbound and OPENING.moor_players or 1)
    or px_wanted(me)
  -- Changed only between fights: what has spawned keeps the life it has.
  if game.players().configured ~= wanted and #combat:hostiles(me, PX.quiet) == 0 and act.ready() then
    players(m, wanted, true)
  end
end
-- A stint at more than one player that ended in an emergency.
local function px_setback(me, why)
  local count = build.players_by_gold and game.players().configured or 1
  if count <= 1 then return end
  local info = game.level_info(me.area)
  local key = 'px_down.' .. me.level .. '.' .. (info and info.monster_level or 1)
  set_flag(key, (flag(key) or 0) + 1)
  print(string.format('act1: %s at %d players in %s; one fewer among monsters of level %d and up while she is level %d',
    why, count, game.level_name(me.area), info and info.monster_level or 1, me.level))
end

-- Before her first spell the way is cleared, not run: those kills are the
-- level the Den waits for.
local function opening_sight(me)
  return build.den_marker and me.level < OPENING.level and not quest_done(QUEST_DEN)
    and (me.area == 2 or me.area == COLD_PLAINS) and CLEAR_SIGHT or TRAVEL_SIGHT
end

-- What she fights on the way out of town in the opening: see OPENING.tough.
function OPENING.only(m, me)
  if not (build.den_marker and me.area == 2 and not quest_done(QUEST_DEN) and not m.data.opening_off
    and not known_waypoints()[COLD_PLAINS]) then return nil end
  local policy = combat.neural
  local power = policy and policy.cast_power and policy.cast_power(combat:known_skills()) or 0
  if power <= 0 then return nil end
  return function(unit)
    return world.distance(me, unit) <= 5 or value.life(unit) <= OPENING.tough * power
  end
end

-- The Den's elite pack (Corpsefire and his zombies) is the one thing in it
-- she cannot club: it is fought from range (lib/neural_combat), and that
-- takes most of a pool. Found with less, it is left where it stands and the
-- rest of the Den cleared first, with the staff where that will do; the pool
-- fills meanwhile (a full one in two minutes, and at once with a level).
--   radius  ground around where the pack was last seen that is left alone
--   casts   it is hers once pool and potions pay for this many casts,
--   pool    or once the pool is this full, whichever is less,
--   low     and left again when she is down to this many casts with it alive
--   wait_ms with nothing else left she stands off while the pool fills, this
--           long at most (a whole pool is two minutes). Sent in at once with
--           six and eight casts in hand, she kited the pack dry in two runs
--           of five and walked home hurt from both.
local DEN_PACK = { radius = 14, casts = 10, pool = 0.9, low = 2, wait_ms = 120000 }
function DEN_PACK.member(unit) return unit.superunique or unit.unique or unit.champion or unit.minion end

-- Notes the pack where it is seen. True while it is being left for later.
function DEN_PACK.note(m, me)
  local pack = m.data.den_pack
  local count, x, y = 0, 0, 0
  for _, unit in ipairs(combat:hostiles(me, CLEAR_SIGHT)) do
    if unit.x and DEN_PACK.member(unit) then count, x, y = count + 1, x + unit.x, y + unit.y end
  end
  if count > 0 then
    if not pack or pack.gone then
      pack = { radius = DEN_PACK.radius }
      m.data.den_pack = pack
    end
    pack.x, pack.y = x / count, y / count
  elseif pack and not pack.gone and world.distance(me, pack) <= pack.radius then
    pack.gone, pack.held = true, false -- dead, or not where it was: met again, it is noted again
  end
  if not pack or pack.gone or pack.last then return false end
  local cost = combat.attack_mana or 3
  local have = me.mana + stock.mana_points(me)
  local ready = have >= math.min(DEN_PACK.casts * cost, DEN_PACK.pool * me.max_mana)
  if pack.held == nil or (pack.held and ready) or (not pack.held and have < DEN_PACK.low * cost) then
    if pack.held ~= not ready then
      pack.held = not ready
      print(string.format(pack.held and 'den: the elite pack at (%.0f,%.0f) is left for later (mana for %.0f casts)'
        or 'den: the elite pack at (%.0f,%.0f) is hers now (mana for %.0f casts)', pack.x, pack.y, have / cost))
    end
  end
  return pack.held
end

-- While it is left: the ground it stands on, for the search and its routes,
-- and what is fought meanwhile (what is upon her, and nothing of the pack).
function DEN_PACK.only(m, me)
  local pack = m.data.den_pack
  if not (pack and pack.held) then return nil end
  return function(unit)
    if world.distance(me, unit) <= 5 then return true end
    return not DEN_PACK.member(unit) and (unit.x - pack.x) ^ 2 + (unit.y - pack.y) ^ 2 > pack.radius ^ 2
  end
end
function DEN_PACK.explorer(m, reset)
  local function held() return m.data.den_pack and m.data.den_pack.held end
  local search = explore.new(DEN, reset, function() return held() and { m.data.den_pack } or {} end)
  search.accept = function(unit) return not (held() and DEN_PACK.member(unit)) end
  return search
end

-- Wearing what is better (lib/gear says what is; the order is the maintainer's).
--   quiet    nothing hostile this near while she changes: her hands are full
--   step_ms  between the clicks of a change
--   ms       longest a change may take before it is given up for this game
local GEAR = { quiet = 20, step_ms = 300, ms = 8000 }
local CLASS_ID = { amazon = 0, sorceress = 1, necromancer = 2, paladin = 3, barbarian = 4, druid = 5, assassin = 6 }
local outfit
local outfit_seen = {}
-- The change to make now, or nil. Asked again only when what she carries,
-- wears or is has changed: each asking prices every item in every slot.
local function gear_plan(m, me)
  if not build.attacks then return nil end -- builds that have said what their attack is
  outfit = outfit or gear_lib.new(build, {
    power = function(name, id, level) return combat.neural.power_at(name, id, level) end,
    class_id = CLASS_ID[me.class] })
  local key = { me.level, me.strength or 0 }
  for _, item in ipairs(game.items()) do
    if item.location == 'equipped' or (item.location == 'container' and item.page == 0) then
      key[#key + 1] = tostring(item.id) .. (item.location == 'equipped' and 'e' or item.identified == false and 'u' or 'c')
    end
  end
  key = table.concat(key, ',')
  if outfit_seen.key ~= key then
    outfit.potion = stock.mana_dose(me)
    -- What has come off once is not put back on: it was the worse by this same reckoning.
    outfit_seen.key, outfit_seen.plan = key, outfit:plan(me, m.data.gear_off)
  end
  local plan = outfit_seen.plan
  if not plan or (m.data.gear_failed and m.data.gear_failed[plan.item.id]) or not gear_lib.room(plan) then return nil end
  return plan
end
-- Whether an item, wherever it lies, may turn out better than what she
-- wears (gear.candidate: the maintainer's rule by quality). Asked for every
-- item in sight every tick, so each answer is kept a few seconds.
--   gold      Akara's price for a scroll of identify
--   keep_ms   how long an answer about an item stands
--   step_ms, ms  between requests to read, and the longest one item may take
local READ = { gold = 100, keep_ms = 3000, step_ms = 400, ms = 5000 }
local candidates = {}
local function candidate(item)
  if not build.attacks or not item.id then return false end
  local t = now()
  local known = candidates[item.id]
  if known and t - known.at < READ.keep_ms and t >= known.at then return known.is end
  local me = game.me()
  outfit = outfit or gear_lib.new(build, {
    power = function(name, id, level) return combat.neural.power_at(name, id, level) end,
    class_id = CLASS_ID[me.class] })
  local ok, is = pcall(outfit.candidate, outfit, me, item)
  if not ok then print('gear: could not weigh ' .. tostring(item.name) .. ': ' .. tostring(is)) end
  candidates[item.id] = { at = t, is = ok and is or false }
  return candidates[item.id].is
end
loot_lib.candidate = candidate

-- The unidentified finds she carries that are worth reading, as many as she
-- can pay for once her restock is paid: a scroll is the price of two mana
-- potions, and what is not read is sold as it is.
unread = function(me)
  local list = {}
  local afford = math.floor((stock.gold(me) - restock_gold()) / READ.gold)
  for _, item in ipairs(game.items()) do
    if #list >= afford then break end
    if item.location == 'container' and item.page == 0 and item.identified == false and candidate(item)
      and not (bot and bot.data.read_failed and bot.data.read_failed[item.id]) then
      list[#list + 1] = item
    end
  end
  return list
end

-- Gear she carries and will not wear is sold for whatever it fetches: what
-- came off, what was read and found wanting, what she cannot pay to read.
local surplus_seen = {}
loot_lib.surplus = function(item)
  if not build.attacks or not item.id or loot_lib.wear(item) then return false end
  if surplus_seen[item.id] == nil then
    local ok, is = pcall(gear_lib.wearable, item)
    surplus_seen[item.id] = ok and is or false
  end
  return surplus_seen[item.id]
end

loot_lib.wear = function(item)
  if outfit_seen.plan ~= nil and outfit_seen.plan.item.id == item.id then return true end
  -- Nor is what she means to read first.
  if item.identified == false and item.location == 'container' then
    local me = game.me()
    for _, kept in ipairs(unread(me)) do
      if kept.id == item.id then return true end
    end
  end
  return false
end

-- What she carries that sells, by the client's quote.
--   check_ms  how often the pack is priced (each item is a call into the client)
local CASH = { check_ms = 2000 }
local cash = { at = 0, value = 0,
  wearable = function(item) return item.identified ~= false and candidate(item) end }
local function pack_value()
  local t = now()
  if t - cash.at >= CASH.check_ms or t < cash.at then
    cash.at, cash.value = t, 0
    for _, item in ipairs(game.items()) do
      -- Not what she would put on: that is not going to be sold. (A rare
      -- armour counted at its price sent her to town from the Crypt, where
      -- she wore it, sold a javelin, and was as poor as before.)
      if item.location == 'container' and item.page == 0 and loot_lib.sellable(item)
        and not (cash.wearable and cash.wearable(item)) then
        cash.value = cash.value + (loot_lib.sell_value(item) or 0)
      end
    end
  end
  return cash.value
end
-- Mana doses within her means: in hand, within her gold, and within what
-- her pack sells for, once the scroll that comes before any potion is paid
-- for (see SCROLL_GOLD) if she has none.
local function means(me)
  local scroll = quest_done(QUEST_DEN) and services.portal_scrolls() == 0 and SCROLL_GOLD or 0
  return stock.mana() + math.max(0, math.floor((stock.gold(me) + pack_value() - scroll) / MANA_POTION_GOLD))
end

-- The next step of the opening, or nothing when it does not apply (no scroll
-- left, the waypoint already known, a later game): then the Den is simply next.
local function opening(m, me)
  if not build.den_marker or quest_done(QUEST_DEN) or m.data.opening_off then return end
  local known = known_waypoints()[COLD_PLAINS]
  if not m.data.den_marker then
    if known or not portal_scroll() then return end
    if me.in_town then
      -- Set before she leaves: the rooms by the camp spawn as she nears the gate.
      players(m, OPENING.moor_players, true)
      trip:to(2)
      return 'travel', 'Blood Moor, to leave a portal by the Den'
    end
    if me.area == 2 then return 'den_marker', 'a town portal by the Den as the way back' end
    return
  end
  if not known then
    if m.data.no_wp == COLD_PLAINS or flag('no_wp.' .. COLD_PLAINS) then
      m.data.opening_off = true
      return
    end
    if me.area == COLD_PLAINS then
      m.data.wp_spawn, m.data.wp_resume, m.data.wp_budget = waypoint_spawn(COLD_PLAINS), 'decide', OPENING.waypoint_ms
      return 'activate_waypoint', 'the Cold Plains waypoint, the way on after the Den'
    end
    if me.in_town then
      -- Home to restock in the middle of it: back out, the portal still stands.
      trip:to(2)
      return 'travel', 'Blood Moor, where the opening goes on'
    end
    if me.area ~= 2 then return end
    -- The Blood Moor first, until she can pay for her potions (the
    -- maintainer's route): its monsters are hers at level 1, and with
    -- nothing to her name after the Den she was sent back here at level 3.
    if build.moor_first and not m.data.moor_done and means(me) < SUPPLY_OK then
      return 'opening_moor', string.format('the Blood Moor, until she can pay for %d mana potions', SUPPLY_OK)
    end
    trip:to(COLD_PLAINS)
    return 'travel', 'Cold Plains'
  end
  if me.in_town then return 'den_return', 'through the portal to the Den' end
  if me.area ~= COLD_PLAINS then return end
  if OPENING.level_first and me.level < OPENING.level and not m.data.opening_leveled then
    return 'opening_level', string.format('level %d before the Den', OPENING.level)
  end
  m.data.waypoint_to = TOWN
  return 'take_waypoint', 'town, and the portal to the Den'
end

local function has_vendor_loot()
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and loot_lib.sellable(item) then return true end
  end
  return false
end

local function recovery_anchor(me)
  return me.area == COLD_PLAINS and me.class == 'sorceress' and me.difficulty == 0 and quest_done(QUEST_DEN)
end

local function waypoint_budget(me)
  return WAYPOINT_ANCHORS[me.area] and ANCHOR_WAYPOINT_MS
    or recovery_anchor(me) and RECOVERY_WAYPOINT_MS or OPTIONAL_WAYPOINT_MS
end

local function select_waypoint(m, me, resume)
  local info = game.level_info(me.area)
  if not info or not info.waypoint or me.in_town or known_waypoints()[me.area] or m.data.no_wp == me.area
    or flag('no_wp.' .. me.area) then return false end
  local recovery = recovery_anchor(me)
  if recovery and has_vendor_loot() then return false end -- cash in useful recovery drops before detouring
  local anchor = WAYPOINT_ANCHORS[me.area] or recovery
  local nearby = not anchor and world.nearest(game.units('object'), me, function(o)
    return o.waypoint and world.distance(me, o) <= WAYPOINT_NEAR and map.line_of_sight(me.x, me.y, o.x, o.y)
  end)
  if not anchor and not nearby then return false end
  m.data.wp_spawn, m.data.wp_resume = nearby or nil, resume
  return true
end

local function finish_waypoint(m, reason)
  local resume = m.data.wp_resume or 'travel'
  m.data.wp_resume = nil
  return resume, reason
end

local function boss_restock(m, me)
  if stock.healing() >= BOSS_HEALING then
    m.data.boss_supply_attempted = nil
    return
  end
  if m.data.boss_supply_attempted == me.area then return end
  local budget = math.max(0, SUPPLY_LOW - supplies(me, true)) * MANA_POTION_GOLD
    + 2 * HEALING_POTION_GOLD -- services buys its minimum mana reserve before healing
  -- Use a quiet staging position while loot can still fund a healing reserve.
  if services.portal_scrolls() > 0 and #combat:hostiles(me, 7) == 0
    and (me.gold + (me.stash_gold or 0) >= budget or has_vendor_loot()) then
    m.data.boss_supply_attempted = me.area
    return 'portal_heal', 'restock healing before the boss approach'
  end
end

local function travel_restock(m, me)
  local healing, mana = stock.healing(), stock.carried().mp
  if healing > 1 then m.data.travel_supply_attempted = nil; return end
  if me.in_town or not quest_done(QUEST_DEN)
    or (m.data.travel_supply_attempted and m.data.travel_supply_attempted[me.area]) then return end
  local budget = math.max(0, SUPPLY_LOW - mana) * MANA_POTION_GOLD
    + 2 * HEALING_POTION_GOLD
  if services.portal_scrolls() > 0 and #combat:hostiles(me, 12) == 0
    and #(combat.missile_lanes or {}) == 0
    and (me.gold + (me.stash_gold or 0) >= budget or has_vendor_loot()) and act.ready() then
    m.data.travel_supply_attempted = m.data.travel_supply_attempted or {}
    m.data.travel_supply_attempted[me.area] = true
    return 'portal_heal', 'restock healing on a quiet stretch of travel'
  end
end

local function recovery_target(me, area)
  -- Early forward packs can fund the next refill; do not sweep the Moor for
  -- six doses when an otherwise ready sorceress can carry only two.
  if me.class == 'sorceress' and me.difficulty == 0 and me.level >= 3 and me.level <= 5
    and quest_done(QUEST_DEN) and not flag('tower_prep') and supplies(me) < SUPPLY_OK then
    return SUPPLY_LOW
  end
  -- Tower packs can replenish two reserves; buy six when the cash is available.
  area = area or me.area
  return flag('tower_prep') and ((area >= 20 and area <= COUNTESS_CELLAR)
    or supplies(me) < SUPPLY_OK) and SUPPLY_LOW or SUPPLY_OK
end

local function recovery_restock(m, me)
  local target = recovery_target(me)
  if not flag('gold_farm') or supplies(me, true) >= target or supplies(me) < target then return end
  m.data.farm_commit = nil
  if me.in_town then
    if m.data.town_done then return 'save_exit', 'mana purchase failed despite funds; reset town services' end
    return 'town', 'buy the recovery mana reserve'
  end
  if flag('tower_prep') and me.area >= 20 and me.area <= COUNTESS_CELLAR
    and (services.portal_scrolls() > 0 or world.nearest(game.units('object'), me,
      function(o) return o.portal and (o.owner_name == me.name or (o.class_id == 59 and o.owner_name == '')) end)) then
    return 'portal_heal', 'restock without losing the Tower descent'
  end
  local route = travel.route(me.area, TOWN)
  if route and #route > 1 then return 'save_exit', 'recovery reserve affordable; town is far' end
  return 'retreat', 'recovery reserve affordable; restock before moving on'
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

-- Every area of the act, each with the known waypoint that brings it nearest
-- and the hops on from there. Where to level is the optimizer's choice among
-- all of them (lib/leveling: experience per point of damage, spawn density,
-- what was measured, the way there), not only the areas of the main route:
-- the Tower's cellars pay best from level 5 to 12 by its reckoning, and the
-- crypts of the Burial Grounds before that.
--
-- The way to an area is reckoned from the nearest waypoint of the act,
-- whether she has it yet or not (the maintainer's rule). One she lacks is
-- fetched on foot when the choice falls on its area: that walk is made once
-- and pays for all the levelling beyond it. Reckoned from the waypoints she
-- had, she stayed in the two crypts beside the Cold Plains at level 8,
-- because the Tower was eight levels' walk away.
local function all_farms()
  local from = {}
  for level = WORTH.first, WORTH.last do
    local info = game.level_info(level)
    if info and info.waypoint then from[level] = leveling.hops_from(level) end
  end
  local candidates, via, onward = {}, {}, {}
  for level = WORTH.first, WORTH.last do
    local info = game.level_info(level)
    if not NOT_FARMED[level] and level ~= DEN and info and (info.monster_level or 0) > 0 then
      candidates[#candidates + 1] = level
      for wp, hops in pairs(from) do
        if hops[level] and (not onward[level] or hops[level] < onward[level]
          or (hops[level] == onward[level] and wp < via[level])) then
          via[level], onward[level] = wp, hops[level]
        end
      end
    end
  end
  return candidates, via, onward
end

-- With build.follow_pay the areas are not gated by level at all (the
-- maintainer's experiment): after the Den she is always where the optimizer
-- says a kill pays best, and what moves her on through the act is that an
-- area's pay falls off as she out-levels it. Once its best areas are cleared
-- a game is left for a new one when that pays more even so: everything is
-- back, she starts from town, and leaving and loading are charged as
-- NEW_GAME_HOPS more of travel (the maintainer's rule, in place of a flat
-- share of the fresh game's pay).
local NEW_GAME_HOPS = 1
-- What she is doing, for builds with follow_pay: one answer, worked out in
-- one place from the character (level, what her attack does, what is cleared
-- in this game, where the waypoints are), and held until its area is cleared
-- or she gains a level. The planner asks here and nowhere else, and nothing
-- below it (how she gets there, whom she fights on the way) changes the
-- answer. Rules that each decided this for themselves, from where she stood
-- at the time, sent her back and forth between two waypoints four times in
-- a day.
--   area     where a kill pays best (lib/leveling)
--   via      the waypoint it is reached from, hers or not
--   refresh  set when a new game would pay more, its cost counted
--   progress set once an area of the next act pays more than anything a
--            fresh game of this one offers: then the act boss is what stands
--            between her and it, and the boss's level is the objective until
--            the boss is dead (the maintainer's rule; it is not gone back on)
-- The next act's areas are reckoned as NEXT_ACT.hops from a waypoint: its
-- layout cannot be read from this one.
local NEXT_ACT = { first = 41, last = 74, hops = 2 }
-- Her means are part of the answer. Without a mana potion in hand or within
-- her gold she casts a dozen times and is left with her staff, which kills
-- where monsters are weak and nowhere else: sent to the Tower like that she
-- arrived dry, left the game for it, and did the same in the next one. So
-- while she is poor the choice is among areas whose monsters are at least
-- MEANS.below levels under her (MEANS.floor at the least; the maintainer's
-- figure): weak enough for the staff and a thin pool, near enough to her
-- level to go on paying experience. There chests are opened and what drops
-- is hers, until she can pay for MEANS.enough doses; poverty begins under
-- MEANS.short. The two figures are apart so that one potion bought does not
-- send her back to the Tower.
-- `below` and `floor` are the maintainer's (2026-11-12): five levels under
-- her, and never under "whatever lets her fight in cold plains, crypts,
-- and mausoleum", which is monster level 3 (the Cold Plains are 2, the
-- Burial Grounds' crypts 3). The floor was 1 at first, and a level-4
-- character was sent back to the Blood Moor after the Den and searched it
-- for 770 seconds, for coins.
local MEANS = { below = 5, floor = 3, short = SUPPLY_LOW, enough = SUPPLY_OK }
local function poor_cap(me) return math.max(MEANS.floor, me.level - MEANS.below) end
local function objective(m, me)
  m.data.spent = m.data.spent or {}
  local goal = m.data.objective
  local doses = supplies(me)
  if doses < MEANS.short then m.data.poor = true elseif doses >= MEANS.enough then m.data.poor = nil end
  local poor = m.data.poor or false
  if progressing(me) then
    if not goal or goal.area ~= CATACOMBS_4 then
      local _, via = all_farms()
      goal = { area = CATACOMBS_4, via = via[CATACOMBS_4], level = me.level, progress = true,
        why = game.level_name(CATACOMBS_4) .. ': Andariel stands before the next act' }
      m.data.objective = goal
      print('objective: ' .. goal.why)
    end
    return goal
  end
  if goal and goal.level == me.level and goal.poor == poor and not m.data.spent[goal.area]
    and flag('farm_left') ~= goal.area .. ':' .. me.level then
    return goal
  end
  local every, via, onward = all_farms()
  local afoot = leveling.hops_from(me.area)
  local candidates, reach = {}, {}
  for _, level in ipairs(every) do
    local info = poor and game.level_info(level)
    if flag('farm_left') ~= level .. ':' .. me.level
      and (not poor or (info and info.monster_level <= poor_cap(me))) then
      candidates[#candidates + 1] = level
      -- On foot from here, or one jump and the walk on from its waypoint.
      reach[level] = math.min(me.in_town and math.huge or afoot[level] or math.huge,
        via[level] and 1 + onward[level] or math.huge)
      if reach[level] == math.huge then reach[level] = nil end
    end
  end
  local policy = combat.neural
  local power = policy and policy.cast_power and policy.cast_power(combat:known_skills()) or 0
  local options = { candidates = candidates, spent = m.data.spent, hops = reach, cast = power > 0 and power or nil }
  local area, why, ranking, score = leveling.choose(me, build, options)
  if not area then
    m.data.objective = nil
    return nil
  end
  local _, _, _, fresh = leveling.choose(me, build, { candidates = candidates, hops = reach, cast = options.cast })
  -- What a new game would offer: nothing cleared, from town, one hop dearer.
  local anew = {}
  for _, level in ipairs(candidates) do
    if via[level] then anew[level] = 1 + onward[level] + NEW_GAME_HOPS end
  end
  local _, _, _, renewed = leveling.choose(me, build, { candidates = candidates, hops = anew, cast = options.cast })
  -- Does the next act pay more by now?
  local beyond, far = {}, {}
  for level = NEXT_ACT.first, NEXT_ACT.last do
    local info = game.level_info(level)
    if info and (info.monster_level or 0) > 0 then
      beyond[#beyond + 1] = level
      far[level] = NEXT_ACT.hops
    end
  end
  local next_area, _, _, next_score = leveling.choose(me, build, { candidates = beyond, hops = far, cast = options.cast })
  if not flag('progress') and next_area and fresh and next_score > fresh then
    print(string.format('objective: %s in the next act pays %.1f against %.1f for the best of this one; on to Andariel',
      game.level_name(next_area), next_score, fresh))
    set_flag('progress', true)
    m.data.objective = nil
    return objective(m, me)
  end
  goal = { area = area, via = via[area], level = me.level, poor = poor, why = why, ranking = ranking,
    refresh = renewed and score < renewed
      and string.format('what is left pays %.1f against %.1f in a new game', score, renewed) or nil }
  m.data.objective = goal
  print('objective: ' .. (poor and string.format('poor (%d mana doses in hand or within her gold), so among monsters of level %d at most: ',
      doses, poor_cap(me)) or '')
    .. why .. (ranking ~= '' and ' (scores: ' .. ranking .. ')' or ''))
  return goal
end

-- Poor, with the way out of it in her pack: what she carries that sells
-- counts as hers once it would pay for MEANS.enough doses, and then selling
-- it is what she does next. Counting gold alone, she searched the Blood
-- Moor for ten minutes at level 4 with 51 gold and two thousand in staves
-- and wands on her back. Returns the state that takes her to town, and why.
local function cash_in(m, me)
  if not m.data.poor then return nil end
  pack_value()
  if me.in_town then
    -- What a visit left unsold does not send her back for another.
    if m.data.town_done then m.data.cash_left = cash.value end
    return nil
  end
  if cash.value <= (m.data.cash_left or 0)
    or means(me) < MEANS.enough then return nil end
  m.data.cash_left = cash.value
  local why = string.format('poor with %d gold, carrying what sells for %d', stock.gold(me), cash.value)
  if spare_scrolls() > 0 then
    m.data.portal_town_only, m.data.portal_passage, m.data.portal_sell = true, true, true
    return 'portal_heal', why .. '; a portal to town to sell it'
  end
  local route = travel.route(me.area, TOWN)
  if route and #route <= 1 then return 'retreat', why .. '; to town to sell it' end
  return 'save_exit', why .. '; town is far, and a new game starts beside the shop'
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

local strategy_logged
local function optional_tower_choice(m, me)
  local onward = travel.route(6, CATACOMBS_4)
  local next_area = onward and onward[1] and onward[1].to
  if not next_area then return end -- existing main-route validation owns missing routes
  local known = known_waypoints()
  local inside = me.area >= 20 and me.area <= COUNTESS_CELLAR
  local reset = m.data.countess_done == true or m.data.countess_failed == true
  local tower_from = reset and (known[6] and 6 or TOWN) or me.area
  local tower_route = travel.route(tower_from, COUNTESS_CELLAR)
  local progress_route = travel.route(me.area, next_area)
  local progress_hops
  if known[6] and (inside or me.in_town) then progress_hops = 2
  elseif progress_route then progress_hops = #progress_route
  else return end
  local failures = flag('tower_failures') or 0
  local unavailable = me.level >= TOWER_MAX_LEVEL or failures >= TOWER_FAILURE_LIMIT or not tower_route
  local known_skills = combat:known_skills()
  local function learned(name)
    local skill = known_skills[name]
    return skill and (skill.base_level or 0) > 0 and (skill.level or 0) > 0 and skill.level or 0
  end
  local begun = flag('tower_started')
  local context = {level=me.level, bolt_rank=learned('Charged Bolt'), nova=learned('Frost Nova')>0,
    static=learned('Static Field')>0, first_clear=not flag('tower_cleared') and not quest_done(QUEST_TOWER),
    committed=inside and not m.data.countess_done and not m.data.countess_failed and not unavailable,
    tower_unavailable=not not unavailable, reset=reset, tower_hops=tower_route and #tower_route or 0,
    progress_hops=progress_hops, tower_value=leveling.prior(COUNTESS_CELLAR, build, me.level) or 0,
    progress_value=leveling.prior(next_area, build, me.level) or 0, failures=failures,
    tower_ms=begun and math.max(0, now()-begun) or 0}
  local action, scores = planner:choose(context)
  local signature = table.concat({action,me.level,context.bolt_rank,tostring(context.nova),tostring(context.static),
    tostring(context.first_clear),tostring(context.committed),tostring(reset),failures,context.tower_hops,progress_hops}, ':')
  if signature ~= strategy_logged then
    local function score(n) return n and string.format('%.2f', n) or 'illegal' end
    print(string.format('strategy %s: %s progress=%s tower=%s level=%d CB=%d nova=%s static=%s first=%s committed=%s time=%.1fs hops=%d/%d reset=%s failures=%d',
      planner.version, action, score(scores[1]), score(scores[2]), me.level, context.bolt_rank,
      tostring(context.nova), tostring(context.static), tostring(context.first_clear), tostring(context.committed),
      context.tower_ms/1000, context.tower_hops, progress_hops, tostring(reset), failures))
    strategy_logged = signature
  end
  if action == 'tower' then
    set_flag('tower_prep', true)
    if not begun then set_flag('tower_started', now()) end
  else
    set_flag('tower_prep_done', true)
    set_flag('tower_prep', nil)
  end
  return action
end

-- Where to go next, or nil and why when the character must level first.
local level_gate -- the character level the next area asks for, while we level up for it

local function next_goal(me)
  if not quest_done(QUEST_DEN) then
    -- The sorceress needs her first skill, not a cleared Blood Moor.
    if me.level >= (build.den_level or 3) then return DEN end
    return nil, 'level ' .. me.level .. ' is low for the Den of Evil'
  end
  -- The next area on the way, from where the plan goes on: the activated
  -- waypoint nearest the goal when that is ahead of us (or we are in town).
  -- Asked from where she stood, a sorceress sent back to level up found the
  -- next hop easy, jumped ahead to the gated waypoint, and was sent back
  -- again, every two seconds.
  if build.follow_pay then
    level_gate = nil
    return nil, 'where a kill pays best'
  end
  local here = travel.route(me.area, CATACOMBS_4)
  local waypoint, hops_left = best_waypoint(CATACOMBS_4)
  local from = waypoint and (me.in_town or not here or hops_left < #here) and waypoint or me.area
  local route = from == me.area and here or travel.route(from, CATACOMBS_4)
  local ahead = route and route[1] and route[1].to
  local info = ahead and game.level_info(ahead)
  local gap = build.enter_gap -- builds without one walk on regardless
  if gap and info and (info.monster_level or 0) > me.level + gap then
    level_gate = info.monster_level - gap
    return nil, string.format('level %d is low for %s (monster level %d)', me.level,
      game.level_name(ahead), info.monster_level)
  end
  level_gate = nil
  return CATACOMBS_4
end

-- Whether leveling may stop. The gate is fixed where the plan met it: asking
-- again from the farm area would look at a different, easier next hop.
local function level_ready(me)
  if not quest_done(QUEST_DEN) then return next_goal(me) end
  if build.follow_pay then return false end -- never done: the next area is chosen when this one is cleared
  return not level_gate or me.level >= level_gate
end

-- Walk into range before queuing an interaction; the native approach can stall at obstacles.
local function use_waypoint_object(m, me)
  local wp = world.nearest(game.units('object'), me, function(o) return o.waypoint end)
  if wp and world.distance(me, wp) <= 5 and map.line_of_sight(me.x, me.y, wp.x, wp.y) then
    if act.ready() and now() - (m.data.used or 0) > 1500 then
      act.interact(wp)
      m.data.used = now()
      m.data.wp_clicks = (m.data.wp_clicks or 0) + 1
      if m.data.wp_clicks % 4 == 0 then
        print(string.format('waypoint: click %d unanswered, waypoint (%.1f,%.1f) mode %s, player (%.1f,%.1f) mode %s, distance %.1f',
          m.data.wp_clicks, wp.x, wp.y, tostring(wp.mode), me.x, me.y, tostring(me.mode), world.distance(me, wp)))
      end
    end
    return true
  end
  return false
end

-- Counts emergency portals from one place; returns the count once it is one
-- too many (see EMERGENCY_LIMIT).
local function emergency_again(m, me)
  local spot = m.data.emergency
  if spot and spot.area == me.area and world.distance(me, spot) <= EMERGENCY_REACH then
    spot.count = spot.count + 1
  else
    spot = { area = me.area, x = me.x, y = me.y, count = 1 }
    m.data.emergency = spot
  end
  return spot.count >= EMERGENCY_LIMIT and spot.count or nil
end

bot = fsm.new('warm', {
  -- Generate the act's level layouts one per tick (breadth-first from here)
  -- so route and leveling plans never spend a whole frame generating them.
  warm = {
    enter = function(m)
      tower_px_exposure = 1
      players(m, 1)
      m.data.warm_queue, m.data.warm_seen = { game.me().area }, { [game.me().area] = true }
    end,
    tick = function(m)
      local me = game.me()
      -- A hot reload can start here in the middle of a fight.
      if not me.in_town and combat:threat(me, 10) then combat:fight(me, 10) end
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
      -- which opening the quest panel requests: sync once before trusting it.
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
      -- (Not for the sorceress: a Rogue hired with the monastery's gold died
      -- in every fight in the Catacombs, and each hire is a new game.)
      if me.class ~= 'sorceress' and me.in_town and not m.data.hire_tried and (not merc or merc.dead) and me.level >= 8
        and me.gold + (me.stash_gold or 0) >= HIRE_MIN_GOLD then
        return 'hire'
      end
      -- Once per session: the list read then is kept in the store, and every
      -- waypoint activated since is recorded as it happens.
      if me.in_town and not m.data.waypoints_synced and not flag('waypoints_read') then
        local goal = next_goal(me) or CATACOMBS_4
        local route = travel.route(me.area, goal)
        if route and #route > 1 then return 'sync_waypoints' end
      end
      if quest_done(QUEST_ANDARIEL) then return 'done', 'Andariel quest credited' end
      -- One visit per town stay, so a failed sale cannot loop.
      if not me.in_town then m.data.town_done = nil end
      -- An item left on the cursor (a change cut short, a game left in the
      -- middle of one) is put away before anything else: with her hands
      -- full she can do nothing.
      if me.cursor_item and not m.data.stow_failed then
        m.data.gear_change = nil
        return 'gear', 'an item left on the cursor'
      end
      -- Before anything is sold: what is better than what she wears goes on.
      local wear = gear_plan(m, me)
      if wear and not me.cursor_item and (me.in_town or #combat:hostiles(me, GEAR.quiet) == 0) then
        m.data.gear_change = wear
        local off = {}
        for _, old in ipairs(wear.removes or {}) do off[#off + 1] = tostring(old.name) end
        return 'gear', string.format('%s (quality %s) into slot %d, %s %.1f -> %.1f%s', tostring(wear.item.name),
          tostring(wear.item.quality), wear.slot, tostring(wear.gain), wear.was or 0, wear.becomes or 0,
          #off > 0 and ', taking off ' .. table.concat(off, ' and ') or '')
      end
      if me.in_town and not m.data.town_done and visit:needs(me) then return 'town', 'heal and restock' end
      -- With the scrolls bought: what she found is read, and weighed on the next asking.
      if me.in_town and not me.cursor_item and services.scrolls('isc') > 0 and #unread(me) > 0 then
        return 'identify', string.format('%d to read', #unread(me))
      end
      for _, unit in ipairs(game.units('player')) do
        -- Our corpse in sight (for example brought to town by save and exit).
        if unit.name == me.name and unit.id ~= me.id then set_flag('corpse_area', me.area) end
      end
      if flag('corpse_area') then return 'corpse' end
      -- The Den's reward is Akara's to give, and a new game starts beside her.
      local den = game.quest(QUEST_DEN)
      if me.in_town and den and den.reward_pending and not den.completed and not m.data.den_reward_tried then
        return 'den_reward', 'Akara, for the Den'
      end
      local step, step_why = opening(m, me)
      if step then return step, step_why end
      if build.follow_pay and quest_done(QUEST_DEN) then
        -- A boss is always fought once she stands on its level.
        if me.area == CATACOMBS_4 then return 'andariel' end
        local goal = objective(m, me)
        local sell, sell_why = cash_in(m, me)
        if sell then return sell, sell_why end
        if not goal then return 'save_exit', 'nothing left in this game that pays' end
        if goal.refresh then
          m.data.objective = nil
          return 'save_exit', goal.refresh
        end
        return 'farm', game.level_name(goal.area)
      end
      local optional_tower = build.tower_prep and me.class == 'sorceress' and me.difficulty == 0 and quest_done(QUEST_DEN)
      -- One descent only: the Countess's first drop funds the potions; repeats did not pay.
      if optional_tower and build.tower_prep == 'once' and (flag('tower_cleared') or quest_done(QUEST_TOWER))
        and not flag('tower_prep_done') then
        set_flag('tower_prep_done', true)
        set_flag('tower_prep', nil)
      end
      local tower_prep = false
      if optional_tower and me.level >= TOWER_PREP_START and not flag('tower_prep_done')
        and (flag('tower_prep') or me.area == 6 or known_waypoints()[6] or (me.area >= 20 and me.area <= COUNTESS_CELLAR)) then
        local choice = optional_tower_choice(m, me)
        tower_prep = choice == 'tower'
        if choice == 'progress' and me.area >= 20 and me.area <= COUNTESS_CELLAR then
          return 'save_exit', 'Tower strategy chose the main route; resume from town via waypoint'
        end
      end
      if tower_prep and m.data.countess_done then
        return 'save_exit', 'Tower strategy chose another descent; refresh the cleared Countess'
      end
      if m.data.countess_failed and (not optional_tower or tower_prep) then
        return 'save_exit', 'Countess search exhausted; abandon this attempt'
      end
      if me.area == CATACOMBS_4 then return 'andariel' end
      if me.area == DEN and not quest_done(QUEST_DEN) then return 'clear', 'Den of Evil' end
      local supply = supplies(me)
      local carried = supplies(me, true)
      local tower_descent = tower_prep and tower_descent_ready(me)
      if early_forward_ready(me) then
        set_flag('gold_farm', nil)
        m.data.farm_commit = nil
      elseif tower_descent then
        -- Packs can replenish a committed descent; recover when mana is actually low.
        set_flag('gold_farm', nil)
      else
        -- A resource sweep that yielded nothing is not repeated: a dry
        -- sorceress then refills at the healer by portal or a new game and
        -- keeps to the plan. Repeating it looped without funding a reserve.
        if carried >= SUPPLY_LOW then set_flag('farm_dry', nil) end
        local pays = not flag('farm_dry')
        if flag('gold_farm') and (carried >= recovery_target(me) or not pays) then set_flag('gold_farm', nil) end
        pays = pays and build.gold_farm ~= false
        if pays and not flag('gold_farm') and supply < SUPPLY_LOW and quest_done(QUEST_DEN) then set_flag('gold_farm', true) end
        if pays and tower_prep and carried < SUPPLY_LOW then set_flag('gold_farm', true) end
        local restock, restock_why = recovery_restock(m, me)
        if restock then return restock, restock_why end
      end
      -- Preparation starts at Black Marsh, never as an early detour; other
      -- builds retain their optional, level-appropriate resource run.
      local cellar = game.level_info(COUNTESS_CELLAR)
      local countess_ok = cellar and cellar.monster_level <= me.level + ENTER_ABOVE
        and travel.route(me.area, COUNTESS_CELLAR) ~= nil
      local countess = not m.data.countess_done and (tower_descent or carried >= SUPPLY_LOW)
        and ((tower_prep and not flag('gold_farm') and travel.route(me.area, COUNTESS_CELLAR) ~= nil)
          or (countess_ok and me.class ~= 'sorceress' and flag('gold_farm')))
      if countess and me.area == COUNTESS_CELLAR then return 'countess' end
      if not countess and flag('gold_farm') and me.area ~= DEN then
        if m.data.countess_done and me.class ~= 'sorceress' then return 'save_exit', 'Countess run done; a new game brings her back' end
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
        -- A stop to level that is under way is seen through. Asked again from
        -- a waypoint on the way to the chosen area, the plan found the next
        -- hop easy, walked her back to the gate, and sent her off to level
        -- again: three times round at the Monastery Gate.
        local stop = m.data.farm_commit
        if stop and stop.mode == 'xp' and stop.gate and me.level < stop.gate
          and not (m.data.spent and m.data.spent[stop.area]) then
          return 'farm', 'levelling in ' .. game.level_name(stop.area)
        end
        goal, why = next_goal(me)
        if not goal then return 'farm', why end
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
    enter = function(m)
      m.data.quests_synced = nil
      m.data.quest_sync = { phase = 'prepare', attempts = 0 }
    end,
    tick = function(m)
      local sync, t = m.data.quest_sync, now()
      local open = game.ui('quests')
      if sync.phase == 'failed' then return 'save_exit', 'quest refresh timed out' end
      if m:elapsed() > QUEST_SYNC_MS then
        if open then
          act.quest_log(false)
          sync.phase = 'failed'
          return
        end
        return 'save_exit', 'quest refresh timed out'
      end
      if sync.phase == 'prepare' then
        if open then
          if not sync.action_at or t - sync.action_at >= QUEST_RETRY_MS then
            act.quest_log(false)
            sync.action_at, sync.closing = t, true
          end
          return
        end
        if sync.closing then sync.closed_at, sync.closing = t, nil end
        if sync.closed_at and t - sync.closed_at < QUEST_REOPEN_MS then return end
        sync.revision = game.quest_revision()
        act.quest_log(true)
        sync.phase, sync.action_at, sync.attempts = 'waiting', t, sync.attempts + 1
        return
      end
      if sync.phase == 'waiting' then
        if game.quest_revision() ~= sync.revision then
          sync.phase = 'finish'
        elseif t - sync.action_at >= QUEST_RETRY_MS and sync.attempts < 3 then
          sync.phase = 'prepare'
          return
        else
          return
        end
      end
      if open then
        if t - sync.action_at >= QUEST_RETRY_MS or not sync.close_sent then
          act.quest_log(false)
          sync.action_at, sync.close_sent = t, true
        end
        return
      end
      if sync.phase == 'finish' then
        m.data.quest_sync = nil
        m.data.quests_synced = true
        return 'decide', 'quest record refreshed'
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
        for level in pairs(game.waypoints() or {}) do
          set_flag('wp.' .. level, true)
          count = count + 1
        end
        set_flag('waypoints_read', true)
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
        visit = new_visit()
        m.data.town_done = true
        return 'decide', 'town visit done'
      end
    end,
  },

  travel = {
    tick = function(m)
      local me = game.me()
      travel_players(m, me)
      replan_around(m, me, 'travel')
      if reflexes(me, opening_sight(me), true, OPENING.only(m, me)) then return end
      if select_waypoint(m, me, 'travel') then return 'activate_waypoint' end
      local restock, why = travel_restock(m, me)
      if restock then return restock, why end
      local status = trip:tick()
      if status == 'arrived' then
        m.data.failures = 0
        if flag('travel_reset_target') == me.area then
          set_flag('travel_resets', nil)
          set_flag('travel_reset_target', nil)
        end
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
          -- Reset transient obstructions, but earlier successful hops must not
          -- erase repeated failures at the same destination after rejoining.
          if flag('travel_reset_target') ~= trip.target then set_flag('travel_resets', nil) end
          set_flag('travel_reset_target', trip.target)
          set_flag('travel_resets', (flag('travel_resets') or 0) + 1)
          if flag('travel_resets') > 3 then return 'failed', trip.reason end
          return 'save_exit', 'cannot get through (' .. tostring(trip.reason) .. '); trying a new game'
        end
        map.invalidate()
        trip:to(trip.target)
      end
    end,
  },

  -- Detour for restart anchors; other waypoints are touched only on the route.
  activate_waypoint = {
    enter = function(m)
      players(m, 1)
      m.data.wp_since = now()
      local me = game.me()
      m.data.wp_timeout, m.data.wp_budget = m.data.wp_budget or waypoint_budget(me), nil
      if WAYPOINT_ANCHORS[me.area] or recovery_anchor(me) then m.data.wp_spawn = waypoint_spawn(me.area) end
      m.data.wp_mover, m.data.wp_try = move.new(), 0
    end,
    tick = function(m)
      local me = game.me()
      if game.ui('waypoint') then
        local history = game.waypoints()
        if history and history[me.area] then set_flag('wp.' .. me.area, true) end
        act.key(27)
        return finish_waypoint(m, 'waypoint activated in ' .. game.level_name(me.area))
      end
      local spawn = m.data.wp_spawn
      local timeout = m.data.wp_timeout or OPTIONAL_WAYPOINT_MS
      if not spawn or now() - m.data.wp_since > timeout then
        m.data.no_wp = me.area
        return finish_waypoint(m, 'waypoint not reached in ' .. timeout / 1000 .. ' s')
      end
      if reflexes(me, opening_sight(me), true, OPENING.only(m, me)) then return end
      if use_waypoint_object(m, me) then return end
      -- Standing where the map places it without the game showing one: the
      -- Catacombs Level 2 waypoint never appeared at its preset, and circling
      -- the spot cost the whole budget every time. Remember it for later games.
      if world.distance(me, spawn) <= WAYPOINT_ABSENT_REACH and not world.nearest(game.units('object'), me,
        function(o) return o.waypoint end) then
        m.data.wp_absent_since = m.data.wp_absent_since or now()
        if now() - m.data.wp_absent_since > WAYPOINT_ABSENT_MS then
          m.data.no_wp = me.area
          set_flag('no_wp.' .. me.area, true)
          return finish_waypoint(m, string.format('no waypoint at its map position (%d,%d)', spawn.x, spawn.y))
        end
      else
        m.data.wp_absent_since = nil
      end
      local mover_ = m.data.wp_mover
      if mover_.status ~= 'moving' then
        if mover_.status == 'failed' and not m.data.wp_logged then
          m.data.wp_logged = true
          print(string.format('activate_waypoint: no way to the waypoint at (%d,%d) from (%.0f,%.0f): %s',
            spawn.x, spawn.y, me.x, me.y, tostring(mover_.reason)))
        end
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
    enter = function(m) players(m, 1); m.data.wp_since = now() end,
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

  -- The opening's portal: walk to the Den's entrance and read the scroll there.
  den_marker = {
    enter = function(m)
      players(m, 1)
      local route = travel.route(2, DEN)
      local hop = route and route[1]
      m.data.marker_spot = hop and hop.to == DEN and { x = hop.x, y = hop.y } or nil
      m.data.marker_mover, m.data.marker_cast, m.data.marker_casts, m.data.marker_fails = move.new(), nil, 0, 0
    end,
    tick = function(m)
      local me = game.me()
      local spot = m.data.marker_spot
      local function drop(why)
        m.data.opening_off = true
        return 'decide', why
      end
      if me.area ~= 2 or not spot then return drop('no way to the Den entrance from here') end
      if reflexes(me, CLEAR_SIGHT, true, OPENING.only(m, me)) then return end
      if m.data.marker_cast then
        local portal = world.nearest(game.units('object'), me, function(o)
          return o.portal and (o.owner_name == me.name or (o.class_id == 59 and o.owner_name == ''))
        end)
        if portal then
          m.data.den_marker = { x = portal.x, y = portal.y }
          print(string.format('opening: town portal stands at (%.0f,%.0f), %.1f from the Den entrance',
            portal.x, portal.y, world.distance(portal, spot)))
          return 'decide', 'the portal by the Den stands'
        end
        if now() - m.data.marker_cast < OPENING.cast_ms then return end
        m.data.marker_cast = nil
        if m.data.marker_casts >= OPENING.casts then return drop('the town portal would not open') end
      end
      if world.distance(me, spot) > OPENING.reach then
        local walk = m.data.marker_mover
        if walk.status ~= 'moving' then
          if walk.status == 'failed' then
            m.data.marker_fails = m.data.marker_fails + 1
            if m.data.marker_fails >= 3 then return drop('cannot reach the Den entrance: ' .. tostring(walk.reason)) end
            map.invalidate()
          end
          walk:to(spot.x, spot.y)
        end
        walk:tick()
        return
      end
      local scroll = portal_scroll()
      if not scroll then return drop('the town portal scroll is gone') end
      if act.ready() and act.use_item(scroll) ~= false then
        m.data.marker_cast, m.data.marker_casts = now(), m.data.marker_casts + 1
      end
    end,
  },

  -- Level 2 did not come on the way: fight on by the Cold Plains waypoint.
  opening_level = {
    enter = function(m)
      players(m, 1)
      explorer = explore.new(game.me().area)
      explorer.accept = function(unit) return worth(game.me(), unit) end
    end,
    tick = function(m)
      local me = game.me()
      if me.level >= OPENING.level or me.area ~= COLD_PLAINS then return 'decide' end
      if m:elapsed() > OPENING.level_ms then
        m.data.opening_leveled = true
        return 'decide', string.format('still level %d; on to the Den', me.level)
      end
      if reflexes(me, CLEAR_SIGHT, true) then return end
      if explorer:tick(me) == 'done' then
        m.data.opening_leveled = true
        return 'decide', 'Cold Plains searched'
      end
    end,
  },

  -- Scrolls of identify read on what she found: one request, then the wait
  -- to see the item known.
  identify = {
    enter = function(m)
      m.data.read_at, m.data.read_item, m.data.read_since = 0, nil, nil
    end,
    tick = function(m)
      local me = game.me()
      local scroll
      for _, item in ipairs(game.items()) do
        if item.location == 'container' and item.page == 0 and item.code == 'isc' then scroll = item; break end
      end
      local target = unread(me)[1]
      if not scroll or not target or me.cursor_item then return 'decide', 'nothing more to read' end
      if m.data.read_item ~= target.id then m.data.read_item, m.data.read_since = target.id, now() end
      if now() - m.data.read_since > READ.ms then
        m.data.read_failed = m.data.read_failed or {}
        m.data.read_failed[target.id] = true
        print('identify: ' .. tostring(target.name) .. ' would not be read')
        return
      end
      if act.ready() and now() - m.data.read_at >= READ.step_ms then
        print('identify: reading ' .. tostring(target.name))
        act.identify(target, scroll)
        m.data.read_at = now()
      end
    end,
  },

  -- One change of gear: the item onto the cursor, into its slot, and what
  -- came off into the pack. Each step is a request; the next waits to see it done.
  gear = {
    enter = function(m)
      m.data.gear_step, m.data.gear_at = 'lift', 0
    end,
    tick = function(m)
      local me = game.me()
      local change = m.data.gear_change
      local function held()
        for _, item in ipairs(game.items()) do
          if item.location == 'cursor' then return item end
        end
      end
      local function over(why, failed)
        if failed then
          m.data.gear_failed = m.data.gear_failed or {}
          m.data.gear_failed[change.item.id] = true
        end
        outfit_seen.key = nil
        m.data.gear_change = nil
        -- In town, what came off is for sale: one more visit for it.
        if me.in_town and not failed then
          m.data.town_done = nil
          visit = new_visit()
        end
        return 'decide', why
      end
      local item = held()
      if not change then
        -- Only something to put away.
        if not item then return 'decide' end
        if now() - m.data.gear_at < GEAR.step_ms then return end
        local x, y = gear_lib.free_cell(item.width or 1, item.height or 1)
        if not x or m:elapsed() > GEAR.ms then
          m.data.stow_failed = true
          return 'decide', 'no room in the pack for ' .. tostring(item.name)
        end
        act.place_item(item, 'inventory', x, y)
        m.data.gear_at = now()
        return
      end
      if m:elapsed() > GEAR.ms then
        -- Never left holding something: back into the pack with it first.
        if item then
          local x, y = gear_lib.free_cell(item.width or 1, item.height or 1)
          if x and now() - m.data.gear_at >= GEAR.step_ms then
            act.place_item(item, 'inventory', x, y)
            m.data.gear_at = now()
          end
          if x then return end
        end
        return over('the change of gear did not go through', true)
      end
      if now() - m.data.gear_at < GEAR.step_ms or not act.ready() and not item then return end
      if m.data.gear_step == 'lift' then
        if item and item.id == change.item.id then
          m.data.gear_step = 'wear'
        elseif not item then
          act.pick_item(change.item)
          m.data.gear_at = now()
        end
        return
      end
      if m.data.gear_step == 'wear' then
        if item and item.id == change.item.id then
          local sent, action = act.equip(change.slot)
          m.data.gear_at = now()
          if sent then
            m.data.gear_step = 'stow'
          elseif action == 0 then
            m.data.gear_step = 'stow' -- it will not go on after all: back into the pack
            m.data.gear_refused = true
          end
        end
        return
      end
      -- What came off, or what would not go on, into the pack.
      if item then
        local x, y = gear_lib.free_cell(item.width or 1, item.height or 1)
        if not x then return over('no room in the pack for ' .. tostring(item.name), true) end
        act.place_item(item, 'inventory', x, y)
        m.data.gear_at = now()
        return
      end
      if m.data.gear_refused then
        m.data.gear_refused = nil
        return over(tostring(change.item.name) .. ' would not go on', true)
      end
      for _, worn in ipairs(game.items()) do
        if worn.id == change.item.id and worn.location == 'equipped' then
          print(string.format('gear: wearing %s (%s)', tostring(change.item.name), tostring(change.gain)))
          m.data.gear_off = m.data.gear_off or {}
          for _, old in ipairs(change.removes or {}) do m.data.gear_off[old.id] = true end
          return over('wearing ' .. tostring(change.item.name))
        end
      end
    end,
  },

  -- The Blood Moor, searched for what its monsters and chests give, until
  -- she can pay for her potions or there is no more of it.
  opening_moor = {
    enter = function(m)
      players(m, 1)
      explorer = explore.new(game.me().area, nil, solid_zones, avoid_zones)
      explorer.accept = function(unit) return worth(game.me(), unit) end
    end,
    tick = function(m)
      local me = game.me()
      if me.area ~= 2 then return 'decide' end
      if means(me) >= SUPPLY_OK then
        m.data.moor_done = true
        return 'decide', string.format('%d gold and what sells for %d: on to the Cold Plains', stock.gold(me), pack_value())
      end
      note_packs(me)
      if reflexes(me, CLEAR_SIGHT, true) then return end
      if explorer:tick(me) == 'done' then
        m.data.moor_done = true
        return 'decide', 'Blood Moor searched'
      end
    end,
  },

  -- In town with the portal by the Den still open: find it and step through.
  den_return = {
    enter = function(m)
      players(m, 1, true) -- the Den is cleared at one player, and it spawns as she comes through
      m.data.return_mover, m.data.return_used, m.data.return_spot = move.new(), 0, 0
    end,
    tick = function(m)
      local me = game.me()
      local function done(why)
        m.data.den_marker, m.data.opening_off = nil, true
        return 'decide', why
      end
      if not me.in_town then return done('at the Den by the portal') end
      if m:elapsed() > OPENING.return_ms then return done('the portal to the Den was not found; walking') end
      local portal = world.nearest(game.units('object'), me, function(o)
        return o.portal and (o.owner_name == me.name or (o.class_id == 59 and o.owner_name == '' and o.destination == 2))
      end)
      local walk = m.data.return_mover
      if portal then
        if world.distance(me, portal) > 3 then
          if walk.status ~= 'moving' or m.data.return_to ~= portal.id then
            m.data.return_to = portal.id
            walk:to(portal.x, portal.y)
          end
          walk:tick()
        elseif act.ready() and now() - m.data.return_used > 1500 then
          act.interact(portal)
          m.data.return_used = now()
        end
        return
      end
      -- Units are only known near the player. Portals open where a game
      -- starts and where the last one stood.
      local spots = {}
      if flag('portal_x') then spots[#spots + 1] = { x = flag('portal_x'), y = flag('portal_y') } end
      if me0.in_town then spots[#spots + 1] = { x = me0.x, y = me0.y } end
      local spot = spots[m.data.return_spot + 1]
      if not spot then return done('the portal to the Den is not in town; walking') end
      if world.distance(me, spot) < 4 or walk.status == 'failed' then
        m.data.return_spot = m.data.return_spot + 1
        walk:stop()
        return
      end
      if walk.status ~= 'moving' then walk:to(spot.x, spot.y) end
      walk:tick()
    end,
  },

  den_reward = {
    tick = function(m)
      local me = game.me()
      if not me.in_town or m:elapsed() > 30000 then
        m.data.den_reward_tried = true
        return 'decide', 'Akara not reached'
      end
      if not visit:talk(me, 'Akara') then return end
      town.close()
      m.data.den_reward_tried, m.data.quests_synced = true, nil
      return 'decide', 'the Den reported to Akara'
    end,
  },

  clear = {
    enter = function(m) explorer = game.me().area == DEN and DEN_PACK.explorer(m) or explore.new(game.me().area) end,
    tick = function(m)
      local me = game.me()
      if me.area ~= DEN then return 'decide' end
      if quest_done(QUEST_DEN) then
        print(string.format('Den of Evil cleared (level %d, %d kills)', me.level, combat.kills))
        -- With the Cold Plains waypoint a new game is the way home and on:
        -- it starts beside Akara and the shop, and nothing is walked twice.
        if build.den_marker and known_waypoints()[COLD_PLAINS] then
          return 'save_exit', 'Den of Evil cleared; a new game starts in town with the Cold Plains waypoint'
        end
        if services.portal_scrolls() > 0 then
          m.data.portal_town_only = true
          return 'portal_heal', 'portal to Akara for the reward'
        end
        trip:to(TOWN)
        return 'travel', 'back to Akara for the reward'
      end
      local held = DEN_PACK.note(m, me)
      local pack = m.data.den_pack
      if pack and pack.waiting and not held then
        -- Hers while she waited: what was kept out of the search is in it again.
        pack.waiting = nil
        explorer = DEN_PACK.explorer(m)
      end
      if reflexes(me, CLEAR_SIGHT, nil, DEN_PACK.only(m, me)) then return end
      if explorer:tick(me) ~= 'done' then return end
      if held then
        -- Nothing else is left. She stands off while the pool fills; after
        -- DEN_PACK.wait_ms the pack is fought with what she has.
        if not pack.waiting then
          pack.waiting = now()
          print(string.format('den: the rest is searched; standing off for mana (%d of %d)', me.mana, me.max_mana))
        end
        if now() - pack.waiting < DEN_PACK.wait_ms then return end
        pack.last, pack.held, pack.waiting = true, false, nil
        print('den: the elite pack is next, with what she has')
        explorer = DEN_PACK.explorer(m)
      elseif not explorer:cleanup() then
        -- Only quest credit proves the Den cleared; revisit after exhaustive search.
        explorer = DEN_PACK.explorer(m, true)
        explorer:cleanup()
      end
    end,
  },

  -- Pick the most valuable area nearby, go there and clear it until the next
  -- goal is safe. Experience and time spent feed leveling.record.
  farm = {
    enter = function(m)
      players(m, 1)
      local me = game.me()
      m.data.spent = m.data.spent or {}
      local hops = leveling.hops_from(me.area)
      local goal = build.follow_pay and quest_done(QUEST_DEN) and objective(m, me)
      if goal then
        -- The objective is decided; this state only carries it out.
        local area = goal.area
        -- On foot or by its waypoint: by where she stands now, not where she
        -- stood when the objective was made. Made in the Tower, it said
        -- "walk"; sent to town from there with the portal judged unsafe, she
        -- then walked from the camp through five levels with the Black
        -- Marsh waypoint in her pocket.
        local by_waypoint = goal.via and 1 + (leveling.hops_from(goal.via)[area] or math.huge) or math.huge
        local walk = not me.in_town and hops[area] ~= nil and hops[area] <= by_waypoint
        m.data.farm_via = goal.via and not walk and { [area] = goal.via } or nil
        m.data.farm_commit = { area = area, mode = 'xp' }
        m.data.farm_area, m.data.farm_since, m.data.farm_xp, explorer = area, nil, nil, nil
        m.data.farm_supply, m.data.farm_gain_at = nil, nil
        m.data.farm_wp_walk, m.data.farm_portal_tried = nil, nil
        local via = m.data.farm_via and m.data.farm_via[area]
        if via and not me.in_town and hops[area] and known_waypoints()[via] then
          local onward = leveling.hops_from(via)[area] or math.huge
          local best, best_cost = nil, hops[area]
          for wp in pairs(known_waypoints()) do
            local cost = (hops[wp] or math.huge) + 1 + onward
            if wp ~= via and wp ~= TOWN and cost + 2 <= best_cost then best, best_cost = wp, cost end
          end
          m.data.farm_wp_walk = best
        end
        if area ~= me.area then trip:to(area) end
        return
      end
      -- No area is given anything beyond what it pays (the maintainer's
      -- rule): a third added for lying on the route put the Blood Moor above
      -- the crypts for a level-3 sorceress.
      local candidates = {}
      -- With no mana reserve, recover where the cheap staff fallback can fight.
      local dry = flag('gold_farm') and supplies(me, true) < SUPPLY_LOW
      m.data.farm_via = nil
      if flag('gold_farm') then
        candidates, m.data.farm_via = gold_farms(me, dry and math.max(GOLD_FARM_BELOW, me.level - 2) or nil)
      end
      if #candidates == 0 then
        -- Anywhere a known waypoint reaches (one hop past it), plus what is
        -- walkable from here; the planner weighs experience per second.
        candidates, m.data.farm_via = gold_farms(me, -math.max(ENTER_ABOVE, build.enter_gap or 0))
        local listed = {}
        for _, level in ipairs(candidates) do listed[level] = true end
        for level, distance in pairs(hops) do
          if listed[level] then distance = math.huge end
          if distance <= 3 and not NOT_FARMED[level] and level ~= DEN then
            candidates[#candidates + 1] = level
          end
        end
      end
      local xp_stop = not flag('gold_farm') and quest_done(QUEST_DEN)
      local onward
      if xp_stop then
        -- A stop to level: every area is weighed. One that ended in a chicken
        -- at this level is left out.
        local every
        every, m.data.farm_via, onward = all_farms()
        candidates = {}
        for _, level in ipairs(every) do
          if flag('farm_left') ~= level .. ':' .. me.level then candidates[#candidates + 1] = level end
        end
      else
        -- Resource farms stay on the main route; optional crypt/cave detours
        -- cannot repay their travel cost during this Act 1 run.
        local route_levels = { [me.area] = true }
        for _, hop in ipairs(travel.route(TOWN, CATACOMBS_4) or {}) do route_levels[hop.to] = true end
        local on_route = {}
        for _, level in ipairs(candidates) do
          local info = dry and game.level_info(level)
          if route_levels[level] and (not dry or (info and info.monster_level <= 2)) then
            on_route[#on_route + 1] = level
          end
        end
        candidates = on_route
      end
      if not quest_done(QUEST_DEN) then candidates = { 2 } end
      local fallback_candidates = candidates
      local forward_recovery = false
      if flag('gold_farm') and quest_done(QUEST_DEN) and hops[COLD_PLAINS] and not m.data.spent[COLD_PLAINS] then
        for _, level in ipairs(candidates) do
          if level == COLD_PLAINS then
            -- Cold Plains earns supplies on the way forward instead of
            -- sweeping the already traversed Moor for its better XP estimate.
            candidates, forward_recovery = { COLD_PLAINS }, true
            break
          end
        end
      end
      -- Levels reached by a waypoint are a jump away, not the walk the map
      -- shows: after a restart in town the Catacombs counted as fifteen hops
      -- and the Dark Wood, ten monster levels lower, won.
      local to_waypoint = me.in_town and 0 or math.huge
      for wp in pairs(known_waypoints()) do
        if wp ~= TOWN and hops[wp] then to_waypoint = math.min(to_waypoint, hops[wp]) end
      end
      local reach = {}
      for _, level in ipairs(candidates) do
        local wp = m.data.farm_via and m.data.farm_via[level]
        reach[level] = hops[level]
        if onward then
          -- A stop to level: one jump and the walk on from the waypoint.
          if wp then reach[level] = math.min(hops[level] or math.huge, 1 + onward[level]) end
        elseif wp and to_waypoint < math.huge then
          reach[level] = math.min(hops[level] or math.huge, to_waypoint + 1 + (level == wp and 0 or 1))
        end
      end
      local policy = combat.neural
      local power = xp_stop and policy and policy.cast_power and policy.cast_power(combat:known_skills()) or 0
      local options = { candidates = candidates, spent = m.data.spent, hops = reach,
        cast = power > 0 and power or nil }
      local area, why, ranking = leveling.choose(me, build, options)
      if forward_recovery and not area then
        forward_recovery = false
        options.candidates = fallback_candidates
        area, why, ranking = leveling.choose(me, build, options)
      end
      -- Stick with a choice until it is cleared or we are ready to move on;
      -- re-choosing after each waypoint hop let travel cost undo the choice.
      local kept = m.data.farm_commit
      local kept_info = kept and dry and game.level_info(kept.area)
      if kept and kept.mode == (flag('gold_farm') and 'gold' or 'xp') and not m.data.spent[kept.area]
        and not (forward_recovery and kept.area == 2)
        and (not dry or (kept_info and kept_info.monster_level <= 2)) then
        area, why = kept.area, game.level_name(kept.area) .. ' (continuing)'
      end
      m.data.farm_commit = area and { area = area, mode = flag('gold_farm') and 'gold' or 'xp',
        gate = kept and kept.area == area and kept.gate or level_gate } or nil
      m.data.farm_area, m.data.farm_since, m.data.farm_xp, explorer = area, nil, nil, nil
      m.data.farm_supply, m.data.farm_gain_at = nil, nil
      -- Out in the field and far from it: walk to a known waypoint first when
      -- that (walk + jump + the walk on from its waypoint) saves two levels.
      m.data.farm_wp_walk, m.data.farm_portal_tried = nil, nil
      local via = area and m.data.farm_via and m.data.farm_via[area]
      if area and via and not me.in_town and hops[area] and known_waypoints()[via] then
        local onward = leveling.hops_from(via)[area] or math.huge
        local best, best_cost = nil, hops[area]
        for wp in pairs(known_waypoints()) do
          local cost = (hops[wp] or math.huge) + 1 + onward
          if wp ~= via and wp ~= TOWN and cost + 2 <= best_cost then best, best_cost = wp, cost end
        end
        m.data.farm_wp_walk = best
      end
      if area then
        print('farming ' .. why .. (ranking and ranking ~= '' and ' (scores: ' .. ranking .. ')' or ''))
        if area ~= me.area then trip:to(area) end
      end
    end,
    tick = function(m)
      local me = game.me()
      local area = m.data.farm_area
      -- A boss is always fought: on her level the choice of area is over.
      if me.area == CATACOMBS_4 and not quest_done(QUEST_ANDARIEL) then return 'decide', 'on the level of Andariel' end
      if not flag('gold_farm') and select_waypoint(m, me, 'farm') then
        return 'activate_waypoint', 'a waypoint close by that she does not have'
      end
      -- The objective is the planner's to change, not this state's to keep:
      -- when it is another area now (she has become poor, or rich again), go back and ask.
      if build.follow_pay and quest_done(QUEST_DEN) and area then
        local goal = objective(m, me)
        if goal and goal.area ~= area then return 'decide', 'the objective is now ' .. game.level_name(goal.area) end
        -- Asked here, answered by the planner: a second asking there finds the same.
        local left = m.data.cash_left
        local sell = cash_in(m, me)
        m.data.cash_left = left
        if sell then return 'decide', 'what she carries would pay her way' end
      end
      if flag('gold_farm') and early_forward_ready(me) then
        set_flag('gold_farm', nil)
        m.data.farm_commit = nil
        return 'decide', 'life, mana and carried reserves ready; resume the main route'
      end
      if tower_descent_ready(me) then
        m.data.farm_commit = nil
        return 'decide', 'mana available; resume the Tower descent'
      end
      local restock, restock_why = recovery_restock(m, me)
      if restock then return restock, restock_why end
      local destination = area and game.level_info(area)
      if flag('gold_farm') and supplies(me, true) < SUPPLY_LOW and destination and destination.monster_level > 2 then
        m.data.farm_commit = nil
        return 'decide', 'mana reserve spent; recover within staff range'
      end
      local ready = flag('gold_farm') and supplies(me, true) >= recovery_target(me)
        or not flag('gold_farm') and level_ready(me)
      if ready then
        if m.data.farm_since then
          leveling.record(area, me.experience - m.data.farm_xp, (now() - m.data.farm_since) / 1000, me.level)
        end
        m.data.farm_commit = nil
        return 'decide', 'ready at level ' .. me.level
      end
      if not area then
        return 'save_exit', 'no unspent resource area; refresh the game'
      end
      if flag('gold_farm') and me.area == area then
        local supply = supplies(me)
        if not m.data.farm_supply or supply > m.data.farm_supply then
          m.data.farm_supply, m.data.farm_gain_at = supply, now()
        end
        if has_vendor_loot() then
          m.data.farm_commit = nil
          local route = travel.route(me.area, TOWN)
          if route and #route > 1 then return 'save_exit', 'sell recovery loot; town is far' end
          return 'retreat', 'sell recovery loot and restock'
        end
        if now() - m.data.farm_gain_at >= FARM_DRY_MS then
          m.data.spent[area], m.data.farm_commit = true, nil
          set_flag('farm_dry', true) -- survives the new game: do not come back for more of nothing
          if recovery_anchor(me) and known_waypoints()[COLD_PLAINS] then
            return 'save_exit', 'Cold Plains yielded no extra mana reserve for 90 s; refresh via its waypoint'
          end
          return 'decide', 'resource farm yielded no extra mana reserve for 90 s'
        end
      end
      if flag('gold_farm') and me.area == area and recovery_anchor(me) and select_waypoint(m, me, 'farm') then
        return 'activate_waypoint', 'activate the Cold Plains recovery anchor'
      end
      if me.area == area and not explorer then
        -- What is kept off on the way is kept off while searching too.
        -- Routes go around what is dangerous; nothing is sought on any ground kept off.
        explorer = explore.new(area, nil, solid_zones, avoid_zones)
        -- What is not worth fighting is not worth walking up to.
        explorer.accept = function(unit) return worth(game.me(), unit) end
      end
      -- Packs are judged on the way as much as on arrival: this state is how
      -- she gets everywhere now, and with the judging left to the arrival
      -- she walked down the stairs of the Underground Passage into seven
      -- archers and an elite pack, twice through her own portal as well.
      if me.area ~= area then replan_around(m, me, 'farm') else note_packs(me) end
      if reflexes(me, me.area == area and CLEAR_SIGHT or TRAVEL_SIGHT, true) then return end
      -- A native route may briefly cross a neighboring level before reaching its target.
      if me.area ~= area and explorer and explorer:tick(me) == 'moving' then return end
      -- From town, or from a waypoint on the way, a far farm is reached by
      -- its waypoint.
      local via = m.data.farm_via and m.data.farm_via[area]
      if via and me.area ~= area and not known_waypoints()[via] then
        -- The waypoint the area hangs on is not hers yet: she goes and gets it.
        if m.data.no_wp == via or flag('no_wp.' .. via) then
          m.data.farm_via[area] = nil
        elseif me.area == via then
          m.data.wp_spawn, m.data.wp_resume, m.data.wp_budget = waypoint_spawn(via), 'farm', OPENING.waypoint_ms
          return 'activate_waypoint', 'the waypoint that ' .. game.level_name(area) .. ' is reached from'
        else
          -- By the known waypoint nearest to it, when that is nearer than she is.
          local start, left = best_waypoint(via)
          local afoot = travel.route(me.area, via)
          if start and start ~= me.area and (me.in_town or (afoot and left + 2 <= #afoot)) then
            if me.in_town then
              m.data.waypoint_to = start
              return 'take_waypoint', 'waypoint to ' .. game.level_name(start) .. ', on the way to ' .. game.level_name(via)
            end
            if not m.data.farm_portal_tried and services.portal_scrolls() > 0 then
              m.data.farm_portal_tried = true
              m.data.portal_town_only, m.data.portal_passage = true, true
              return 'portal_heal', 'town portal, then on to the ' .. game.level_name(via) .. ' waypoint'
            end
          end
          if trip.status ~= 'moving' or trip.target ~= via then trip:to(via) end
          if trip:tick() == 'failed' then
            m.data.spent[area] = true
            return 'decide', 'cannot reach the ' .. game.level_name(via) .. ' waypoint'
          end
          return
        end
      end
      local wp_walk = m.data.farm_wp_walk
      if via and me.area ~= area and me.area ~= via and (me.in_town or me.area == wp_walk) then
        m.data.farm_wp_walk = nil
        m.data.waypoint_to = via
        return 'take_waypoint', 'waypoint to ' .. game.level_name(via) .. ' for ' .. game.level_name(area)
      end
      if wp_walk and me.area ~= wp_walk and not me.in_town then
        -- A town portal is the short way to a waypoint: the walk back to the
        -- last one is a level or two each time.
        if not m.data.farm_portal_tried and services.portal_scrolls() > 0 then
          m.data.farm_portal_tried = true
          m.data.portal_town_only, m.data.portal_passage = true, true
          return 'portal_heal', 'town portal, then the waypoint for ' .. game.level_name(area)
        end
        if trip.status ~= 'moving' or trip.target ~= wp_walk then trip:to(wp_walk) end
        if trip:tick() == 'failed' then m.data.farm_wp_walk = nil end
        return
      end
      if me.area ~= area then
        if trip.status ~= 'moving' or trip.target ~= area then trip:to(area) end
        if trip:tick() == 'failed' then
          m.data.spent[area] = true
          return 'decide', 'cannot reach ' .. game.level_name(area)
        end
        return
      end
      m.data.farm_since = m.data.farm_since or now()
      m.data.farm_xp = m.data.farm_xp or me.experience
      local goal = level_ready(me)
      if flag('gold_farm') then goal = supplies(me, true) >= recovery_target(me) end
      local exhausted = explorer:tick(me) == 'done'
      if goal or exhausted then
        leveling.record(area, me.experience - m.data.farm_xp, (now() - m.data.farm_since) / 1000, me.level)
        if exhausted then m.data.spent[area] = true end
        m.data.farm_commit = nil
        return 'decide', string.format('%s at level %d', exhausted and 'worthwhile targets exhausted' or 'ready', me.level)
      end
    end,
  },

  -- Town portal out of trouble: free heal and restock, then back through our portal.
  portal_heal = {
    enter = function(m, from)
      players(m, 1)
      -- Leaving a boss fight for town is still that fight: the survival rules
      -- go on treating it as one.
      m.data.portal_boss = from == 'andariel' or from == 'countess'
      m.data.from_area, m.data.cast_at, m.data.used_at, m.data.visit_done = game.me().area, nil, 0, false
      m.data.portal_move_at = 0
      m.data.portal_urgent_at, m.data.cast_try_at = nil, nil
      m.data.back_since, m.data.town_portal = nil, nil
      m.data.den_reward_talked = nil
      m.data.portal_return_unsafe, m.data.return_logged = nil, nil
      m.data.portal_departure = nil
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
        m.data.portal_urgent_at = nil
        -- Remember where our portal stands: the client only knows units near
        -- the player, and the visit takes us out of its range.
        local seen = our_portal()
        if seen then
          m.data.town_portal = { x = seen.x, y = seen.y }
          set_flag('portal_x', seen.x)
          set_flag('portal_y', seen.y)
        end
        -- Quest rewards require the NPC interaction even when no shopping or
        -- healing is needed; a stash-only service visit can skip Akara.
        -- (A portal taken only as the way to a waypoint has nothing to ask of Akara.)
        if m.data.portal_town_only and not m.data.portal_passage and not m.data.den_reward_talked then
          if not visit:talk(me, 'Akara') then return end
          town.close()
          m.data.den_reward_talked = true
          return
        end
        if not m.data.visit_done then
          -- Tick the visit to the end: its last steps close the shop properly.
          if visit:tick(me) ~= 'done' then return end
          visit = new_visit()
          m.data.visit_done = true
        end
        if m.data.portal_sell then
          -- Sold. If where she was is still where she belongs, she goes
          -- back through the portal; only with somewhere else to be does
          -- the trip end in town. (Ending there always, she left the Crypt
          -- seventeen seconds after reaching it and had it to walk to again.)
          m.data.portal_sell = nil
          local goal = build.follow_pay and objective(m, me)
          if goal and goal.area == m.data.from_area then
            m.data.portal_town_only, m.data.portal_passage = nil, nil
          end
        end
        if m.data.portal_town_only then
          m.data.portal_town_only, m.data.portal_passage = nil, nil
          m.data.town_done = true
          return 'decide', 'quest return and restock complete'
        end
        local origin = game.level_info(m.data.from_area)
        local reserve = flag('gold_farm') and recovery_target(me, m.data.from_area) or SUPPLY_LOW
        -- A build that does not farm gold goes back through its portal with
        -- what the visit could buy: giving the portal up cost the whole walk
        -- from the last waypoint, again and again.
        if build.gold_farm ~= false and supplies(me, true) < reserve and m.data.from_area ~= 2 and m.data.from_area ~= DEN
          and (not origin or origin.monster_level > 2) then
          if supplies(me) >= reserve then
            return 'save_exit', 'mana purchase failed despite funds; reset town services'
          end
          m.data.town_done = true
          if not flag('farm_dry') then set_flag('gold_farm', true) end
          return 'decide', 'mana restock short; recover supplies in an easy area'
        end
        -- A waypoint beats an unsafe portal only when it lands next door: from
        -- farther back the walk costs minutes, and we return healed and stocked.
        local waypoint, hops_left
        local boss_company = m.data.portal_boss and (m.data.portal_crowd or 0) >= BOSS_CROWD
        if m.data.portal_return_unsafe or boss_company then waypoint, hops_left = best_waypoint(m.data.from_area) end
        if waypoint and hops_left <= (boss_company and BOSS_RETURN_HOPS or UNSAFE_RETURN_HOPS) then
          print(string.format('portal_heal: %s; resume via waypoint %s; departure %s; life %.0f/%.0f',
            boss_company and string.format('boss fight left with %d of her company in sight', m.data.portal_crowd)
              or 'unsafe return',
            game.level_name(waypoint), m.data.portal_departure, me.life, me.max_life))
          m.data.town_done = true
          return 'decide', 'unsafe portal return; resume via waypoint in this game'
        end
        if not m.data.return_logged then
          print(string.format('portal_heal: return via portal (%s); departure %s; life %.0f/%.0f',
            m.data.portal_return_unsafe and 'unsafe, no progression waypoint' or 'departure clear',
            tostring(m.data.portal_departure), me.life, me.max_life))
          m.data.return_logged = true
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
      local nearby = portal and world.distance(me, portal) <= 6
      local scroll
      if not nearby and not m.data.cast_at then
        for _, item in ipairs(game.items()) do
          if item.location == 'container' and item.page == 0 and item.code == 'tsc' then
            scroll = item
            break
          end
        end
      end
      -- An old portal across the fight costs more life than opening one here.
      if portal and (nearby or (not scroll and not m.data.cast_at)) then
        if world.distance(me, portal) > 3 then
          if act.ready() and now() - m.data.portal_move_at > 400 then
            act.move(math.floor(portal.x), math.floor(portal.y), stamina.walk(me))
            m.data.portal_move_at = now() -- reaching the portal must not delay its first interaction
            return
          end
        elseif act.ready() and now() - m.data.used_at > 1500 then
          if not m.data.portal_town_only then
            local close, elites = 0, 0
            for _, unit in ipairs(combat:monsters()) do
              if world.hostile(unit) and unit.x and world.distance(portal, unit) <= 5
                and map.line_of_sight(portal.x, portal.y, unit.x, unit.y) then
                close = close + 1
                if unit.unique or unit.superunique or unit.champion or unit.boss then elites = elites + 1 end
              end
            end
            combat:observe_missiles(me)
            local fire = not combat:ground_clear(portal)
            local urgent = m.data.portal_urgent_at ~= nil
            local badly_hurt = me.life < me.max_life * RETREAT_LIFE
            m.data.portal_return_unsafe = close >= 2 or fire or (elites > 0 and (urgent or badly_hurt))
            local company, boss_seen = 0, false
            for _, unit in ipairs(m.data.portal_boss and combat:hostiles(me, CLEAR_SIGHT) or {}) do
              if unit.id == m.data.boss_id then boss_seen = true else company = company + 1 end
            end
            m.data.portal_crowd = boss_seen and company or 0
            m.data.portal_departure = string.format('%.1f,%.1f life %.0f/%.0f close %d elite %d fire %s urgent %s badly_hurt %s unsafe %s',
              portal.x, portal.y, me.life, me.max_life, close, elites, tostring(fire), tostring(urgent), tostring(badly_hurt), tostring(m.data.portal_return_unsafe))
            print('portal_heal: outbound ' .. m.data.portal_departure)
          end
          act.interact(portal)
          m.data.used_at = now()
          return
        end
        combat:dodge(me)
        return
      end
      if not m.data.cast_at then
        if scroll then
          if act.ready() and (not m.data.cast_try_at or now() - m.data.cast_try_at >= PORTAL_CAST_RETRY_MS) then
            m.data.cast_try_at = now()
            if act.use_item(scroll) ~= false then m.data.cast_at = now() end
            return
          end
          combat:dodge(me)
          return
        end
        if m.data.portal_urgent_at then return 'save_exit', 'urgent escape lost its town portal' end
        -- A walk of several levels only runs into its one-minute limit: a new game is the way home.
        local home = travel.route(me.area, TOWN)
        if home and #home > 1 then return 'save_exit', 'no town portal scroll; town is far' end
        return 'retreat', 'no town portal scroll'
      elseif now() - m.data.cast_at > 4000 then
        m.data.cast_at = nil -- the portal never appeared; try again
      end
      -- Keep monsters off while waiting, after giving portal actions first use of the tick.
      if combat:dodge(me) then return end
      if combat:threat(me, 5) then combat:fight(me, 5) end
    end,
  },

  retreat = {
    enter = function(m)
      players(m, 1)
      -- Keep the original deadline if the stall watchdog replans this walk.
      m.data.retreat_started = m.data.retreat_started or now()
      m.data.retreat_den = m.data.retreat_den or (game.me().area == DEN and not quest_done(QUEST_DEN))
      trip:to(TOWN)
    end,
    tick = function(m)
      local me = game.me()
      if me.in_town then
        m.data.retreat_started, m.data.retreat_den, m.data.town_done = nil, nil, nil
        m.data.retreat_replanned = nil
        return 'decide', 'back in town'
      end
      -- The cleared Den ends the game (see the clear state), whatever she was
      -- doing when its last monster fell: hurt and out of potions she had
      -- turned for home a moment before the credit, and walked all the way.
      if me.area == DEN and quest_done(QUEST_DEN) and build.den_marker and known_waypoints()[COLD_PLAINS] then
        m.data.retreat_started, m.data.retreat_den, m.data.retreat_replanned = nil, nil, nil
        return 'save_exit', 'Den of Evil cleared; a new game starts in town with the Cold Plains waypoint'
      end
      -- A level gained on the way refills life and mana: the reason to walk home is gone.
      if me.life >= me.max_life * RECOVERED_LIFE and (me.max_mana or 0) > 0 and me.mana >= me.max_mana * RECOVERED_MANA then
        m.data.retreat_started, m.data.retreat_den, m.data.retreat_replanned = nil, nil, nil
        return 'decide', 'recovered on the way to town'
      end
      if now() - m.data.retreat_started >= (m.data.retreat_den and DEN_RETREAT_MS or RETREAT_MS) then
        return 'save_exit', 'walk to town timed out'
      end
      -- Fight whatever blocks or chases us; otherwise keep walking.
      if combat:threat(me, 6) then
        combat:fight(me, 6)
        return
      end
      if trip:tick() == 'failed' then
        -- Leaving the game from the Den resets its clear: plan the walk once more first.
        print('retreat: walk failed (' .. tostring(trip.reason) .. ')')
        if m.data.retreat_replanned then return 'save_exit', 'walk to town failed' end
        m.data.retreat_replanned = true
        map.invalidate()
        trip:to(TOWN)
      end
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
      if her then m.data.boss_id = her.id end
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
        set_flag('tower_cleared', true)
        return 'decide', 'the Countess is dead'
      end
      if flag('tower_prep') and me.level >= TOWER_MAX_LEVEL then return 'decide', 'Tower preparation maximum level reached' end
      -- The reserve is topped up on the way in; once the fight is on, leaving
      -- is for the survival rules to decide.
      if not m.data.boss_engaged then
        local restock, why = boss_restock(m, me)
        if restock then return restock, why end
      end
      if her and her.alive and world.distance(me, her) < 25
        and map.line_of_sight(me.x, me.y, her.x, her.y) then
        m.data.boss_engaged = true
        combat:fight(me, 25, nil, her.id)
        return
      end
      if reflexes(me, CLEAR_SIGHT) then return end
      if explorer:tick(me) == 'done' then
        if explorer:cleanup() then return end
        m.data.countess_failed = true
        if flag('tower_prep') and not flag('tower_prep_done') then
          local failures = (flag('tower_failures') or 0) + 1
          set_flag('tower_failures', failures)
          print(string.format('Tower preparation: Countess missing after exhaustive sweep (%d/%d)', failures, TOWER_FAILURE_LIMIT))
          if failures >= TOWER_FAILURE_LIMIT then return 'decide', 'Tower preparation retry limit reached; reconsider the main route' end
        end
        return 'save_exit', 'cellar swept without finding the Countess; abandon this attempt'
      end
    end,
  },

  andariel = {
    -- Every way in is a new approach: its own trail, and its own walk back.
    enter = function(m) m.data.lure, m.data.lured, m.data.andariel_trail = nil, nil, nil end,
    tick = function(m)
      local me = game.me()
      if me.area ~= CATACOMBS_4 then return 'decide' end
      if quest_done(QUEST_ANDARIEL) then return 'done', 'Andariel is dead' end
      if m.data.andariel_credit_refresh then
        return 'save_exit', 'Andariel remains uncredited after a refresh attempt'
      end
      local andariel = world.nearest(game.units('monster'), me, function(u) return u.class_id == ANDARIEL_CLASS end)
      if andariel and not andariel.alive then
        m.data.andariel_credit_refresh = andariel.id
        return 'sync_quests', 'Andariel died; refresh quest credit'
      end
      if andariel then
        m.data.boss_id = andariel.id
        m.data.andariel_last_seen = {x=andariel.x,y=andariel.y,id=andariel.id}
        if not m.data.andariel_logged then
          m.data.andariel_logged = true
          print(string.format('andariel: life %.0f at %d%%, %.1f life per second, unique=%s superunique=%s boss=%s',
            value.life(andariel), andariel.life_percent or 100, value.dps(andariel), tostring(andariel.unique),
            tostring(andariel.superunique), tostring(andariel.boss)))
        end
      end
      -- The reserve is topped up on the way in; once the fight is on, leaving
      -- is for the survival rules to decide.
      if not m.data.boss_engaged then
        local restock, why = boss_restock(m, me)
        if restock then return restock, why end
      end
      local trail = m.data.andariel_trail or {}
      m.data.andariel_trail = trail
      if not m.data.lure and (not trail[#trail] or world.distance(me, trail[#trail]) >= TRAIL_STEP) then
        trail[#trail + 1] = { x = me.x, y = me.y }
        if #trail > TRAIL_POINTS then table.remove(trail, 1) end
      end
      -- How the duel goes, once a second: her life is what the fight is paid for.
      if andariel and andariel.alive and now() - (m.data.andariel_noted or 0) >= 1000 then
        local seen = m.data.andariel_seen
        local speed = seen and world.distance(seen, andariel) / math.max(0.001, (now() - seen.at) / 1000) or 0
        m.data.andariel_noted, m.data.andariel_seen = now(), { x = andariel.x, y = andariel.y, at = now() }
        print(string.format('andariel: %d%% at %.1f, %s, moving %.1f subtiles/s; life %d%% mana %d%%',
          andariel.life_percent or 100, world.distance(me, andariel),
          game.state('monster', andariel.id, 11) and 'chilled' or 'not chilled', speed,
          me.life / me.max_life * 100, (me.max_mana or 0) > 0 and me.mana / me.max_mana * 100 or 0))
      end
      -- Once begun, the walk back is finished whether or not she stays in sight.
      local lure = m.data.lure
      if lure and not m.data.lured then
        if world.distance(me, lure) > 3 and now() - lure.since <= LURE_MS
          and not (andariel and andariel.alive and world.distance(me, andariel) <= 5) then
          if mover.level or mover.x ~= lure.x or mover.y ~= lure.y or mover.status ~= 'moving' then
            mover:to(lure.x, lure.y)
          end
          if mover:tick() ~= 'failed' then return end
        end
        m.data.lured = true
      end
      if andariel and andariel.alive and world.distance(me, andariel) < 30
        and map.line_of_sight(me.x, me.y, andariel.x, andariel.y) then
        services.prepare_antidote(me) -- a delayed item reply must not suspend combat
        m.data.boss_engaged = true
        combat.boss_at = now() -- mana is fed ahead of the duel (combat:drink)
        if not m.data.lured then
          if not m.data.lure then
            local back, walked = nil, 0
            for i = #trail - 1, 1, -1 do
              walked = walked + world.distance(trail[i], trail[i + 1])
              back = trail[i]
              if walked >= LURE_BACK then break end
            end
            if back and walked >= LURE_MIN then
              m.data.lure = { x = math.floor(back.x), y = math.floor(back.y), since = now() }
              print(string.format('andariel: seen at %.0f; meeting her %.0f subtiles back at (%d,%d)',
                world.distance(me, andariel), walked, m.data.lure.x, m.data.lure.y))
            else
              m.data.lured = true
            end
          end
          if m.data.lure then return end -- the walk back starts on the next tick
        end
        -- Her company shoots and resurrects while she is being fought: it
        -- goes first, by the usual target choice, and she is singled out
        -- once it is gone.
        local company = false
        for _, unit in ipairs(combat:hostiles(me, ADDS_SIGHT)) do
          if unit.id ~= andariel.id then company = true; break end
        end
        combat:fight(me, 40, function(u)
          return u.id == andariel.id or world.distance(me, u) <= (company and ADDS_SIGHT or 5)
        end, not company and andariel.id or nil)
        return
      end
      -- The way in is run like any other (clearing it was tried: the elite
      -- Afflicted by the stairs ended five games in a row).
      if reflexes(me, TRAVEL_SIGHT) then return end
      local destination, source = andariel or m.data.andariel_last_seen, andariel and 'live' or 'last seen'
      if not destination then
        if not m.data.lair then
          for _, preset in ipairs(map.presets(CATACOMBS_4)) do
            if preset.type == 'monster' and preset.class_id == ANDARIEL_CLASS then m.data.lair = preset end
          end
        end
        if not m.data.lair then return 'failed', "Andariel's spawn is not in the map data" end
        destination, source = m.data.lair, 'spawn'
      end
      local goal = m.data.andariel_goal
      -- Keep subcell jitter from resetting route retries while following a moving boss.
      if not goal or world.distance(goal.observed, destination) > 2.5 then
        goal = {x=math.floor(destination.x),y=math.floor(destination.y),source=source,
          observed={x=destination.x,y=destination.y}}
        m.data.andariel_goal = goal
        print(string.format('andariel: approach %s position (%d,%d)', source, goal.x, goal.y))
      else
        goal.source = source
      end
      local status
      if mover.level or mover.x ~= goal.x or mover.y ~= goal.y then
        status = mover:to(goal.x, goal.y)
      elseif mover.status == 'idle' then
        status = mover:plan()
      else
        status = mover:tick()
      end
      if (status == 'failed' or status == 'arrived') and source == 'last seen' then
        -- She has moved on from where she was last seen (we came back on
        -- foot, or she followed us part of the way): look in her hall.
        print('andariel: not where she was last seen; trying her hall')
        m.data.andariel_last_seen, m.data.andariel_goal = nil, nil
        return
      end
      if status == 'failed' or status == 'arrived' then
        -- Refresh a stale collision snapshot once; never reset the mover's
        -- retries/door history every tick or renew this allowance after town.
        if not m.data.lair_refreshed then
          m.data.lair_refreshed = true
          map.invalidate()
          status = mover:plan()
        end
        if status == 'failed' or status == 'arrived' then
          if not m.data.andariel_terminal_logged then
            m.data.andariel_terminal_logged = true
            local endpoint = mover.path and mover.path[#mover.path]
            local function position(unit)
              return unit and string.format('%.2f,%.2f',unit.x,unit.y) or 'unavailable'
            end
            print(string.format('andariel: terminal approach boss=%s alive=%s position=(%s) distance=%s los=%s player=(%s) source=%s goal=(%s) endpoint=(%s) status=%s mover=%s refresh_used=%s',
              tostring(andariel and andariel.id),tostring(andariel and andariel.alive),position(andariel),
              tostring(andariel and world.distance(me,andariel)),
              tostring(andariel and map.line_of_sight(me.x,me.y,andariel.x,andariel.y)),position(me),source,
              position(goal),position(endpoint),status,tostring(mover.status),tostring(m.data.lair_refreshed)))
          end
          return 'save_exit', 'Andariel approach ' .. status .. ' without a visible boss after one map refresh'
        end
      end
    end,
  },

  -- Death: ESC brings us back to life in town; the corpse stays where we fell.
  -- Instead of a naked walk back, save and exit: in single player the corpse
  -- waits in town when the character re-enters (lib campaign re-enters).
  dead = {
    enter = function(m)
      players(m, 1)
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
    enter = function(m) players(m, 1) end,
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
      players(m, 1)
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
    enter = function(m) players(m, 1); m.data.grabs, explorer = 0, nil end,
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
        explorer = explorer or explore.new(area)
        if reflexes(me, CLEAR_SIGHT) then return end
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
-- Reached through `bot` by the survival tick, which has no upvalue to spare.
bot.px_setback, bot.keep_players, bot.progress_failed = px_setback, keep_players, progress_failed

function tick()
  if not game.ready() then return end
  local me = game.me()
  if bot:is('done') or bot:is('failed') then return end
  if not me.alive then
    if not bot:is('dead') then bot:go('dead', 'died') end
    return bot:tick()
  end
  if bot.data.quests_synced and quest_done(QUEST_ANDARIEL) then
    return bot:go('done', 'Andariel quest credited')
  end
  if me.area >= CATACOMBS_1 and me.area <= CATACOMBS_4 and not flag('deep') then set_flag('deep', true) end
  -- Reconcile before combat/loot/dodges can move across an open level border.
  if bot:is('travel') then travel_players(bot, me) end
  bot.keep_players(bot, me)
  if explorer then explorer:observe(me, combat:monsters()) end
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
  -- Standing off for mana (see DEN_PACK) is not a stall, nor is the minute after it.
  if bot:is('clear') and bot.data.den_pack and bot.data.den_pack.waiting then stall.since = now() end
  if mark ~= stall.mark then
    stall.mark, stall.since = mark, now()
    if now() - (stall.last or 0) > 120000 then stall.count = 0 end
  elseif now() - stall.since > 60000
    and not (bot:is('portal_heal') and bot.data.portal_urgent_at and not bot.data.visit_done) then
    stall.count, stall.since, stall.last = stall.count + 1, now(), now()
    local function count(kind) return stock.carried()[kind] end
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
  -- Gain watchdog: the position watchdog cannot see pacing between two places
  -- (a route that keeps re-planning across an open border, for example).
  stall.areas = stall.areas or {}
  if not stall.areas[me.area] then stall.areas[me.area], stall.area_count = true, (stall.area_count or 0) + 1 end
  local gain = string.format('%d:%d:%d:%d:%d', me.experience, me.gold + (me.stash_gold or 0), combat.kills,
    stall.area_count, math.floor((combat.target_life or 0) / 5))
  if gain ~= stall.gain or me.in_town then
    stall.gain, stall.gain_at = gain, now()
  elseif now() - stall.gain_at > NO_GAIN_MS and not bot:is('save_exit') then
    stall.gain_at = now()
    print(string.format('no gain for %d s in state %s (%s)', NO_GAIN_MS / 1000, bot.state, game.level_name(me.area)))
    return bot:go('save_exit', 'no experience, gold or new ground for four minutes')
  end
  combat:upkeep(me)
  stamina.upkeep(me)
  -- Early gold is scarce. Staff hits can bridge a mana shortage in easy areas
  -- until a town trip can buy a useful reserve or turn carried loot into one.
  local area = game.level_info(me.area)
  local easy = me.area == 2 or me.area == DEN or (area and area.monster_level <= 2)
  local funded = me.gold + (me.stash_gold or 0) >= 2 * MANA_POTION_GOLD
  if easy and not funded then funded = has_vendor_loot() end
  -- A restock is only worth leaving the fight for when town is a portal or a
  -- short walk away; a walk home out of the Den costs more than staff hits.
  local town_near = me.area == 2 or spare_scrolls() > 0
  local den_retreat = bot:is('retreat') and bot.data.retreat_den and not quest_done(QUEST_DEN)
  -- Out of mana and broke, anywhere, the staff is tried before the game is
  -- given up (the maintainer's rule): there is nothing a trip to town could
  -- buy, and the policy still judges each fight by what it would cost her.
  local broke_dry = not funded and not has_vendor_loot()
  -- In broke mode (see MEANS) the staff always comes before a restock: she
  -- is there to earn, and a trip to town spends what she has not got.
  local broke_mode = bot.data.poor or false
  local melee_ok = easy and (not funded or not town_near or den_retreat) or broke_dry or broke_mode
  if combat.melee_ok ~= melee_ok then
    print('act1: mana fallback ' .. (den_retreat and 'defensive melee on the walk home'
      or melee_ok and (broke_mode and 'staff hits: broke mode' or easy and 'staff hits in an easy area'
        or 'staff hits: dry and broke') or 'restock when dry'))
  end
  combat.melee_ok = melee_ok
  -- In the Den the pool is saved for its elite pack, until that is dead:
  -- the staff does for the rest what it can (see DEN_PACK).
  combat.save_mana = bot:is('clear') and me.area == DEN and not (bot.data.den_pack and bot.data.den_pack.gone) or false
  local life = me.max_life > 0 and me.life / me.max_life or 1
  local boss_fight = bot:is('andariel') or bot:is('countess') or (bot:is('portal_heal') and bot.data.portal_boss) or false
  local boss = bot.data.boss_id and boss_fight and game.unit('monster', bot.data.boss_id)
  local boss_left = boss and boss.alive and (boss.life_percent or 100) / 100 or nil
  local function ahead(lead)
    return boss_left ~= nil and (boss_left < life * lead or boss_left <= RACE.finish)
  end
  local hurt = life < RETREAT_LIFE and stock.healing() == 0 and not ahead(RACE.hurt)
  local in_transit = bot:is('travel') or bot:is('activate_waypoint') or bot:is('take_waypoint')
    or (bot:is('farm') and bot.data.farm_area ~= me.area)
  -- Out of mana where we fight for a living is a reason to restock. On the
  -- way somewhere it is only when town is a portal away and the gold is there.
  local dry
  if in_transit then
    dry = combat:dry(me) and spare_scrolls() > 0
      and (stock.gold(me) >= SUPPLY_OK * MANA_POTION_GOLD or has_vendor_loot())
  else
    dry = combat:out_of_mana() and combat:threat(me, CLEAR_SIGHT)
  end
  -- Chicken: a fight going wrong ends with leaving the game, which takes no
  -- cast and cannot be blocked; potions and portals are too slow once a pack
  -- is on us. A new game and a waypoint bring us back.
  local t = now()
  stall.life = stall.life or {}
  table.insert(stall.life, { at = t, life = life, hp = me.life })
  while stall.life[1] and t - stall.life[1].at > BURST_MS do table.remove(stall.life, 1) end
  local peak, peak_hp = life, me.life
  for _, sample in ipairs(stall.life) do
    peak = math.max(peak, sample.life)
    peak_hp = math.max(peak_hp, sample.hp)
  end
  local close, near = #combat:hostiles(me, 5), #combat:hostiles(me, 8)
  combat:observe_missiles(me)
  local burning = not me.in_town and not combat:ground_clear(me)
  if burning then near = math.max(near, 1) end -- environmental fire can outlive every monster
  -- Shooters kill from beyond melee reach: an archer pack took a healthy
  -- sorceress down from 71% in three seconds without ever counting as near.
  if near == 0 and not me.in_town then
    if #(combat.missile_lanes or {}) > 0 then near = 1 end
    for _, unit in ipairs(near == 0 and combat:hostiles(me, CLEAR_SIGHT) or {}) do
      if unit.ranged then near = 1; break end
    end
  end
  local chicken
  if life < CHICKEN_CROWDED and close >= 2 and not ahead(0) then
    chicken = string.format('%d%% life with %d monsters close', math.floor(life * 100), close)
  elseif life < CHICKEN_LOW and near >= 1 then
    chicken = string.format('%d%% life with %s', math.floor(life * 100), burning and 'ground fire' or 'monsters near')
  elseif peak - life >= CHICKEN_BURST and life < BURST_BELOW and near >= 1 and not combat:winning(COMMIT_OUTLAST)
    and not ahead(0) then
    chicken = string.format('lost %d%% life in %d s', math.floor((peak - life) * 100), math.floor(BURST_MS / 1000))
  elseif life < (boss_fight and BURST_BELOW or PORTAL_DANGER_LIFE)
    and life - (peak - life) <= CHICKEN_LOW and near >= 1 and not combat:winning(COMMIT_OUTLAST)
    and not ahead(RACE.chicken) then
    -- (In a boss fight a new game restores the boss: there the portal rule
    -- acts first and this one waits for the lower threshold, also while the
    -- portal it asked for is being opened. Forgetting that then left the
    -- game a third of a second after the portal was decided, twice.)
    -- Losing life at this rate, the next two seconds end below the last-ditch threshold.
    chicken = string.format('%d%% life and falling %d%% in %d s', math.floor(life * 100),
      math.floor((peak - life) * 100), math.floor(BURST_MS / 1000))
  end
  if chicken and not me.in_town and not bot:is('save_exit') then
    stall.life = {}
    if me.class == 'sorceress' and me.difficulty == 0 and me.area >= 20 and me.area <= COUNTESS_CELLAR
      and tower_px_exposure > 1 then
      local cap = flag('tower_px_cap') or 8
      local next_cap = math.min(cap, tower_px_exposure >= 8 and 3 or 1)
      if next_cap < cap then
        set_flag('tower_px_cap', next_cap)
        print(string.format('act1: Tower PX cap P%d -> P%d after chicken with P%d exposure', cap, next_cap, tower_px_exposure))
      end
    end
    -- Where levelling ended in a chicken is not where to level next (at this level).
    if bot:is('farm') and bot.data.farm_area == me.area and not flag('gold_farm') then
      set_flag('farm_left', me.area .. ':' .. me.level)
    end
    bot.px_setback(me, 'a chicken')
    bot.progress_failed(me)
    print(string.format('act1: chicken in %s at level %d among %s', game.level_name(me.area), me.level, crowd(me)))
    bot:go('save_exit', 'chicken: ' .. chicken)
  elseif life < PORTAL_DANGER_LIFE and peak_hp - me.life >= me.max_life * PORTAL_DANGER_LOSS and near >= 1
    and not me.in_town and not bot:is('save_exit') and not combat:winning(COMMIT_OUTLAST) and not ahead(RACE.portal)
    and (not bot:is('portal_heal') or not bot.data.visit_done)
    and ((bot:is('portal_heal') and not bot.data.visit_done) or emergency_scrolls() > 0 or world.nearest(game.units('object'), me, function(o)
      return o.portal and (o.owner_name == me.name or (o.class_id == 59 and o.owner_name == ''))
        and world.distance(me, o) <= 6
    end)) then
    combat.resting = false
    if not bot:is('portal_heal') then
      bot.px_setback(me, 'an emergency portal')
      print(string.format('act1: rapid life loss in %s at level %d among %s', game.level_name(me.area), me.level, crowd(me)))
      local again = emergency_again(bot, me)
      -- (A boss already hurt is worth more trips: a new game gives her life back.
      -- One still whole is not: seven trips went to an elite pack at her stairs.)
      if again and (not boss_fight or (boss_left or 1) >= 1) then
        bot:go('save_exit', string.format('emergency %d in the same place; a new game has other monsters there', again))
        return
      end
      bot:go('portal_heal', 'rapid life loss; portal before chicken')
    end
    bot.data.portal_urgent_at = bot.data.portal_urgent_at or t
  elseif (bot:is('andariel') or bot:is('countess')) and life < BOSS_PORTAL_LIFE and near >= 1
    and not me.in_town and not ahead(RACE.portal) and not combat:winning(COMMIT_OUTLAST)
    and services.portal_scrolls() > 0 then
    combat.resting = false
    print(string.format('act1: losing slowly in %s at level %d among %s', game.level_name(me.area), me.level, crowd(me)))
    bot:go('portal_heal', 'boss fight going down slowly; heal in town before it is a chicken')
    bot.data.portal_urgent_at = bot.data.portal_urgent_at or t
  elseif (hurt or dry) and not me.in_town and not bot:is('retreat') and not bot:is('portal_heal')
    and not bot:is('save_exit') then
    local why = hurt and 'low life and no potions' or 'out of mana and mana potions'
    local route = travel.route(me.area, TOWN)
    local after
    combat.resting = false
    if spare_scrolls() > 0 then
      after = 'portal_heal'
    elseif marker_stands() and me.area == COLD_PLAINS then
      -- The opening's own way to town is the waypoint ahead, and its portal the way back.
      if known_waypoints()[COLD_PLAINS] and not bot:is('take_waypoint') and not bot:is('decide') then
        after, why = 'decide', why .. '; by the waypoint, with the portal to the Den standing'
      end
    elseif route and #route > 1 and not (me.area == DEN and not quest_done(QUEST_DEN)) then
      -- Town is far: save and exit is the instant way back; a waypoint returns us.
      after, why = 'save_exit', why .. ', town is far'
    else
      after = 'retreat'
    end
    -- Out of mana and broke, with town far and no scroll: a new game would
    -- give her nothing she could buy with, so she fights on with the staff
    -- (a portal or a short walk home is still taken: the healer is free).
    if not after or (not hurt and (broke_mode or (after == 'save_exit' and broke_dry))) then
      combat.resting = false -- she goes on as she is
    -- Out of mana but healthy: first pick up what the fight dropped. That gold
    -- and loot is what buys the next round of potions.
    elseif not hurt and not bot:is('collect') and gather:nearby(me) then
      bot.data.after, bot.data.after_why = after, why
      bot:go('collect', why .. '; collecting loot first')
    elseif not bot:is('collect') then
      bot:go(after, why)
    end
  end
  -- Reactive dodges must not indefinitely postpone a failed recovery deadline.
  if bot:is('portal_heal') then
    if me.in_town then bot.data.portal_urgent_at = nil end
    if not me.in_town and not bot.data.visit_done and bot.data.portal_urgent_at
      and now() - bot.data.portal_urgent_at >= PORTAL_ESCAPE_MS then
      bot:go('save_exit', 'urgent town portal timed out')
    end
  end
  if bot:is('retreat') and not me.in_town and bot.data.retreat_started
    and now() - bot.data.retreat_started >= (bot.data.retreat_den and DEN_RETREAT_MS or RETREAT_MS) then
    bot:go('save_exit', 'walk to town timed out')
  end
  if bot:is('activate_waypoint') and bot.data.wp_since
    and now() - bot.data.wp_since > (bot.data.wp_timeout or OPTIONAL_WAYPOINT_MS) then
    return bot:tick() -- settle the expired attempt or its open panel before another dodge
  end
  if bot:is('sync_quests') and bot:elapsed() > QUEST_SYNC_MS then
    return bot:tick() -- post-kill synchronization also runs outside town
  end
  if bot:is('portal_heal') and not me.in_town and not bot.data.visit_done then
    return bot:tick() -- outbound portal requests own the tick ahead of loot and reactive dodges
  end
  if not bot:is('save_exit') then
    loot:grab(me)
    local target, passage
    if bot:is('travel') then
      local accept = travel_filter(me)
      target = combat.target_id and game.unit('monster', combat.target_id)
      if target and combat.sight then combat.sight[target.id] = nil end
      -- Global avoidance should share an accepted fight target, not pick a new leash.
      if not (target and combat:usable(target) and world.distance(me, target) <= TRAVEL_SIGHT
        and accept(target) and (value.priority(target, me, combat.build.element) >= 0
          or world.distance(me, target) < 4)) then
        target = combat:threat(me, TRAVEL_SIGHT, accept)
      end
      if not target then passage = passage_heading end
    elseif bot:is('farm') and not combat.target_id then
      -- Searching a level or crossing it with nothing to fight: the same.
      passage = passage_heading
    end
    local retain_engagement = bot:is('clear') or bot:is('farm') or bot:is('countess') or bot:is('andariel')
    if combat:dodge(me, target, passage, retain_engagement) then return end
  end
  bot:tick()
end
