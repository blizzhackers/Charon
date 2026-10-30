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

function travel:tick()
  if self.status ~= 'moving' then return self.status end
  local me = game.me()
  if me.area == self.target then
    self.status = 'arrived'
    self.mover:stop()
    return self.status
  end
  if not self.hop or self.hop.from ~= me.area then
    -- New level, or a fight moved us: plan the next hop from here.
    self:begin_hop(me.area)
    return self.status
  end
  local hop = self.hop
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
    if status == 'failed' then self.status, self.reason = 'failed', 'cannot reach the way to ' .. game.level_name(hop.to) end
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
