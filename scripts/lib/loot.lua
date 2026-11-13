-- Picks up useful ground items near the player, one tick at a time.
--
--   local loot = require('lib.loot').new()
--   loot:grab(me) -- already in reach; does not interrupt the current activity
--   if loot:nearby(me) then loot:tick(me) end   -- returns 'looting' or 'done'
--
-- Takes supplies and runes, plus client-quoted vendor loot worth at least two
-- mana potions and one potion per inventory cell. Known early vendor items
-- remain the fallback when a quote is unavailable.

local world = require 'lib.world'
local move = require 'lib.move'
local stamina = require 'lib.stamina'

local loot = {}
loot.__index = loot

local INVENTORY_CELLS = 40 -- 10 x 4
local KEEP_FREE = 4        -- leave room for potions bought later
local MIN_SALE, GOLD_PER_CELL = 120, 60 -- at least two mana potions total, one per occupied cell
local OPENING_SALE, OPENING_PER_CELL = 60, 20 -- modest drops can fund the first mana reserve
local VALUABLE_SALE = 600  -- ten minor mana potions justify a wider pickup radius
local QUOTE_MS = 1000      -- stacks and quest discounts can change while an item stays loaded
local GRAB_RANGE = 3       -- margin inside the native pickup distance while the player moves
local PICKUP_MS = 500      -- share pacing across normal, gather and opportunistic pickups
local RETRY_MS = 1500      -- let another drop win while a request is pending or refused
local APPROACH_MS = 160    -- short safe approaches use the movement driver's normal cadence
local APPROACH_STEP = 2.5  -- commit only half a tile before the next threat check
local MAX_ATTEMPTS = 3    -- unacknowledged requests must eventually yield to other drops
local IGNORE_MS = 60000   -- retry a failed identity after the client has had time to recover
local MAX_PENDING = 64    -- bound tracking even when loaded drops keep changing
local last_pickup, retry_after = 0, {}
local observed_frame, observed_gold

local function carried(item)
  return item and (item.location == 'belt' or item.location == 'equipped'
    or (item.location == 'container' and item.page == 0))
end

local function pickup_log(event, id, request, me)
  local distance = me and me.x and me.y and world.distance(me, request) or -1
  print(string.format('loot: %s id=%s code=%s ground=(%s,%s) player=(%s,%s) distance=%.1f mode=%s attempt=%d elapsed=%d',
    event, tostring(id), tostring(request.code), tostring(request.x), tostring(request.y),
    tostring(me and me.x), tostring(me and me.y), distance, tostring(me and me.mode),
    request.attempts, charon.now()-request.at))
end

-- An item leaving the ground snapshot is not a receipt. Check fresh client ownership.
local function observe_pickups(me)
  local frame = charon.frame and charon.frame() or charon.now()
  if observed_frame == frame then return end
  observed_frame = frame
  local now, inventory, gold_pending = charon.now(), nil, 0
  for id, request in pairs(retry_after) do
    if now >= request.expires then
      retry_after[id] = nil
    elseif request.pending then
      inventory = inventory or game.items()
      local owned
      for _, item in ipairs(inventory) do
        if item.id == id then owned = item; break end
      end
      if carried(owned) and owned.code == request.code
        and (not request.class_id or owned.class_id == request.class_id) then
        pickup_log('ack carried', id, request, me)
        retry_after[id] = nil
      else
        if request.code == 'gld' and not request.suppressed then gold_pending = gold_pending + 1 end
        if now >= request.after and not request.timed_out then
          request.timed_out = true
          if request.attempts >= MAX_ATTEMPTS then
            request.suppressed, request.expires = true, now + IGNORE_MS
            pickup_log('timeout suppressed', id, request, me)
          else
            pickup_log('unconfirmed retry eligible', id, request, me)
          end
        end
      end
    end
  end
  -- Spending, other piles and overlapping requests prevent assigning this delta to an ID.
  if me and me.gold then
    if observed_gold and me.gold > observed_gold and gold_pending > 0 then
      print(string.format('loot: gold delta=%d pending=%d (balance increase; item ids unconfirmed)',
        me.gold-observed_gold, gold_pending))
    end
    observed_gold = me.gold
  end
end

local function tracking_slot()
  local count = 0
  for _ in pairs(retry_after) do count = count + 1 end
  if count < MAX_PENDING then return true end
  -- Unconfirmed history for unloaded drops must not disable new pickups.
  local ground, oldest, oldest_at = {}, nil, math.huge
  for _, item in ipairs(game.units('item')) do
    if item.location == 'ground' then ground[item.id] = true end
  end
  for id, request in pairs(retry_after) do
    if not ground[id] and charon.now() >= request.after and request.at < oldest_at then
      oldest, oldest_at = id, request.at
    end
  end
  if oldest then retry_after[oldest] = nil; return true end
  return false
