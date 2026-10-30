-- Town visits: heal, sell loot and restock potions, one tick at a time.
--
--   local services = require 'lib.services'
--   local visit = services.new({ hp = 4, mp = 6 }, { 'mp', 'hp' })
--   function tick() local status = visit:tick(game.me()) end -- 'busy' or 'done'
--
-- Talking to Akara heals; her Trade menu buys our loot and sells potions.
-- Each request waits for the reply before the next, and results are read back
-- from gold and items, never assumed. Potions that land in the inventory are
-- moved to the belt. `order` says which potion kind to buy first.

local loot = require 'lib.loot'
local stash = require 'lib.stash'
local move = require 'lib.move'
local world = require 'lib.world'

local services = {}
services.__index = services

local TALK_RANGE = 4
local STEP_MS = 700        -- pause between requests so replies can arrive
local VISIT_TIMEOUT = 90000
local MIN_GOLD = 50
-- Vendors take stash gold too, so all gold is stashed: a death drops only
-- what was picked up since the last town visit.
local GOLD_FLOAT = 0

-- Monster class ids of town NPCs, for finding their spawn when out of sight.
services.NPC_CLASS = { Akara = 148, Kashya = 150, Charsi = 154, Gheed = 147, Warriv = 155 }

function services.new(wanted, order)
  return setmetatable({
    wanted = wanted or { hp = 4, mp = 4 },
    order = order or { 'hp', 'mp' },
    mover = move.new(),
    step = 'start',
    last = 0,
    belted = {},
    stasher = stash.new(loot.rune, GOLD_FLOAT),
  }, services)
end

local function now() return charon.now() end

-- Gold a vendor can take: carried plus stashed.
local function spendable(me) return me.gold + (me.stash_gold or 0) end

local function npc_named(me, name)
  return world.nearest(game.units('monster'), me, function(u) return u.name == name end)
end

-- Where an NPC stands in the current town, from the map's preset spawns.
local function npc_spawn(name)
  local class_id = services.NPC_CLASS[name]
  for _, preset in ipairs(map.presets()) do
    if preset.type == 'monster' and preset.class_id == class_id then return preset end
  end
end

local function potion_kind(item)
  return item.code and #item.code == 3 and item.code:sub(1, 2) or nil
end

-- Potions of a kind ('hp', 'mp') carried in the belt or inventory.
local function carried(prefix)
  local count = 0
  for _, item in ipairs(game.items()) do
    if (item.location == 'belt' or (item.location == 'container' and item.page == 0)) and potion_kind(item) == prefix then
      count = count + 1
    end
  end
  return count
end

