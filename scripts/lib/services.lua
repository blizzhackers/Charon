-- Town visits: heal, sell loot and restock potions, one tick at a time.
--
--   local services = require 'lib.services'
--   local visit = services.new({ hp = 4, mp = 6 }, { 'mp', 'hp' })
--   function tick() local status = visit:tick(game.me()) end -- 'busy' or 'done'
--
-- Talking to Akara heals; her Trade menu buys our loot and sells potions.
-- Each request waits for the reply before the next, and results are read back
-- from gold and items, never assumed. Health, mana and rejuvenation potions
-- move to the belt; vps/yps stay in inventory. `order` sets buying priority.

local loot = require 'lib.loot'
local stash = require 'lib.stash'
local move = require 'lib.move'
local world = require 'lib.world'

local services = {}
services.__index = services

local TALK_RANGE = 4
local STEP_MS = 700        -- menu, belt and stash actions keep their normal settling time
-- The client's sender (0x478350) drops a packet identical to the one before
-- it within 200 ms. Buying two of the same potion is two identical packets,
-- so trades are spaced by more. Charon runs the sender's clock at the game's
-- pace in single player (headers/frame_wait.h), so game time measures the
-- gap at any frame rate.
local TRADE_MS = 240
local TRADE_WAIT_MS = 1500 -- skip refused trades without resending an uncertain request
local BELT_RETRY_MS = 3000 -- retry refused inventory transfers without flooding the belt
local VISIT_TIMEOUT = 90000
-- Vendors take stash gold too, so all gold is stashed: a death drops only
-- what was picked up since the last town visit.
local GOLD_FLOAT = 0
local ANTIDOTE_WINDOW_MS = 60000 -- prepare again only after the boss preparation window
local ANTIDOTE_RETRY_MS = 1500   -- give the item-use reply time to arrive
local ANTIDOTE_WAIT_MS = 8000    -- refused uses must never stall the fight indefinitely

-- Monster class ids of town NPCs, for finding their spawn when out of sight.
services.NPC_CLASS = { Akara = 148, Kashya = 150, Charsi = 154, Gheed = 147, Warriv = 155 }

function services.new(wanted, order)
  return setmetatable({
    wanted = wanted or { hp = 4, mp = 4 },
    order = order or { 'hp', 'mp', 'vps', 'yps' },
    mover = move.new(),
    step = 'start',
    last = 0,
    belted = {},
    stasher = stash.new(loot.rune, GOLD_FLOAT),
  }, services)
end

local function now() return charon.now() end

-- Called near Andariel, outside town. True means a dose was sent or is awaiting
-- acknowledgement; false means continue fighting (ready, unavailable or timed out).
-- Store the pending item as well as the budget so hot reload cannot double-dose.
function services.prepare_antidote(me)
  if not me.alive or me.in_town then return false end
  local key = 'services.antidote.' .. me.name .. '.'
  local t = now()
  if t >= (store.get(key .. 'until') or 0) then
    store.set(key .. 'until', t + ANTIDOTE_WINDOW_MS)
    store.set(key .. 'count', 0)
    store.set(key .. 'pending', nil)
    store.set(key .. 'started', nil)
  end
  local count = store.get(key .. 'count') or 0
  local pending = store.get(key .. 'pending')
  if pending then
    local present = false
    for _, item in ipairs(game.items()) do
      if item.id == pending then present = true; break end
    end
    if not present then
      count = count + 1
      store.set(key .. 'count', count)
      store.set(key .. 'pending', nil)
      print('services: antidote acknowledged (' .. count .. '/2)')
    elseif t < store.get(key .. 'retry') then
      return true
    else
      store.set(key .. 'pending', nil)
      print('services: antidote not acknowledged')
    end
  end
  local started = store.get(key .. 'started')
  if count >= 2 or (started and t - started >= ANTIDOTE_WAIT_MS) then return false end
  if not act.ready() then return false end
  local item = world.inventory_potion('yps')
  if not item then return false end
  store.set(key .. 'started', started or t)
  store.set(key .. 'pending', item.id)
  store.set(key .. 'retry', t + ANTIDOTE_RETRY_MS)
  print('services: use antidote (' .. (count + 1) .. '/2)')
  act.use_item(item)
  return true
