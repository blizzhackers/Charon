-- Follows map waypoints one tick at a time.
--
--   local move = require 'lib.move'
--   local mover = move.new()
--   mover:to(x, y)          -- or mover:to_level(levelId)
--   function tick() local status = mover:tick() end  -- 'moving', 'arrived', 'failed'
--
-- Each waypoint is requested with act.move, which runs the client's own short
-- range path finding with local prediction. Closed doors on the way are opened.
-- A waypoint that makes no progress is retried, then the whole route is planned
-- again from the current position.

local move = {}
local stamina = require 'lib.stamina'

move.__index = move

local ARRIVE_DISTANCE = 2.5
local STUCK_MS = 1500
local REPLANS = 4
local DOOR_SEARCH = 40   -- subtiles: closed doors considered when no path exists
local DOOR_REACH = 5     -- open a door from this close
local DOOR_TIMEOUT = 20000

local function distance(ax, ay, bx, by)
  return math.sqrt((ax - bx) ^ 2 + (ay - by) ^ 2)
end
move.distance = distance

-- Whether (x, y) can actually be walked up to: the planned path must end
-- within `within` of it. map.path settles for the nearest walkable cell when
-- the goal is out of reach (inside a barred cell, across a gap), which would
-- otherwise look like arrival.
function move.reachable(x, y, within)
  local me = game.me()
  local ok, path = pcall(map.path, x, y)
  if not ok or not path then return false end
  local last = path[#path] or me
  return distance(last.x, last.y, x, y) <= (within or 2.5)
end

-- A closed door within reach that lies toward `point`, or nil.
function move.closed_door_ahead(me, point)
  for _, object in ipairs(game.units('object')) do
    if object.door and object.mode == 0 and object.x then
      local d = distance(me.x, me.y, object.x, object.y)
      if d < 6 and distance(object.x, object.y, point.x, point.y) < distance(me.x, me.y, point.x, point.y) + 2 then
        return object
      end
    end
  end
end

function move.new(options)
  return setmetatable({
    step = options and options.step or 15,
    status = 'idle',
    shared_doors = options and options.tried_doors, -- doors a parent mover already tried
  }, move)
end

function move:plan()
  local path, why
  if self.level then
    path, why = map.path_to_level(self.level, { step = self.step })
  else
    path, why = map.path(self.x, self.y, { step = self.step })
  end
  self.path, self.index, self.issued = path, 1, nil
  self.progress_at, self.best = charon.now(), math.huge
  self.door = nil
  if not path then
    -- No way through: a closed door may be what blocks it. Take the one that
    -- costs the least detour (to it, then on to the goal), walk up to it, open
    -- it and plan again once it has opened (see tick).
    local me = game.me()
    local best, best_cost
    for _, object in ipairs(game.units('object')) do
      if object.door and object.mode == 0 and object.x and not (self.tried_doors or {})[object.id] then
        local reach = distance(me.x, me.y, object.x, object.y)
        if reach < DOOR_SEARCH then
          local cost = reach + (self.x and distance(object.x, object.y, self.x, self.y) or 0)
          if not best_cost or cost < best_cost then best, best_cost = object, cost end
        end
      end
    end
    if best then
      self.door, self.door_at, self.door_since = best, 0, charon.now()
      self.tried_doors = self.tried_doors or {}
      self.tried_doors[best.id] = true
      self.door_mover = nil
      self.status = 'moving'
      return self.status
    end
    self.status, self.reason = 'failed', why
  elseif #path == 0 then
    self.status = 'arrived'
  else
    self.status = 'moving'
  end
  return self.status
end

function move:to(x, y)
  self.x, self.y, self.level = x, y, nil
  self.replans, self.tried_doors = 0, self.shared_doors
  return self:plan()
end

function move:to_level(level)
  self.x, self.y, self.level = nil, nil, level
  self.replans, self.tried_doors = 0, self.shared_doors
  return self:plan()
end

function move:stop()
  self.status, self.path = 'idle', nil
end

function move:tick()
  if self.status ~= 'moving' then return self.status end
  local me = game.me()
  if self.door then
    local door = game.unit('object', self.door.id)
    if not door or door.mode ~= 0 then
      -- Opened (or gone): its collision is cleared live; plan again.
      map.invalidate()
      return self:plan()
    end
    if charon.now() - self.door_since > DOOR_TIMEOUT then
      -- Not this one: try the next door, if any.
      self.door = nil
      return self:plan()
    end
    if distance(me.x, me.y, door.x, door.y) > DOOR_REACH then
      -- Walk up to it first; clicking from afar leaves it to the client's
      -- short-range path finding.
      if not self.door_mover then
        -- Shares our tried doors: no door hunting in circles.
        self.door_mover = move.new({ step = self.step, tried_doors = self.tried_doors })
        self.door_mover:to(door.x, door.y)
      end
      if self.door_mover:tick() ~= 'failed' then return self.status end
    end
    if charon.now() - self.door_at > 1200 then
      act.interact(door)
      self.door_at = charon.now()
    end
    return self.status
  end
  if self.level and me.area == self.level then
    self.status = 'arrived'
    return self.status
  end
  -- Out of stamina: stand still to recover; it is not being stuck.
  if stamina.must_rest(me) then
    self.progress_at = charon.now()
    return self.status
  end
  local point = self.path[self.index]
  local d = distance(me.x, me.y, point.x, point.y)
  if d <= ARRIVE_DISTANCE then
    self.index = self.index + 1
    self.best, self.progress_at = math.huge, charon.now()
    if self.index > #self.path then
      -- Level routes end just inside the border; arrival is the area change.
      self.status = self.level and 'moving' or 'arrived'
      if self.level then return self:plan() end
      return self.status
    end
    point = self.path[self.index]
    d = distance(me.x, me.y, point.x, point.y)
  end
  if d < self.best - 0.5 then
    self.best, self.progress_at = d, charon.now()
  elseif charon.now() - self.progress_at > STUCK_MS then
    self.replans = self.replans + 1
    if self.replans > REPLANS then
      self.status, self.reason = 'failed', 'stuck'
      return self.status
    end
    map.invalidate() -- doors and objects may have changed
    return self:plan()
  end
  -- A closed door between us and the waypoint: open it (the client walks up to
  -- it) and wait; waiting at a door is not being stuck.
  local door = move.closed_door_ahead(me, point)
  if door then
    if charon.now() - (self.door_at or 0) > 1200 then
      act.interact(door)
      self.door_at = charon.now()
    end
    self.progress_at = charon.now()
    return self.status
  end
  -- Issue each waypoint once; repeat only if the player stopped or a second passed.
  local now = charon.now()
  local idle = me.mode == 1 or me.mode == 5
  local due = self.issued ~= self.index or (idle and now - self.issued_at > 250) or now - self.issued_at > 1000
  if due and act.ready() then
    act.move(point.x, point.y, stamina.walk(me))
    self.issued, self.issued_at = self.index, now
  end
  return self.status
end

return move