-- Loot in the inventory worth turning into gold.
local function for_sale()
  local items = {}
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and loot.sellable(item) then items[#items + 1] = item end
  end
  return items
end

-- Best potion of a kind in a vendor's stock (highest grade), or nil.
local function stock_potion(vendor, prefix)
  local best
  for _, item in ipairs(game.items(vendor)) do
    if potion_kind(item) == prefix and (not best or item.code > best.code) then best = item end
  end
  return best
end

-- Town Portal scrolls carried in the inventory.
local function portal_scrolls()
  local count = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.code == 'tsc' then count = count + 1 end
  end
  return count
end
services.portal_scrolls = portal_scrolls

-- Potions of a kind to carry: the base stock, plus two more of each per
-- thousand gold on hand (up to ten more) while the inventory has room. Gold
-- does nothing in the pocket; potions save trips back to town.
local INVENTORY_CELLS, KEEP_FREE = 40, 6
function services:want(prefix)
  local base = self.wanted[prefix] or 0
  if prefix ~= 'hp' and prefix ~= 'mp' then return base end
  local me = game.me()
  local extra = math.min(10, 2 * math.floor((me.gold + (me.stash_gold or 0)) / 1000))
  local room = INVENTORY_CELLS - loot.used_cells() - KEEP_FREE
  return base + math.max(0, math.min(extra, room))
end

function services:short()
  if portal_scrolls() < (self.wanted.tsc or 0) then return true end
  for _, prefix in ipairs(self.order) do
    if carried(prefix) < self:want(prefix) then return true end
  end
  return false
end

function services:needs(me)
  local hurt = me.life < me.max_life * 0.9 or me.mana < me.max_mana * 0.5
  return hurt or #for_sale() > 0 or (self:short() and spendable(me) >= MIN_GOLD)
    or self.stasher:wanted(me)
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
  if game.ui('npc_menu') or game.ui('npc_shop') then return npc end
  if self.talked_at and now() - self.talked_at > 3000 and now() - self.talked_at < 3600 then
    -- No menu yet: a quest speech is playing; skip it and talk again.
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
    self.last, self.talked_at = now(), now()
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
    print('services: visit timed out')
    town.close()
    return 'done'
  end
  if now() - self.last < STEP_MS then return 'busy' end

  if self.step == 'start' then
    self.step = self:needs(me) and 'akara' or 'belt'
  end

  if self.step == 'akara' then
    if not self:needs(me) then return self:finish('belt') end -- talking healed us; nothing to trade
    local akara = self:talk(me, 'Akara')
    if not akara then return 'busy' end
    self.akara = akara
    if #for_sale() == 0 and not (self:short() and spendable(me) >= MIN_GOLD) then return self:finish('belt') end
    if not game.ui('npc_shop') then
      print('services: trade')
      town.trade(akara)
      self.last = now()
      return 'busy'
    end
    self.step = 'sell'
  end

  if self.step == 'sell' then
    -- One sale per step; the next step sees it gone and the gold added.
    local item = for_sale()[1]
    if item and (self.sold_at or {})[item.id] == nil then
      self.sold_at = self.sold_at or {}
      self.sold_at[item.id] = now()
      print(string.format('services: sell %s for %d', tostring(item.name), town.price(item, self.akara, 'sell')))
      town.sell(self.akara, item)
      self.last = now()
      return 'busy'
    end
    self.step = 'buy'
  end

  if self.step == 'buy' then
    -- A Town Portal scroll first: it turns every emergency into a free heal.
    if portal_scrolls() < (self.wanted.tsc or 0) then
      for _, item in ipairs(game.items(self.akara)) do
        if item.code == 'tsc' and town.price(item, self.akara, 'buy') <= spendable(me) then
          print(string.format('services: buy %s (gold %d)', tostring(item.name), spendable(me)))
          town.buy(self.akara, item)
          self.last = now()
          return 'busy'
        end
      end
    end
    for _, prefix in ipairs(self.order) do
      if carried(prefix) < self:want(prefix) then
        local potion = stock_potion(self.akara, prefix)
        if potion and town.price(potion, self.akara, 'buy') <= spendable(me) then
          print(string.format('services: buy %s (gold %d)', tostring(potion.name), spendable(me)))
          town.buy(self.akara, potion)
          self.last = now()
          return 'busy'
        end
      end
    end
    return self:finish('belt')
  end

  if self.step == 'belt' then
    if not self.logged_panels then
      self.logged_panels = true
      local open = {}
      for name in pairs(game.ui()) do
        if name ~= 'hire_icons' and name ~= 'automap' and name ~= 'hireables' then open[#open + 1] = name end
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
      end
    end
    for _, item in ipairs(game.items()) do
      local kind = potion_kind(item)
      if item.location == 'container' and item.page == 0 and not self.belted[item.id]
        and (kind == 'hp' or kind == 'mp' or kind == 'rv') then
        self.belted[item.id] = true
        print('services: belt ' .. tostring(item.name))
        act.to_belt(item)
        self.last = now()
        return 'busy'
      end
    end
    self.step = 'stash'
  end

  if self.step == 'stash' then
    -- Keepers (runes) and gold go to the stash: the inventory stays free for
    -- loot and a death costs no banked gold.
    if not self.stasher:wanted(me) and not game.ui('stash') then return 'done' end
    if self.stasher:tick(me) == 'done' then return 'done' end
    return 'busy'
  end
  return 'busy'
end

return services
