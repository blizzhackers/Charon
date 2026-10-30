-- Chooses where to gain experience, weighing what an area is worth to the
-- build against how far away it is and what else a visit achieves.
--
--   local leveling = require 'lib.leveling'
--   local area, why = leveling.choose(me, build, { candidates = {...}, bonus = { [17] = 1.0 } })
--
-- Worth per area starts from game data (value.area: experience per point of
-- effective damage, spawn density) and is replaced by the rate actually
-- measured there (leveling.record). Areas whose monsters out-level the
-- character by more than SAFE_ABOVE are skipped.

local value = require 'lib.value'

local leveling = {}

local SAFE_ABOVE = 2
-- Each level of travel divides the score by (1 + HOP_COST * hops): a hop is
-- a minute or less of walking against several minutes of farming.
local HOP_COST = 0.15


-- Fewest level hops from `from` to every level reachable through map.exits.
function leveling.hops_from(from)
  local hops, queue, head = { [from] = 0 }, { from }, 1
  while queue[head] do
    local level = queue[head]
    head = head + 1
    local ok, exits = pcall(map.exits, level)
    if ok then
      for _, exit in ipairs(exits) do
        if not hops[exit.level] then
          hops[exit.level] = hops[level] + 1
          queue[#queue + 1] = exit.level
        end
      end
    end
  end
  return hops
end

-- Measurements live in the shared store so they outlast each game's script.
local function key(level, what) return 'leveling.' .. level .. '.' .. what end

function leveling.record(level, experience, seconds)
  store.set(key(level, 'experience'), (store.get(key(level, 'experience')) or 0) + experience)
  store.set(key(level, 'seconds'), (store.get(key(level, 'seconds')) or 0) + seconds)
end

-- Experience per minute measured in an area, once there is enough data.
function leveling.measured_rate(level)
  local seconds = store.get(key(level, 'seconds')) or 0
  if seconds >= 60 then return (store.get(key(level, 'experience')) or 0) / seconds * 60 end
end

-- Prior worth of an area for a build: higher is better, comparable across areas.
function leveling.prior(level, build, player_level)
  local area = value.area(level, build.element, player_level)
  if not area then return nil end
  return area.experience_per_damage * area.density / 100, area
end

-- Ratio of measured experience per minute to the prior, learned from every
-- area with enough data, so estimates and measurements share one scale.
function leveling.calibration(build, player_level)
  local measured_total, prior_total = 0, 0
  for level = 1, 200 do
    local rate = leveling.measured_rate(level)
    local prior = rate and leveling.prior(level, build, player_level)
    if rate and prior and prior > 0 then
      measured_total, prior_total = measured_total + rate, prior_total + prior
    end
  end
  if prior_total > 0 then return measured_total / prior_total end
end

-- options.candidates: level ids to consider; options.bonus: level -> extra
-- worth for dual purpose (quest reward, waypoint, mercenary); options.spent:
-- level -> true for areas already cleared this game.
function leveling.choose(me, build, options)
  local hops = leveling.hops_from(me.area)
  local scale = leveling.calibration(build, me.level)
  local best, best_score, best_reason
  for _, level in ipairs(options.candidates) do
    if not (options.spent and options.spent[level]) and hops[level] then
      local prior, area = leveling.prior(level, build, me.level)
      if prior and area.monster_level <= me.level + SAFE_ABOVE then
        local rate = leveling.measured_rate(level)
        -- Experience per minute where known; otherwise the calibrated prior.
        local worth = rate or (scale and prior * scale) or prior
        worth = worth * (1 + (options.bonus and options.bonus[level] or 0))
        local score = worth / (1 + HOP_COST * hops[level])
        if not best_score or score > best_score then
          best, best_score = level, score
          best_reason = string.format('%s: %s %.1f, %d hops, monster level %d', game.level_name(level),
            rate and 'measured' or (scale and 'calibrated' or 'estimated'), worth, hops[level], area.monster_level)
        end
      end
    end
  end
  return best, best_reason
end

return leveling
