-- Explores (clears) a level along a pre-planned sweep.
--
--   local explorer = require('lib.explore').new(levelId)   -- (levelId, true) re-sweeps
--   function tick() local status = explorer:tick(game.me()) end  -- 'moving' or 'done'
--
-- On arrival in the level it plans once (map.explore_plan): reachable ground is
-- split into blocks small enough to see whole, and the blocks are ordered into
-- a walking order (depth-first, dead ends first), so the sweep moves through
-- the level without doubling back. Planning is linear in the level size (a few
-- milliseconds even for the largest outdoor areas). Each tick marks what the player can see
-- (map.explore_mark, walls block sight) and tour points already seen are
-- skipped. Pockets the tour missed are then picked up by frontier search
-- (map.explore_next: nearest unseen ground by walking distance). Done when
-- nothing reachable is left unseen. Normal travel does not use this.

local move = require 'lib.move'

local explore = {}
explore.__index = explore

local SIGHT = 14
local CELL = 12

function explore.new(level, reset)
  if reset then map.explore_reset(level) end
  return setmetatable({ level = level, mover = move.new() }, explore)
end

-- Walkable cells still unseen and the level's total (known once the frontier phase runs).
function explore:remaining()
  return self.unseen, self.total
end

-- Next point to walk to: the next unseen tour point, else the nearest frontier.
function explore:next_target()
  if not self.tour then
    local ms
    self.tour, ms = map.explore_plan(CELL)
    self.tour = self.tour or {}
    self.index = 1
    print(string.format('explore: %s sweep of %d points planned in %.0f ms',
      game.level_name(self.level), #self.tour, ms or 0))
  end
  while self.index <= #self.tour do
    local point = self.tour[self.index]
    self.index = self.index + 1
    if not map.explore_seen(point.x, point.y) then return point end
  end
  local next = map.explore_next()
  if next then self.unseen, self.total = next.unseen, next.total end
  return next
end

function explore:tick(me)
  if me.area ~= self.level then return 'moving' end
  -- Re-mark only after moving a little: sight checks are the per-tick cost.
  local last = self.marked
  if not last or (me.x - last.x) ^ 2 + (me.y - last.y) ^ 2 >= 4 then
    map.explore_mark(me.x, me.y, SIGHT)
    self.marked = { x = me.x, y = me.y }
  end
  local target = self.target
  if target and map.explore_seen(target.x, target.y) then target = nil end
  if not target then
    target = self:next_target()
    if not target then return 'done' end
    self.target = target
    if self.mover:to(target.x, target.y) == 'failed' then
      -- Cannot get there after all: count it as seen and pick another next tick.
      map.explore_mark(target.x, target.y, 3)
      self.target = nil
    end
    return 'moving'
  end
  local status = self.mover:tick()
  if status == 'failed' then
    map.explore_mark(target.x, target.y, 3)
    self.target = nil
  elseif status ~= 'moving' then
    -- Arrived (or idle after a fight): look from here and choose again.
    self.target = nil
  end
  return 'moving'
end

return explore
