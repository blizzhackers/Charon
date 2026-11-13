-- Stamina: run with a reserve, then recover it while continuing to walk.
--
--   local stamina = require 'lib.stamina'
--   stamina.upkeep(me)                      -- once per tick, before moving
--   act.move(x, y, stamina.walk(me))          -- every movement request

local stamina = {}

local WALK_BELOW = 0.20   -- reserve for the movement segment already in progress
local RUN_ABOVE = 0.60    -- rebuild enough for a useful run, avoiding rapid toggles
local DRINK_BELOW = 0.40  -- refill before the walking reserve is exhausted
local RETRY_MS = 3000     -- refused uses must not flood the client
local REFILL_MS = 20000   -- leave an acknowledged potion time to cover travel

local walking = false
local pending, retry_at = nil, 0

local function fraction(me)
  return me.max_stamina > 0 and me.stamina / me.max_stamina or 1
end

-- Inventory disappearance or a useful refill acknowledges the request; no
-- guessed game state id is needed. Returns true only when issuing a use.
function stamina.upkeep(me)
  if not me.alive or me.in_town then return false end
  local t = charon.now()
  if pending then
    local present = false
    for _, item in ipairs(game.items()) do
      if item.id == pending.id then present = true; break end
    end
    if not present or fraction(me) > RUN_ABOVE then
      print('stamina: potion acknowledged')
      pending, retry_at = nil, t + REFILL_MS
    elseif t >= retry_at then
      print('stamina: potion not acknowledged; retry allowed')
      pending = nil
    end
  end
  if pending or t < retry_at or fraction(me) >= DRINK_BELOW or not act.ready() then return false end
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.code == 'vps' then
      pending, retry_at = { id = item.id }, t + RETRY_MS
      print('stamina: use stamina potion')
      act.use_item(item)
      return true
    end
  end
  return false
end

-- True when the next move should be a walk.
function stamina.walk(me)
  local f = fraction(me)
  if walking and f > RUN_ABOVE then walking = false end
  if not walking and f < WALK_BELOW then walking = true end
  return walking
end

return stamina