end
local POTION_CAPS = { hp = 4, mp = 10, rv = 4 } -- keep campaign reserves without crowding vendor loot
local quotes = {}          -- game-local item identity -> client sale quote
loot.SUPPLIES = { vps = 4, yps = 2 } -- useful travel and boss stock, kept in inventory

loot.POTIONS_AND_GOLD = {
  gld = true, hp1 = true, hp2 = true, hp3 = true, hp4 = true, hp5 = true,
  mp1 = true, mp2 = true, mp3 = true, mp4 = true, mp5 = true, rvs = true, rvl = true,
}
loot.JEWELRY = { rin = true, amu = true, jew = true, cm1 = true, cm2 = true, cm3 = true }
local VENDOR_WEAPONS = { jav = true, tkf = true } -- plain stacks fund early mana purchases

-- Briefly cache loaded ground/carried quotes; identification changes value.
function loot.sell_value(item)
  if not item.id or not town or not town.sell_value then return nil end
  local cached = quotes[item.id]
  if cached and charon.now()-cached.at < QUOTE_MS and cached.class_id == item.class_id
    and cached.identified == item.identified then return cached.value end
  local value = town.sell_value(item)
  if value then quotes[item.id] = {class_id=item.class_id,identified=item.identified,value=value,at=charon.now()} end
  return value
end

-- Inventory cells in use (page 0 only).
function loot.used_cells()
  local used = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 then used = used + (item.width or 1) * (item.height or 1) end
  end
  return used
end

function loot.supply_count(code)
  local count = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.code == code then count = count + 1 end
  end
  return count
end

-- Judge value per inventory cell, protecting supplies and rune keepers first.
function loot.sellable(item)
  -- What she is about to put on is not for sale (loot.wear, set by the bot from lib/gear).
  if loot.wear and loot.wear(item) then return false end
  if loot.SUPPLIES[item.code] then return loot.supply_count(item.code) > loot.SUPPLIES[item.code] end
  if loot.POTIONS_AND_GOLD[item.code] or loot.rune(item) or item.code=='tsc' or item.code=='isc' then return false end
  local value = loot.sell_value(item)
  if value then
    local me = game.me and game.me()
    -- Already-carried vendor loot has no pickup detour or extra space cost.
    local held = item.location == 'container' and item.page == 0
    local opening = held or me and me.class == 'sorceress' and (me.level or 99) <= 4
      and (me.gold or 0) + (me.stash_gold or 0) < 300
    if opening and not held then
      local mana = 0
      for _, held in ipairs(game.items()) do
        if carried(held) and world.potion_kind(held.code) == 'mp' then mana = mana + 1 end
      end
      opening = mana < 4
    end
    return value >= math.max(opening and OPENING_SALE or MIN_SALE,
      (opening and OPENING_PER_CELL or GOLD_PER_CELL) * (item.width or 1) * (item.height or 1))
  end
  if loot.JEWELRY[item.code] or VENDOR_WEAPONS[item.code] or (item.staff_mods and not item.class_only) then return true end
  return (item.quality or 0) >= 4 and (item.width or 9) * (item.height or 9) <= 4
end

function loot.new(radius)
  return setmetatable({ radius = radius or 12, ignored = {}, mover = move.new() }, loot)
end

-- Runes (r01 El .. r33 Zod): one cell each, kept for runewords, not sold.
function loot.rune(item)
  return item.code ~= nil and item.code:match('^r%d%d$') ~= nil
end

function loot:wanted(item, free)
  if loot.SUPPLIES[item.code] then
    return free >= (item.width or 1) * (item.height or 1)
      and loot.supply_count(item.code) < loot.SUPPLIES[item.code]
  end
  if item.code == 'tsc' then return free >= 1 and loot.supply_count('tsc') < 1 end
  if item.code == 'gld' then return true end
  if loot.POTIONS_AND_GOLD[item.code] then
    local prefix, count = item.code:sub(1, 2), 0
    for _, carried in ipairs(game.items()) do
      if (carried.location == 'belt' or (carried.location == 'container' and carried.page == 0))
        and loot.POTIONS_AND_GOLD[carried.code] and carried.code:sub(1, 2) == prefix then
        count = count + 1
      end
    end
    return count < POTION_CAPS[prefix] and (free >= 1 or not world.starter_belt_full())
  end
  if loot.rune(item) then return free >= 1 end
  -- What may turn out better than what she wears (loot.candidate, set by the
  -- bot from lib/gear): picked up to be read and weighed, and sold if not.
  if loot.candidate and loot.candidate(item) and (item.width or 1) * (item.height or 1) <= free - KEEP_FREE then
    return true
  end
  return loot.sellable(item) and (item.width or 1) * (item.height or 1) <= free - KEEP_FREE
