-- Stamina: run while it lasts, walk when it runs low, and stand still when it
-- is empty, because an exhausted character that keeps walking never recovers.
--
--   local stamina = require 'lib.stamina'
--   act.move(x, y, stamina.walk(me))          -- every movement request
--   if stamina.must_rest(me) then return end -- skip moving this tick

local stamina = {}

local WALK_BELOW = 0.05   -- switch to walking under this fraction
local RUN_ABOVE = 0.15    -- and back to running over this one
local RESUME_AT = 0.20    -- after emptying, stand until this much has returned

local walking, resting = false, false

local function fraction(me)
  return me.max_stamina > 0 and me.stamina / me.max_stamina or 1
end

-- True when the next move should be a walk.
function stamina.walk(me)
  local f = fraction(me)
  if walking and f > RUN_ABOVE then walking = false end
  if not walking and f < WALK_BELOW then walking = true end
  return walking
end

-- Whether a hostile is close enough that standing still would be unsafe.
local function threatened(me)
  for _, unit in ipairs(game.units('monster')) do
    if unit.alive and unit.attackable and not unit.npc and unit.x
      and (unit.x - me.x) ^ 2 + (unit.y - me.y) ^ 2 < 100 then
      return true
    end
  end
  return false
end

-- True while the character should stand still to recover. Never with a
-- hostile within 10 subtiles: in a fight we keep walking rather than freeze.
function stamina.must_rest(me)
  local f = fraction(me)
  if not resting and me.stamina < 1 then resting = true end
  if resting and f >= RESUME_AT then resting = false end
  return resting and not threatened(me)
end

return stamina
