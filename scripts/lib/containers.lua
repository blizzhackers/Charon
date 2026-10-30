-- Opens the chests, urns, barrels, corpses, bookshelves and other containers
-- the player passes, one tick at a time. Whatever falls out is picked up by
-- lib.loot afterwards.
--
--   local containers = require('lib.containers').new(12)
--   if containers:nearby(me) then containers:tick(me) end  -- 'opening' or 'done'
--
-- Containers are recognised by their Objects.txt operate function, not by
-- name; the ones that hurt (exploding barrels, trap objects) and the ones
-- that need a key are left alone.

local move = require 'lib.move'
local world = require 'lib.world'

local containers = {}
containers.__index = containers

-- Objects.txt OperateFn values that open or break into loot.
containers.OPERATE = {
  [1] = true,  -- chest (may release a monster)
  [3] = true,  -- chest
  [4] = true,  -- sparkly chest
  [5] = true,  -- trapped chest (mild in Normal)
  [18] = true, -- destructible
  [19] = true, -- item drop (exceptional)
  [20] = true, -- item drop (corpses, bookshelves, ...)
  [29] = true, -- breakable: urns, pots, barrels, caskets
  [48] = true, -- unlocked chest
  [51] = true, -- timed destructible
}

local GIVE_UP_MS = 8000
local CLICK_MS = 800
local CLICK_RANGE = 4

function containers.new(radius)
  return setmetatable({ radius = radius or 12, tried = {}, last = 0, mover = move.new() }, containers)
end

-- Nearest unopened container within reach, and its distance.
function containers:nearby(me)
  return world.nearest(game.units('object'), me, function(object)
    return object.x and object.mode == 0 and containers.OPERATE[object.operate]
      and not self.tried[object.id] and world.distance(me, object) <= self.radius
  end)
end

function containers:tick(me)
  local object, d = self:nearby(me)
  if not object then return 'done' end
  local now = charon.now()
  if self.object ~= object.id then
    self.object, self.since, self.clicks, self.routed = object.id, now, 0, false
    self.mover:stop()
    -- Behind bars or a wall: clicking it anyway leaves the client stuck
    -- trying to operate what it cannot reach.
    if not move.reachable(object.x, object.y, 3) then
      self.tried[object.id] = true
      return 'opening'
    end
  end
  if now - self.since > GIVE_UP_MS or self.clicks >= 3 then
    -- Out of reach or not responding: leave it.
    self.tried[object.id] = true
    self.mover:stop()
    return 'opening'
  end
  if d > CLICK_RANGE then
    if self.mover.status == 'arrived' and self.routed then
      -- The path ends short of it: behind bars or a wall with no way round.
      self.tried[object.id] = true
      return 'opening'
    end
    if self.mover.status ~= 'moving' then
      self.routed = true
      if self.mover:to(object.x, object.y) == 'failed' then
        self.tried[object.id] = true
        return 'opening'
      end
    end
    if self.mover:tick() == 'failed' then self.tried[object.id] = true end
  elseif act.ready() and now - self.last > CLICK_MS then
    self.mover:stop()
    if self.clicks == 0 then print('containers: opening ' .. tostring(object.code)) end
    act.interact(object)
    self.last, self.clicks = now, self.clicks + 1
  end
  return 'opening'
end

return containers
