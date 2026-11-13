-- Follows map waypoints one tick at a time.
--
--   local move = require 'lib.move'
--   local mover = move.new()
--   mover:to(x, y)          -- or mover:to_level(levelId)
--   function tick() local status = mover:tick() end  -- 'moving', 'arrived', 'failed'
--
-- move.new{avoid = function() return {{x=, y=, radius=}, ...} end} plans
-- around that ground when a way round exists that is not much longer
-- (map.path's avoid option); mover.detour tells whether the route in hand
-- does, and mover.through whether zones were given that it could not avoid.
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
local REFUSAL_MS = 300 -- let local traffic change without flooding native path requests
local MIN_STEP = 3     -- short native requests can recover where a long waypoint is refused
local DOOR_SEARCH = 40   -- subtiles: closed doors considered when no path exists
local DOOR_REACH = 5     -- open a door from this close
local DOOR_TIMEOUT = 20000
-- What breaks and stands in her way is broken (the maintainer's rule): a
-- barrel, an urn or a casket on the next leg of the route, before the step
-- is refused, and whatever of the kind stands beside a step that was. Two
-- barrels in a cellar doorway refused every step until the route gave up,
-- four times, and the game was left for a new one.
local BREAK_REACH = 3.5  -- how near a refused step's obstacle is looked for
local BREAK_AHEAD = 7    -- how far along the next leg a breakable is gone for
local BREAK_BESIDE = 2.5 -- and how far to the side of it: her own width and the object's
local BREAK_MS = 600     -- time for the blow to land before the step is tried again
local BREAKS = 12        -- per route: more than that is a storeroom, not a way
-- Objects.txt OperateFn of what breaks when struck (see lib/containers.lua).
local BREAKABLE = { [18] = true, [29] = true, [51] = true }

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
      local dx, dy = point.x - me.x, point.y - me.y
      local length = dx * dx + dy * dy
      local projection = length > 0 and ((object.x - me.x) * dx + (object.y - me.y) * dy) / length or -1
      local sideways = length > 0 and math.abs((object.x - me.x) * dy - (object.y - me.y) * dx) / math.sqrt(length) or math.huge
      -- A route can cross a wide door away from its center; require a blocked
      -- segment before considering those extra subtiles beside the route.
      if d < 6 and projection > 0 and projection <= 1
        and (sideways <= 2 or (sideways <= 4 and not map.line_of_sight(me.x, me.y, point.x, point.y))) then
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
    avoid = options and options.avoid,
  }, move)
end

function move:plan(keep_progress)
  local me = game.me()
  local path, why
  local zones = self.avoid and self.avoid() or nil
  if zones and #zones == 0 then zones = nil end
  if self.level then
    path, why = map.path_to_level(self.level, { step = self.route_step or self.step, avoid = zones })
  else
    path, why = map.path(self.x, self.y, { step = self.route_step or self.step, avoid = zones })
  end
  self.detour = path ~= nil and why == true
  self.through = path ~= nil and zones ~= nil and why ~= true
  self.path, self.index, self.issued = path, 1, nil
  if not keep_progress then self.progress_at = charon.now() end
  self.best = path and path[1]
    and distance(me.x, me.y, path[1].x, path[1].y) or math.huge
  self.tick_frame = charon.frame and charon.frame()
  self.tick_x, self.tick_y = me.x, me.y
  self.door = nil
  if not path then
    -- No way through: a closed door may be what blocks it. Take the one that
    -- costs the least detour (to it, then on to the goal), walk up to it, open
    -- it and plan again once it has opened (see tick).
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

-- An unbroken barrel, urn or the like on the way from her to `point`: within
-- BREAK_AHEAD of her, and no farther than BREAK_BESIDE from the line. Or nil.
function move.breakable_on_way(me, point)
  local dx, dy = point.x - me.x, point.y - me.y
  local length = math.sqrt(dx * dx + dy * dy)
  if length < 0.1 then return nil end
  local best, best_along
  for _, object in ipairs(game.units('object')) do
    if object.x and object.mode == 0 and BREAKABLE[object.operate] then
      local ox, oy = object.x - me.x, object.y - me.y
      local along = (ox * dx + oy * dy) / length
      local beside = math.abs(ox * dy - oy * dx) / length
      if along >= 0 and along <= math.min(BREAK_AHEAD, length + BREAK_BESIDE) and beside <= BREAK_BESIDE
        and (not best_along or along < best_along) then
        best, best_along = object, along
      end
    end
  end
  return best
end

-- An unbroken barrel, urn or the like within reach on the side of `point`, or nil.
function move.breakable_ahead(me, point)
  local best, best_distance
  for _, object in ipairs(game.units('object')) do
    if object.x and object.mode == 0 and BREAKABLE[object.operate] then
      local d = distance(me.x, me.y, object.x, object.y)
      local toward = (object.x - me.x) * (point.x - me.x) + (object.y - me.y) * (point.y - me.y) >= 0
      if d <= BREAK_REACH and toward and (not best_distance or d < best_distance) then
        best, best_distance = object, d
      end
    end
  end
  return best
end

function move:to(x, y)
  self.x, self.y, self.level = x, y, nil
  self.breaks = 0
  self.replans, self.tried_doors = 0, self.shared_doors
  self.route_step, self.retry_at = self.step, nil
  return self:plan()
end