end

function loot:nearby(me, maximum, accept, ready_only)
  observe_pickups(me)
  local free
  return world.nearest(game.units('item'), me, function(item)
    local request = retry_after[item.id]
    if request and (request.code ~= item.code or (request.class_id and request.class_id ~= item.class_id)) then
      retry_after[item.id], request = nil, nil
    end
    if item.location ~= 'ground' or (self.ignored[item.id] or 0) >= charon.now()
      or (request and (request.suppressed or (ready_only and request.after > charon.now()))) then return false end
    local distance = world.distance(me, item)
    if maximum and distance > maximum then return false end
    if distance > self.radius then
      if not self.valuable_radius or distance > self.valuable_radius or loot.SUPPLIES[item.code] then return false end
      local value = loot.sell_value(item)
      if not value or value < VALUABLE_SALE or not loot.sellable(item)
        or not map.line_of_sight(me.x, me.y, item.x, item.y) then return false end
    end
    free = free or INVENTORY_CELLS - loot.used_cells()
    return self:wanted(item, free) and (not accept or accept(item)) and (request ~= nil or tracking_slot())
  end)
end

function loot:pickup(item, nearby_only)
  local me = game.me and game.me()
  observe_pickups(me)
  local now = charon.now()
  local request = retry_after[item.id]
  if now - last_pickup < PICKUP_MS or (request and (request.suppressed or request.after > now)) then return false end
  if not request then
    if not tracking_slot() then return false end
    request = {attempts=0}
    retry_after[item.id] = request
  end
  request.attempts = request.attempts + 1
  request.at, request.after, request.expires = now, now + RETRY_MS, now + IGNORE_MS
  request.code, request.class_id, request.x, request.y = item.code, item.class_id, item.x, item.y
  request.gold = me and me.gold
  local sent = act.pickup(item, false, nearby_only)
  request.pending, request.timed_out = true, false
  pickup_log(sent == false and 'request refused' or 'request sent', item.id, request, me)
  if sent == false then return false end
  last_pickup = now
  return true
end

-- Pickup packets can accompany a cast or movement; never ask the mover to stop.
function loot:grab(me)
  observe_pickups(me)
  if me.in_town or me.alive == false or me.cursor_item or charon.now() - last_pickup < PICKUP_MS then return false end
  local item = self:nearby(me, GRAB_RANGE, nil, true)
  return item and self:pickup(item, true) or false
end

-- The caller proves a short straight corridor safe again on every tick.
-- No route or door detour is retained when combat needs to take over.
function loot:approach(me, safe)
  observe_pickups(me)
  if me.in_town or me.alive == false or me.cursor_item then return false end
  local item, distance = self:nearby(me, nil, safe, true)
  if not item then self.approach_id = nil; return false end
  if distance <= GRAB_RANGE then
    -- Keep this freshly proven safe pickup from alternating with route moves
    -- while another successful pickup's shared packet pacing is still active.
    if last_pickup > 0 and charon.now() - last_pickup < PICKUP_MS then return true end
    return self:pickup(item, true)
  end
  if not act.ready() then return false end
  local now = charon.now()
  local changed = self.approach_id ~= item.id
  if changed then self.approach_since = now end
  if now - self.approach_since > 12000 then
    self.ignored[item.id], self.approach_id = now + 60000, nil
    return false
  end
  if changed or now - (self.approached_at or 0) >= APPROACH_MS then
    local step = math.min(APPROACH_STEP, distance)
    if act.move(me.x+(item.x-me.x)*step/distance, me.y+(item.y-me.y)*step/distance, stamina.walk(me)) == false then return false end
    self.approached_at, self.approach_id = now, item.id
  end
  return true
end

-- Walks to the nearest wanted item along a planned path (so items behind
-- bars or walls are reached through the doorway, or given up when there is
-- none) and picks it up.
function loot:tick(me)
  local item, d = self:nearby(me, nil, nil, true)
  if not item then return self:nearby(me) and 'looting' or 'done' end
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
  elseif now - last_pickup >= PICKUP_MS then
    self.mover:stop()
    self:pickup(item, false)
  end
  return 'looting'
end

return loot