end

-- Gold a vendor can take: carried plus stashed.
local function spendable(me) return me.gold + (me.stash_gold or 0) end

function services.npc_name(unit)
  for name, class_id in pairs(services.NPC_CLASS) do
    if unit.class_id == class_id then return name end
  end
end

local function npc_named(me, name)
  return world.nearest(game.units('monster'), me, function(u) return u.class_id == services.NPC_CLASS[name] end)
end

-- Where an NPC stands in the current town, from the map's preset spawns.
local function npc_spawn(name)
  local class_id = services.NPC_CLASS[name]
  for _, preset in ipairs(map.presets()) do
    if preset.type == 'monster' and preset.class_id == class_id then return preset end
  end
end

local function potion_kind(item)
  if item.code == 'vps' or item.code == 'yps' then return item.code end
  return world.potion_kind(item.code)
end

-- Health/mana tiers count belt and inventory; exact vps/yps codes count inventory.
local function carried(prefix)
  local count = 0
  for _, item in ipairs(game.items()) do
    if ((item.location == 'belt' and not loot.SUPPLIES[prefix]) or (item.location == 'container' and item.page == 0)) and potion_kind(item) == prefix then
      count = count + 1
    end
  end
  return count
end

-- Loot in the inventory worth turning into gold, and gear she will not
-- wear, whatever it fetches (loot.surplus, set by the bot): a staff that
-- came off for a better one is worth a few coins and three cells of the pack.
local function for_sale()
  local items = {}
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0
      and (loot.sellable(item) or (loot.surplus and loot.surplus(item))) then
      items[#items + 1] = item
    end
  end
  return items
end

-- Highest affordable potion grade in a vendor's stock, or nil.
local function stock_potion(vendor, prefix, balance)
  local best, best_price
  for _, item in ipairs(game.items(vendor)) do
    if potion_kind(item) == prefix and (not best or item.code > best.code) then
      local price = town.price(item, vendor, 'buy')
      if price <= balance then best, best_price = item, price end
    end
  end
  return best, best_price
end

-- Scrolls of a kind carried in the inventory: 'tsc' town portal, 'isc' identify.
local function scrolls(code)
  local count = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.code == code then count = count + 1 end
  end
  return count
end
local function portal_scrolls() return scrolls('tsc') end
services.portal_scrolls = portal_scrolls
services.scrolls = scrolls

-- Potions of a kind to carry: the base stock, plus two more of each per
-- thousand gold on hand (up to ten more) while the inventory has room. Gold
-- does nothing in the pocket; potions save trips back to town.
local INVENTORY_CELLS, KEEP_FREE = 40, 6
function services:want(prefix)
  local base = self.wanted[prefix] or 0
  if loot.SUPPLIES[prefix] then
    local room = INVENTORY_CELLS - loot.used_cells()
    return math.min(base, carried(prefix) + math.max(0, room))
  end
  if prefix ~= 'hp' and prefix ~= 'mp' then return base end
  local me = game.me()
  local extra = math.min(10, 2 * math.floor((me.gold + (me.stash_gold or 0)) / 1000))
  local room = INVENTORY_CELLS - loot.used_cells() - KEEP_FREE
  return base + math.max(0, math.min(extra, room))
end

function services:short()
  if portal_scrolls() < (self.wanted.tsc or 0) or scrolls('isc') < (self.wanted.isc or 0) then return true end
  for _, prefix in ipairs(self.order) do
    if carried(prefix) < self:want(prefix) then return true end
  end
  return false
end

-- Worn gear that is wearing out: something with a third of its durability
-- or less left (ItemStatCost rows 72 and 73; what cannot wear has no
-- maximum, and an ethereal item cannot be mended). A third leaves a long
-- stint's worth of blows before it breaks and gives her nothing.
local REPAIR_SHARE = 0.34
local REPAIR_MS = 25000 -- for the walk to the smith and the mending, before the visit goes on without it
function services.worn_out()
  if not game.stat then return nil end
  for _, item in ipairs(game.items()) do
    if item.location == 'equipped' and not item.ethereal and item.id then
      local most = game.stat('item', item.id, 73) or 0
      if most > 0 and (game.stat('item', item.id, 72) or 0) <= most * REPAIR_SHARE then return item end
    end
  end
end

function services:needs_akara(me)
  local hurt = me.life < me.max_life * 0.9 or me.mana < me.max_mana * 0.5
  return hurt or #for_sale() > 0 or (self:short() and spendable(me) > 0)
end

function services:needs(me)
  return self:needs_akara(me) or self.stasher:wanted(me) or (not self.repaired and services.worn_out() ~= nil)
end

-- Walks to an NPC and opens their menu; returns the NPC once the menu is open.
function services:talk(me, name)
  local npc = npc_named(me, name)
  if not npc then
    -- Units are only known near the player: walk to the NPC's spawn first.
    if self.mover.status ~= 'moving' then
      local spawn = npc_spawn(name)
      if spawn then
        self.mover:to(spawn.x, spawn.y)
      else
        local x, y, w, h = map.bounds()
        self.mover:to(x + w / 2, y + h / 2)
      end
    end
    self.mover:tick()
    return nil
  end
  if game.ui('npc_menu') or game.ui('npc_shop') then
    self.menu_seen = true
    return npc
  end
  if self.talked_at and not self.menu_seen and now() - self.talked_at > 3000 and now() - self.talked_at < 3600 then
    -- No menu since we spoke: a quest speech is playing; skip it and talk
    -- again. Only then: with her menu opened and closed again in those
    -- seconds, the key opened the escape menu instead, which pauses single
    -- player, and a paused game gives the script no tick to close it with.
    act.key(27)
  end
  if world.distance(me, npc) > TALK_RANGE then
    if self.mover.status ~= 'moving' or now() - (self.planned or 0) > 2000 then
      self.mover:to(npc.x, npc.y)
      self.planned = now()
    end
    self.mover:tick()
  elseif act.ready() and now() - self.last > 4000 then
    act.interact(npc)
    self.last, self.talked_at, self.menu_seen = now(), now(), false
  end
  return nil
end

function services:finish(step)
  print('services: close (' .. step .. ')')
  town.close()
  self.last = now()
  self.step = step
  return 'busy'
end

function services:tick(me)
  if not me.in_town then return 'done' end
  self.started = self.started or now()
  if now() - self.started > VISIT_TIMEOUT then
    local settling = self.stasher:settle(me, true)
    if settling == 'failed' then error('services: stash cursor recovery failed') end
    if settling then return settling end
    print('services: visit timed out')
    town.close()
    return 'done'
  end
  local pending_trade = (self.step == 'sell' and self.sell_pending)
    or (self.step == 'buy' and self.buy_pending)
  if pending_trade then
    if now() - (self.traded_at or 0) < TRADE_MS then return 'busy' end
  elseif now() - self.last < STEP_MS then
    return 'busy'
  end

  if self.step == 'start' then
    self.step = self:needs_akara(me) and 'akara' or 'belt'
  end

  if self.step == 'akara' then
    if not self:needs_akara(me) then
      -- Talking healed us and there is nothing to trade. But she has been
      -- spoken to, and until her menu has come up and been closed the
      -- conversation is still on: on a first meeting she gives the Den's
      -- speech, and with that left running the character answered nothing
      -- for forty seconds, the portal she had come to town for included.
      -- talk() skips the speech and sees the menu up; finish() closes it.
      if self.talked_at and not self.menu_seen and not self:talk(me, 'Akara') then return 'busy' end
      return self:finish('belt')
    end
    local akara = self:talk(me, 'Akara')
    if not akara then return 'busy' end
    self.akara = akara
    if #for_sale() == 0 and not (self:short() and spendable(me) > 0) then return self:finish('belt') end
    if not game.ui('npc_shop') then
      print('services: trade')
      town.trade(akara)
      self.last = now()
      return 'busy'
    end
    self.step = 'sell'
  end

  if self.step == 'sell' then
    self.sell_refused = self.sell_refused or {}
    if self.sell_pending then
      local p, present = self.sell_pending, false
      for _, item in ipairs(game.items()) do
        if item.id == p.id then present = true; break end
      end
      p.received = p.received or not present
      if p.received and spendable(me) < p.balance then
        if now() - p.at < TRADE_WAIT_MS then return 'busy' end
        print('services: sale received but gold unsettled id=' .. p.id)
        self.sell_pending = nil
        return self:finish('belt')
      end
      if not p.received then
        if now() - p.at < TRADE_WAIT_MS then return 'busy' end
        self.sell_refused[p.id] = true
        print('services: sale not acknowledged id=' .. p.id)
      end
      self.sell_pending = nil
    end
    local item
    for _, candidate in ipairs(for_sale()) do
      if not self.sell_refused[candidate.id] then item = candidate; break end
    end
    if item then
      local price = town.price(item, self.akara, 'sell')
      self.sell_pending = { id = item.id, at = now(), balance = spendable(me) + price }
      print(string.format('services: sell %s for %d', tostring(item.name), price))
      town.sell(self.akara, item)
      self.traded_at = now()
      self.last = now()
      return 'busy'
    end
    self.step = 'buy'
  end

  if self.step == 'buy' then
    -- A refused purchase (full grid, unavailable stock) is skipped for this
    -- visit instead of buying it every step until the visit times out.
    self.buy_refused = self.buy_refused or {}
    if self.buy_pending then
      local p = self.buy_pending
      local count = (p.kind == 'tsc' or p.kind == 'isc') and scrolls(p.kind) or carried(p.kind)
      -- Inventory replies can precede gold replies; both must settle before spending again.
      p.received = p.received or count > p.count
      if p.received and spendable(me) > p.balance then
        if now() - p.at < TRADE_WAIT_MS then return 'busy' end
        print('services: purchase received but gold unsettled kind=' .. p.kind)
        self.buy_pending = nil
        return self:finish('belt')
      end
      if not p.received then
        if now() - p.at < TRADE_WAIT_MS then return 'busy' end
        self.buy_refused[p.kind] = true
        print('services: purchase not acknowledged kind=' .. p.kind)
      end
      self.buy_pending = nil
    end
    local room = INVENTORY_CELLS - loot.used_cells()
    local function buy_potion(kind, target)
      if self.buy_refused[kind] or carried(kind) >= target then return false end
      local potion, price = stock_potion(self.akara, kind, spendable(me))
      if not potion or (potion.width or 1) * (potion.height or 1) > room then return false end
      print(string.format('services: buy %s (gold %d)', tostring(potion.name), spendable(me)))
      self.buy_pending = { kind = kind, count = carried(kind), at = now(), balance = spendable(me) - price }
      town.buy(self.akara, potion)
      self.traded_at = now()
      self.last = now()
      return true
    end
    -- A way home comes first: without a scroll every later restock is a new
    -- game and the walk back from the last waypoint, minutes against the
    -- price of two mana potions.
    local function buy_scroll(code)
      code = code or 'tsc'
      if room < 1 or self.buy_refused[code] or scrolls(code) >= (self.wanted[code] or 0) then return false end
      for _, item in ipairs(game.items(self.akara)) do
        local price = item.code == code and town.price(item, self.akara, 'buy')
        if price and price <= spendable(me) then
          print(string.format('services: buy %s (gold %d)', tostring(item.name), spendable(me)))
          self.buy_pending = { kind = code, count = scrolls(code), at = now(), balance = spendable(me) - price }
          town.buy(self.akara, item)
          self.traded_at = now()
          self.last = now()
          return true
        end
      end
      return false
    end
    if buy_scroll() then return 'busy' end
    -- Attack and healing reserves must survive optional travel purchases.
    local reserves_ready = true
    for _, kind in ipairs({ 'mp', 'hp' }) do
      local reserve = math.min(2, self.wanted[kind] or 0)
      if buy_potion(kind, reserve) then return 'busy' end
      if carried(kind) < reserve then reserves_ready = false end
    end
    -- Save toward the missing reserve; cheap stamina must not spend the last
    -- 25 gold each visit and prevent ever affording a 60-gold mana potion.
    if not reserves_ready then return self:finish('belt') end
    for _, prefix in ipairs(self.order) do
      if buy_potion(prefix, self:want(prefix)) then return 'busy' end
    end
    -- Last, with what is left: scrolls to read what she has found (wanted.isc).
    if buy_scroll('isc') then return 'busy' end
    return self:finish('belt')
  end

  -- The smith, when something she wears is wearing out: once a visit, after
  -- Akara's business, since the potions come first if the gold is short.
  if self.step == 'belt' and not self.repaired and services.worn_out() then
    self.step, self.repair_since = 'repair', now()
    self.talked_at, self.menu_seen = nil, false
    print('services: ' .. tostring(services.worn_out().name) .. ' is wearing out; to Charsi')
  end
  if self.step == 'repair' then
    if now() - self.repair_since > REPAIR_MS then
      print('services: repair not reached')
      self.repaired = true
      return self:finish('belt')
    end
    local charsi = self:talk(me, 'Charsi')
    if not charsi then return 'busy' end
    if not game.ui('npc_shop') then
      town.trade(charsi)
      self.last = now()
      return 'busy'
    end
    local price = town.repair_all(charsi)
    print(string.format('services: repair all for %s (gold %d)', tostring(price), spendable(me)))
    self.repaired = true
    return self:finish('belt')
  end

  if self.step == 'belt' then
    -- A refused kind blocks further transfers this pass, even with an equipped belt.
    self.belt_attempted = self.belt_attempted or {}
    self.belt_refused = self.belt_refused or {}
    if not self.logged_panels then
      self.logged_panels = true
      local open = {}
      -- Some UI flags are notifications, not panels: Escape on quest_log
      -- opens the pause menu instead of clearing the notification.
      for _, name in ipairs({ 'npc_menu', 'npc_shop', 'inventory', 'character',
        'skill_tree', 'skill_select', 'chat', 'quests', 'waypoint', 'stash',
        'cube', 'message_log', 'escape_menu' }) do
        if game.ui(name) then open[#open + 1] = name end
      end
      if #open > 0 then
        -- Something else popped up during the visit (the quest log on a quest
        -- update, for example): close it like a player would.
        print('services: closing leftover panels: ' .. table.concat(open, ', '))
        -- NPC panels close through town.close. A hire list is left alone (Esc
        -- on it opens the escape menu; the hire leaves the game instead).
        if game.ui('npc_menu') or game.ui('npc_shop') then
          town.close()
        elseif not game.ui('hireables') then
          act.key(27)
        end
        self.last = now()
        return 'busy'
      end
    end
    local items = game.items()
    if self.belt_pending then
      local p, received = self.belt_pending, false
      for _, item in ipairs(items) do
        if item.id == p.id and item.location == 'belt' then received = true; break end
      end
      if not received then
        if now() - p.at < STEP_MS then return 'busy' end
        self.belt_refused[p.kind] = true
        print('services: belt not acknowledged kind=' .. p.kind)
      end
      self.belt_pending = nil
    end
    local belt_items = not world.starter_belt_full() and items or {}
    for _, item in ipairs(belt_items) do
      local kind = potion_kind(item)
      if item.location == 'container' and item.page == 0 and not self.belt_attempted[item.id]
        and not self.belt_refused[kind]
        and now() >= (self.belted[item.id] or 0)
        and (kind == 'hp' or kind == 'mp' or kind == 'rv') then
        self.belt_attempted[item.id] = true
        self.belted[item.id] = now() + BELT_RETRY_MS
        self.belt_pending = { id = item.id, kind = kind, at = now() }
        print('services: belt ' .. tostring(item.name))
        act.to_belt(item)
        self.last = now()
        return 'busy'
      end
    end
    self.belt_attempted = nil
    self.belt_refused = nil
    self.step = 'stash'
  end

  if self.step == 'stash' then
    -- Keepers (runes) and gold go to the stash: the inventory stays free for
    -- loot and a death costs no banked gold.
    if not self.stasher:wanted(me) and not game.ui('stash') then return 'done' end
    local status = self.stasher:tick(me)
    if status == 'failed' then error('services: stash cursor recovery failed') end
    if status == 'done' then return 'done' end
    return 'busy'
  end
  return 'busy'
end

return services