function move:to_level(level)
  self.x, self.y, self.level = nil, nil, level
  self.breaks = 0
  self.replans, self.tried_doors = 0, self.shared_doors
  self.route_step, self.retry_at = self.step, nil
  return self:plan()
end

function move:stop()
  self.status, self.path = 'idle', nil
end

function move:tick()
  if self.status ~= 'moving' then return self.status end
  local me = game.me()
  local frame = charon.frame and charon.frame()
  local resumed = frame and self.tick_frame and frame > self.tick_frame + 1
  local displaced = resumed and distance(me.x, me.y, self.tick_x, self.tick_y) > ARRIVE_DISTANCE
  self.tick_frame, self.tick_x, self.tick_y = frame, me.x, me.y
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
  if resumed then
    -- Combat/loot owns skipped frames; its movement can leave our old waypoints behind.
    self.issued = nil -- replace another owner's command even when displacement was small
    if displaced then
      print(string.format('move: resuming route from %.0f,%.0f after interruption', me.x, me.y))
      return self:plan()
    end
  end
  local point = self.path[self.index]
  local d = distance(me.x, me.y, point.x, point.y)
  -- A nearby corner can still hide the next segment. Smooth ordinary spacing
  -- points only when movement clearance proves the onward segment is open.
  local reached = math.floor(me.x) == point.x and math.floor(me.y) == point.y
  local next_point = self.path[self.index + 1]
  if reached or (d <= ARRIVE_DISTANCE and (not next_point or (map.line_clear
    and map.line_clear(me.x, me.y, next_point.x, next_point.y)))) then
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
  elseif act.ready() and charon.now() - self.progress_at > STUCK_MS then
    self.replans = self.replans + 1
    print(string.format('move: no progress at (%.1f,%.1f) toward (%d,%d), %.1f away, mode %s, route %d/%d, replan %d/%d, step %d',
      me.x, me.y, point.x, point.y, d, tostring(me.mode), self.index, #self.path, self.replans, REPLANS,
      self.route_step or self.step))
    if self.replans > REPLANS then
      self.status, self.reason = 'failed', 'stuck'
      return self.status
    end
    map.invalidate() -- doors and objects may have changed
    return self:plan()
  end
  -- Door requests do not count as movement progress: refusal must reach the
  -- same finite replan limit as any other obstruction.
  local door = move.closed_door_ahead(me, point)
  if door then
    if charon.now() - (self.door_at or 0) > 1200 then
      act.interact(door)
      self.door_at = charon.now()
    end
    return self.status
  end
  -- Issue each waypoint once; repeat only if the player stopped or a second passed.
  local now = charon.now()
  local idle = me.mode == 1 or me.mode == 5
  -- What breaks and stands on this leg is broken on the way, not run into.
  if (self.breaks or 0) < BREAKS and now >= (self.retry_at or 0) and act.ready() then
    local blocker = move.breakable_on_way(me, point)
    if blocker then
      self.breaks = (self.breaks or 0) + 1
      print(string.format('move: %s on the way to (%d,%d); breaking it', tostring(blocker.name or blocker.id), point.x, point.y))
      act.interact(blocker)
      self.issued, self.retry_at = nil, now + BREAK_MS
      self.progress_at = now -- the blow is progress: it is not what the stuck rule is for
      return self.status
    end
  end
  -- The same waypoint again is the same packet again, which the client drops
  -- within 200 ms (see lib/services.lua): repeats wait longer than that.
  local due = self.issued ~= self.index or (idle and now - self.issued_at > 250)
    or now - self.issued_at > 1000
  if due and now >= (self.retry_at or 0) and act.ready() then
    if act.move(point.x, point.y, stamina.walk(me)) == false then
      local blocker = (self.breaks or 0) < BREAKS and move.breakable_ahead(me, point)
      if blocker then
        self.breaks = (self.breaks or 0) + 1
        print(string.format('move: step to (%d,%d) refused beside %s; breaking it', point.x, point.y,
          tostring(blocker.name or blocker.id)))
        act.interact(blocker)
        self.issued, self.retry_at = nil, now + BREAK_MS
        return self.status
      end
      self.replans = self.replans + 1
      print(string.format('move: refused at (%.2f,%.2f) to (%d,%d), mode %s, route %d/%d, replan %d/%d, step %d',
        me.x, me.y, point.x, point.y, tostring(me.mode), self.index, #self.path,
        self.replans, REPLANS, self.route_step or self.step))
      -- What stands there, the first time: the log is where a blocker gets its name.
      if self.replans == 1 then
        for _, object in ipairs(game.units('object')) do
          if object.x and distance(me.x, me.y, object.x, object.y) <= 8 then
            print(string.format('move:   beside %s (class %s, operate %s, mode %s) at (%d,%d), %.1f away', tostring(object.name),
              tostring(object.class_id), tostring(object.operate), tostring(object.mode), object.x, object.y,
              distance(me.x, me.y, object.x, object.y)))
          end
        end
      end
      self.issued = nil
      if self.replans > REPLANS then
        self.status, self.reason = 'failed', 'movement refused'
        return self.status
      end
      self.route_step = math.min(self.route_step or self.step,
        math.max(MIN_STEP, math.floor((self.route_step or self.step) / 2)))
      self.retry_at = now + REFUSAL_MS
      map.invalidate()
      return self:plan(true)
    end
    self.issued, self.issued_at = self.index, now
  end
  return self.status
end

return move
