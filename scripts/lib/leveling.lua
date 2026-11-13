-- Chooses where to gain experience, weighing what an area pays the build
-- against how far away it is. Nothing else enters the score: no area is
-- given a bonus for what a visit might also achieve.
--
--   local leveling = require 'lib.leveling'
--   local area, why = leveling.choose(me, build, { candidates = {...} })
--
-- Worth per area starts from game data (value.area: experience per point of
-- effective damage, spawn density) and is replaced by the rate actually
-- measured there at the same character level (leveling.record): what an
-- area paid three levels ago says nothing once its monsters have fallen
-- behind. Areas whose monsters out-level the character by more than
-- SAFE_ABOVE are skipped.

local value = require 'lib.value'

local leveling = {}

local SAFE_ABOVE = 2
-- The way there is paid once, in time: a level's walk against the stay it
-- buys. The stay is the area itself and whatever lies within NEAR hops of it
-- that pays at least NEAR_SHARE as much, each good for one clearing. A flat
-- discount per hop kept a sorceress in the two crypts beside the Cold Plains
-- waypoint at level 8, with the Tower's five cellars eight hops away on foot
-- and scored at less than half their worth.
local HOP_SECONDS = 40    -- crossing a level on the way
local CLEAR_SECONDS = 180 -- what one area occupies her for
local NEAR, NEAR_SHARE = 2, 0.7


-- Fewest level hops from `from` to every level reachable through map.exits.
local hops_cache = {} -- the act's layout does not change within a game
function leveling.hops_from(from)
  if hops_cache[from] then return hops_cache[from] end
  local hops, queue, head = { [from] = 0 }, { from }, 1
  local complete = true
  while queue[head] do
    local level = queue[head]
    head = head + 1
    local ok, exits = pcall(map.exits, level)
    if not ok then complete = false end -- a level not yet readable: ask again next time
    if ok then
      for _, exit in ipairs(exits) do
        if not hops[exit.level] then
          hops[exit.level] = hops[level] + 1
          queue[#queue + 1] = exit.level
        end
      end
    end
  end
  if complete then hops_cache[from] = hops end
  return hops
end

-- Measurements live in the shared store so they outlast each game's script.
local function key(level, player_level, what)
  return 'leveling.' .. level .. '.' .. (player_level or 0) .. '.' .. what
end

function leveling.record(level, experience, seconds, player_level)
  store.set(key(level, player_level, 'experience'), (store.get(key(level, player_level, 'experience')) or 0) + experience)
  store.set(key(level, player_level, 'seconds'), (store.get(key(level, player_level, 'seconds')) or 0) + seconds)
end

-- Experience per minute measured in an area at a character level, once there is enough data.
function leveling.measured_rate(level, player_level)
  local seconds = store.get(key(level, player_level, 'seconds')) or 0
  if seconds >= 60 then return (store.get(key(level, player_level, 'experience')) or 0) / seconds * 60 end
end

-- Prior worth of an area for a build: higher is better, comparable across areas.
-- `cast`: what one cast of her attack does (see value.area), when known.
function leveling.prior(level, build, player_level, cast)
  local area = value.area(level, build.element, player_level, cast)
  if not area then return nil end
  return area.experience_per_damage * area.density / 100, area
end

-- Ratio of measured experience per minute to the prior, learned from every
-- area with enough data, so estimates and measurements share one scale.
function leveling.calibration(build, player_level, cast)
  local measured_total, prior_total = 0, 0
  for level = 1, 200 do
    local rate = leveling.measured_rate(level, player_level)
    local prior = rate and leveling.prior(level, build, player_level, cast)
    if rate and prior and prior > 0 then
      measured_total, prior_total = measured_total + rate, prior_total + prior
    end
  end
  if prior_total > 0 then return measured_total / prior_total end
end

-- options.candidates: level ids to consider; options.spent:
-- level -> true for areas already cleared this game; options.hops: level ->
-- hops, where a waypoint makes the trip shorter than the walk; options.floor:
-- lowest monster level worth the stop (ignored when nothing reaches it);
-- options.cast: the damage of one cast of her attack, so that overkill counts.
-- Also returns every candidate's score, best first, for the log, and the
-- best score.
function leveling.choose(me, build, options)
  local best, reason, ranking, score = leveling.rank(me, build, options, options.floor)
  if not best and options.floor then best, reason, ranking, score = leveling.rank(me, build, options, nil) end
  return best, reason, ranking, score
end

function leveling.rank(me, build, options, floor)
  local hops = options.hops or leveling.hops_from(me.area)
  local scale = leveling.calibration(build, me.level, options.cast)
  local best, best_score, best_reason
  local ranking = {}
  -- What each open candidate pays where it stands.
  local open = {}
  for _, level in ipairs(options.candidates) do
    if not (options.spent and options.spent[level]) and hops[level] then
      local prior, area = leveling.prior(level, build, me.level, options.cast)
      -- Where the build may walk in it may also level: with a narrower
      -- bound here it entered the Barracks, found the Jail beyond gated,
      -- and walked back out to level in the Dark Wood.
      if prior and area.monster_level <= me.level + math.max(SAFE_ABOVE, build.enter_gap or 0)
        and (not floor or area.monster_level >= floor) then
        local rate = leveling.measured_rate(level, me.level)
        -- Experience per minute where known; otherwise the calibrated prior.
        local worth = rate or (scale and prior * scale) or prior
        open[#open + 1] = { level = level, worth = worth, area = area,
          kind = rate and 'measured' or (scale and 'calibrated' or 'estimated') }
      end
    end
  end
  for _, entry in ipairs(open) do
    local around, stay = leveling.hops_from(entry.level), CLEAR_SECONDS
    for _, other in ipairs(open) do
      if other ~= entry and (around[other.level] or math.huge) <= NEAR and other.worth >= entry.worth * NEAR_SHARE then
        stay = stay + CLEAR_SECONDS
      end
    end
    local level = entry.level
    local score = entry.worth * stay / (stay + hops[level] * HOP_SECONDS)
    ranking[#ranking + 1] = { score = score, text = string.format('%s %.1f', game.level_name(level), score) }
    if not best_score or score > best_score then
      best, best_score = level, score
      best_reason = string.format('%s: %s %.1f, %d hops for a stay of %d s, monster level %d', game.level_name(level),
        entry.kind, entry.worth, hops[level], stay, entry.area.monster_level)
    end
  end
  table.sort(ranking, function(a, b) return a.score > b.score end)
  local texts = {}
  for index, entry in ipairs(ranking) do texts[index] = entry.text end
  return best, best_reason, table.concat(texts, ', '), best_score
end

return leveling
