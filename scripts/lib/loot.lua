-- Picks up useful ground items near the player, one tick at a time.
--
--   local loot = require('lib.loot').new()
--   if loot:nearby(me) then loot:tick(me) end   -- returns 'looting' or 'done'
--
-- By default it takes gold, potions and runes, plus items worth selling while the
-- inventory has room: jewelry and charms, staves, wands and scepters of any
-- quality (their class-skill mods make vendors pay far more than their size
-- suggests), and small magic-or-better gear. Vendors turn those into the gold
-- that buys potions.

local world = require 'lib.world'
local move = require 'lib.move'

local loot = {}
loot.__index = loot

local INVENTORY_CELLS = 40 -- 10 x 4
local KEEP_FREE = 4        -- leave room for potions bought later

loot.POTIONS_AND_GOLD = {
  gld = true, hp1 = true, hp2 = true, hp3 = true, hp4 = true, hp5 = true,
  mp1 = true, mp2 = true, mp3 = true, mp4 = true, mp5 = true, rvs = true, rvl = true,
}
loot.JEWELRY = { rin = true, amu = true, jew = true, cm1 = true, cm2 = true, cm3 = true }

-- Inventory cells in use (page 0 only).
function loot.used_cells()
  local used = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 then used = used + (item.width or 1) * (item.height or 1) end
  end
  return used
end

-- Worth picking up to sell: jewelry; any item whose type rolls class-skill
-- mods without being class-only (staves, wands, scepters: ItemTypes.txt
-- StaffMods), whatever its quality, since even plain or ethereal ones sell
-- well when they roll skills; or magic-or-better gear of at most four cells.
function loot.sellable(item)
  if loot.JEWELRY[item.code] or (item.staff_mods and not item.class_only) then return true end
  return (item.quality or 0) >= 4 and (item.width or 9) * (item.height or 9) <= 4
end

function loot.new(radius)
  return setmetatable({ radius = radius or 12, ignored = {}, last = 0, mover = move.new() }, loot)
end

-- Runes (r01 El .. r33 Zod): one cell each, kept for runewords, not sold.
function loot.rune(item)
  return item.code ~= nil and item.code:match('^r%d%d$') ~= nil
end

function loot:wanted(item, free)
  if loot.POTIONS_AND_GOLD[item.code] then return true end
  if loot.rune(item) then return free >= 1 end
  return loot.sellable(item) and (item.width or 1) * (item.height or 1) <= free - KEEP_FREE
end

function loot:nearby(me)
  local free
  return world.nearest(game.units('item'), me, function(item)
    if item.location ~= 'ground' or (self.ignored[item.id] or 0) >= charon.now() then return false end
    if world.distance(me, item) > self.radius then return false end
    free = free or INVENTORY_CELLS - loot.used_cells()
    return self:wanted(item, free)
  end)
end

-- Walks to the nearest wanted item along a planned path (so items behind
-- bars or walls are reached through the doorway, or given up when there is
-- none) and picks it up.
function loot:tick(me)
  local item, d = self:nearby(me)
  if not item then return 'done' end
  local now = charon.now()
  if self.item ~= item.id then
    self.item, self.since = item.id, now
    self.mover:stop()
    -- Behind bars or a wall: a pickup click would leave the client stuck.
    if not move.reachable(item.x, item.y, 3) then
      self.ignored[item.id] = now + 60000
      return 'looting'
    end
  elseif now - self.since > 12000 then
    self.ignored[item.id] = now + 60000
    self.mover:stop()
    return 'looting'
  end
  -- The path ends beside the item when it is out of reach (behind bars with
  -- no door, across a gap): arriving there still well away means leave it;
  -- just short of it (item on an edge cell) is close enough to click.
  local arrived = self.mover.status == 'arrived' and self.routed == item.id
  if d > 3 and not (arrived and d <= 4.5) then
    if arrived then
      self.ignored[item.id] = now + 60000
      self.mover:stop()
      return 'looting'
    end
    if self.mover.status ~= 'moving' then
      self.routed = item.id
      if self.mover:to(item.x, item.y) == 'failed' then
        self.ignored[item.id] = now + 60000
        return 'looting'
      end
    end
    if self.mover:tick() == 'failed' then self.ignored[item.id] = now + 60000 end
  elseif now - self.last > 500 then
    self.mover:stop()
    act.pickup(item)
    self.last = now
  end
  return 'looting'
end

return loot
