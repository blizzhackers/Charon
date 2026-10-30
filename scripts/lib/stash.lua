-- Keeps items and gold in the stash during a town visit, one step per tick.
--
--   local stash = require('lib.stash').new(function(item) return is_rune(item) end, 2000)
--   if stash:wanted(me) then local status = stash:tick(me) end  -- 'busy' or 'done'
--
-- Gold: a death drops the gold carried, but not the stash's, and vendors take
-- stash gold. Carried gold above `float` is deposited.
--
-- Walks to the stash (Objects.txt operate function 32), opens it, then for
-- each wanted inventory item: lift it onto the cursor, drop it into the first
-- free stash cell it fits. An item the server will not place goes back to the
-- inventory cell it came from, so nothing is left on the cursor.

local move = require 'lib.move'
local world = require 'lib.world'

local stash = {}
stash.__index = stash

local OPEN_STASH = 32    -- Objects.txt OperateFn
local STEP_MS = 400
local TIMEOUT_MS = 40000
local REACH = 4

local GOLD_SLACK = 200   -- do not walk to the stash for less than this

function stash.new(keep, float)
  return setmetatable({ keep = keep, float = float or 0, mover = move.new(), last = 0, full = {} }, stash)
end

-- Gold to move: 'deposit' and the amount, or nil.
function stash:gold_move(me)
  if me.gold > self.float + GOLD_SLACK then return 'deposit', me.gold - self.float end
end

-- Whether a stash visit has anything to do.
function stash:wanted(me)
  return self:pending() ~= nil or self:gold_move(me) ~= nil
end

-- Inventory items to store (page 0 only), largest first so they find room.
function stash:pending()
  local list = {}
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and not self.full[item.id] and self.keep(item) then
      list[#list + 1] = item
    end
  end
  table.sort(list, function(a, b) return (a.width or 1) * (a.height or 1) > (b.width or 1) * (b.height or 1) end)
  return list[1] and list or nil
end

-- First free stash cell for a w x h item, or nil. The stash is 6 x 8 in an
-- expansion game, 6 x 4 in classic.
local function free_cell(w, h)
  local cols, rows = 6, game.me().expansion and 8 or 4
  local used = {}
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 4 and item.grid_x then
      for dy = 0, (item.height or 1) - 1 do
        for dx = 0, (item.width or 1) - 1 do used[(item.grid_y + dy) * cols + item.grid_x + dx] = true end
      end
    end
  end
  for y = 0, rows - h do
    for x = 0, cols - w do
      local fits = true
      for dy = 0, h - 1 do
        for dx = 0, w - 1 do
          if used[(y + dy) * cols + x + dx] then fits = false end
        end
      end
      if fits then return x, y end
    end
  end
end

local function stash_object(me)
  return world.nearest(game.units('object'), me, function(o) return o.operate == OPEN_STASH end)
end

local function stash_spawn()
  for _, preset in ipairs(map.presets()) do
    if preset.type == 'object' then
      local info = game.object_info(preset.class_id)
      if info and info.operate == OPEN_STASH then return preset end
    end
  end
end

function stash:tick(me)
  self.started = self.started or charon.now()
  local now = charon.now()
  if now - self.started > TIMEOUT_MS then
    print('stash: timed out')
    return self:finish()
  end
  if now - self.last < STEP_MS then return 'busy' end

  if not game.ui('stash') then
    if not self:wanted(me) then return self:finish() end
    local object = stash_object(me)
    local target = object or stash_spawn()
    if not target then
      print('stash: no stash in this town')
      return self:finish()
    end
    if not object or world.distance(me, object) > REACH then
      if self.mover.status ~= 'moving' then self.mover:to(target.x, target.y) end
      self.mover:tick()
      return 'busy'
    end
    if act.ready() and now - (self.opened_at or 0) > 1500 then
      self.mover:stop()
      act.interact(object)
      self.opened_at = now
    end
    return 'busy'
  end

  self.last = now
  -- Gold first, one transfer at a time; the stat updates when the server answers.
  local way, amount = self:gold_move(me)
  if way and not me.cursor_item and now - (self.gold_at or 0) > 1500 then
    act.transfer_gold(way, amount)
    self.gold_at = now
    print(string.format('stash: %s %d gold', way, amount))
    return 'busy'
  end
  if me.cursor_item then
    local held = self.held
    if not held then return 'busy' end -- wait for the cursor to report what it holds
    local x, y = free_cell(held.width or 1, held.height or 1)
    self.tries = (self.tries or 0) + 1
    if x and self.tries <= 2 then
      act.place_item(held, 'stash', x, y)
      print(string.format('stash: %s -> stash (%d, %d)', tostring(held.name), x, y))
    else
      -- No room (or refused): back where it came from, and leave it be.
      self.full[held.id] = true
      act.place_item(held, 'inventory', held.grid_x or 0, held.grid_y or 0)
      print('stash: no room for ' .. tostring(held.name))
    end
    return 'busy'
  end
  local item = (self:pending() or {})[1]
  if not item then return self:finish() end
  self.held, self.tries = item, 0
  act.pick_item(item)
  return 'busy'
end

function stash:finish()
  if game.ui('stash') then act.key(27) end
  self.started, self.held = nil, nil
  self.mover:stop()
  return 'done'
end

return stash
