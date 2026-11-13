-- Travels between levels of the current act, one tick at a time.
--
--   local travel = require 'lib.travel'
--   local trip = travel.new()
--   trip:to(37)                      -- Catacombs Level 4
--   function tick() local status = trip:tick() end  -- 'moving', 'arrived', 'failed'
--
-- Routes come from map.exits: open borders are crossed by walking, stairs and
-- entrances ('warp' exits) by walking to them and using the warp unit. The
-- caller can stop ticking to fight and resume later; the trip replans from
-- wherever the player is.

local move = require 'lib.move'
local stamina = require 'lib.stamina'

local travel = {}
travel.__index = travel

local USE_RANGE = 4         -- interact with a warp from this close
local USE_RETRY_MS = 1200
local USE_ATTEMPTS = 6
local CROSS_LEVEL_MS = 5000 -- let a moving border route finish a brief third-area detour
local WARP_DETOUR_MS = 20000 -- a route to a stair may cut a corner of a neighbouring level
local JUMP = 30             -- subtiles: farther than one tick walks, so a stair or portal moved us

-- Breadth-first route of exits from level `from` to level `to`, or nil.
function travel.route(from, to)
  if from == to then return {} end
  local previous, queue, head = { [from] = false }, { from }, 1
  while queue[head] do
    local level = queue[head]
    head = head + 1
    local ok, exits = pcall(map.exits, level)
    if ok then
      for _, exit in ipairs(exits) do
        if previous[exit.level] == nil then
          previous[exit.level] = { from = level, exit = exit }
          if exit.level == to then
            local hops = {}
            local at = to
            while previous[at] do
              table.insert(hops, 1, { from = previous[at].from, to = at, kind = previous[at].exit.kind,
                x = previous[at].exit.x, y = previous[at].exit.y })
              at = previous[at].from
            end
            return hops
          end
          queue[#queue + 1] = exit.level
        end
      end
    end
  end
end

function travel.new(options)
  return setmetatable({ mover = move.new(options), status = 'idle' }, travel)
end

function travel:to(level)
  self.target, self.hop, self.status, self.reason = level, nil, 'moving', nil
  return self.status
end

-- Plans the leg in hand again from here: what it should keep off has changed.
function travel:replan()
  if self.status == 'moving' and self.mover.status == 'moving' then self.mover:plan(true) end
end

function travel:stop()
  self.status = 'idle'
  self.mover:stop()
  self.hop = nil
end

-- Starts the hop out of the current level toward the target.
function travel:begin_hop(area)
  local route = travel.route(area, self.target)
  if not route or #route == 0 then
    self.status, self.reason = 'failed', 'no route from ' .. area .. ' to ' .. self.target
    return
  end
  local hop = route[1]
  self.hop = { from = area, to = hop.to, kind = hop.kind, x = hop.x, y = hop.y, attempts = 0, used_at = 0 }
  if hop.kind == 'warp' then
    self.mover:to(hop.x, hop.y)
  else
    self.mover:to_level(hop.to)
  end
  print(string.format('travel: %s -> %s (%s)', game.level_name(area), game.level_name(hop.to), hop.kind))
end

-- The warp unit standing at an exit; warps are only known once nearby.
local function warp_at(x, y)
  local best, best_distance
  for _, unit in ipairs(game.units('warp')) do
    local d = move.distance(unit.x, unit.y, x, y)
    if d < 12 and (not best_distance or d < best_distance) then best, best_distance = unit, d end
  end
  return best
end

-- A passage dodge must not aim at a waypoint left behind by combat. Query the
-- current hop without changing the mover's deadlines, retries or issued action.
function travel:dodge_heading(me)
  local hop, mover = self.hop, self.mover
  if self.status ~= 'moving' or not hop or hop.from ~= me.area or me.area == self.target
    or mover.status ~= 'moving' or mover.door
    or (hop.kind == 'warp' and move.distance(me.x, me.y, hop.x, hop.y) <= USE_RANGE + 2) then return end
  local ok, path
  if hop.kind == 'warp' then
    ok, path = pcall(map.path, hop.x, hop.y, {step=mover.route_step or mover.step})
  else
    ok, path = pcall(map.path_to_level, hop.to, {step=mover.route_step or mover.step})
  end
  if not ok or not path then return end
  for _, point in ipairs(path) do
    if point.x ~= math.floor(me.x) or point.y ~= math.floor(me.y) then
      if move.closed_door_ahead(me, point) then return end
      return {x=point.x+0.5-me.x, y=point.y+0.5-me.y}
    end
  end
end

function travel:tick()
  if self.status ~= 'moving' then return self.status end
  local me = game.me()
  local hop = self.hop
  -- Walking over an open border is continuous; a stair or portal is a jump.
  local last = self.last
  local jumped = last and last.area ~= me.area and move.distance(me.x, me.y, last.x, last.y) > JUMP
  self.last = { x = me.x, y = me.y, area = me.area }
  if hop and (me.area == hop.from or me.area == hop.to) then hop.cross_since = nil end
  if me.area == self.target then
    self.status = 'arrived'
    self.mover:stop()
    return self.status
  end
  if hop and me.area ~= hop.from and me.area ~= hop.to then
    -- The planned path left the hop's level for a neighbour (an open border
    -- beside the route). Keep following it: planning again from each side sent
    -- the player back and forth across the border indefinitely. A jump put us
    -- somewhere the old path does not lead, so that plans again at once.
    hop.cross_since = hop.cross_since or charon.now()
    local allowed = hop.kind == 'warp' and WARP_DETOUR_MS or CROSS_LEVEL_MS
    if not jumped and charon.now() - hop.cross_since < allowed
      and self.mover.status == 'moving' and self.mover:tick() == 'moving' then return self.status end
    -- A failed replan or expired detour releases ownership to the actual area.
    self:begin_hop(me.area)
    return self.status
  end
  if not hop or hop.from ~= me.area then
    -- New level, or a fight moved us: plan the next hop from here.
    self:begin_hop(me.area)
    return self.status
  end
  if hop.kind ~= 'warp' then
    local status = self.mover:tick()
    if status == 'failed' or status == 'idle' then
      -- Replan a few times; monsters or doors can block a border briefly.
      hop.attempts = hop.attempts + 1
      if hop.attempts > 3 then
        self.status, self.reason = 'failed', 'stuck before ' .. game.level_name(hop.to) .. ': ' .. tostring(self.mover.reason)
      else
        map.invalidate()
        self.mover:to_level(hop.to)
      end
    end
    return self.status
  end
  local d = move.distance(me.x, me.y, hop.x, hop.y)
  if d > USE_RANGE + 2 then
    local status = self.mover:tick()
    if status == 'arrived' or status == 'idle' then self.mover:to(hop.x, hop.y) end
    if status == 'failed' then
      self.status, self.reason = 'failed', 'cannot reach the way to ' .. game.level_name(hop.to)
      print(string.format('travel: way to %s at (%d,%d) is %.1f away from (%.1f,%.1f): %s', game.level_name(hop.to),
        hop.x, hop.y, d, me.x, me.y, tostring(self.mover.reason)))
    end
    return self.status
  end
  local warp = warp_at(hop.x, hop.y)
  if not warp then
    self.status, self.reason = 'failed', 'no warp unit near the exit to ' .. game.level_name(hop.to)
    return self.status
  end
  if move.distance(me.x, me.y, warp.x, warp.y) > USE_RANGE then
    if charon.now() - hop.used_at > 600 and act.ready() then
      act.move(math.floor(warp.x), math.floor(warp.y), stamina.walk(me))
      hop.used_at = charon.now()
    end
  elseif charon.now() - hop.used_at > USE_RETRY_MS and act.ready() then
    hop.attempts = hop.attempts + 1
    if hop.attempts > USE_ATTEMPTS then
      self.status, self.reason = 'failed', 'warp to ' .. game.level_name(hop.to) .. ' does not respond'
      return self.status
    end
    act.interact(warp)
    hop.used_at = charon.now()
  end
  return self.status
end

return travel
