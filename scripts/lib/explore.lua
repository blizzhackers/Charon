-- Fights what is at hand, searches worthwhile reachable patches, and goes back for what is left.
--
--   local explorer = require('lib.explore').new(levelId)
--   explorer:observe(game.me(), combat:monsters()) -- also during combat/loot
--   local status = explorer:tick(game.me())       -- moving, done, outside
--   if status == 'done' then explorer:cleanup() end -- mandatory objectives only
--
-- Coverage records searched ground, not monster deaths. The native choice
-- goes to the nearest unsearched patch, counting ground that belongs to a
-- large unsearched region as farther, so pockets beside the way are searched
-- as they are passed and not walked back to; cleanup includes the small tails.
-- Allocated rooms alone never count as searched. Quest records decide completion.
--
-- explorer.accept = function(unit) ... end limits which monsters are walked
-- up to (what is not worth fighting is not worth the walk either).
-- explore.new(level, reset, avoid [, keep_off]): `avoid` returns ground to
-- keep off, as for lib.move ({{x, y, radius}, ...}); routes go around it,
-- and monsters and patches inside it are left for later. `keep_off`, when
-- given, returns the ground nothing is sought on (routes may still cross
-- what of it `avoid` does not name).

local move = require 'lib.move'
local world = require 'lib.world'

local explore = {}
explore.__index = explore

local SIGHT = 24           -- match clear-combat reach; loaded units may be known beyond it
local CELL = 12            -- native connected patches inside these blocks
local MINIMUM = 24         -- leave tiny leftovers until a mandatory cleanup pass
local COMMIT_DISTANCE = 12 -- finish a useful approach instead of changing patches at its first visible cell
local CHOOSE_MS = 250      -- full native distance flood is only needed at decision boundaries
local PURSUE_MS = 15000    -- a loaded target must not cause an endless chase
local RETRY_MS = 15000     -- failed routes stay unknown and can be retried later
local CROSS_LEVEL_MS = 5000 -- let a native route finish a brief neighboring-level detour
-- Monsters that walk out of what the client holds (a Fallen that ran, one a
-- shaman raised behind her) are remembered where they were last seen and
-- gone back for, like any monster on searched ground. Forgotten, they left
-- the Den one kill short with all of it searched, and she searched all of
-- it again, eleven times in one run, without coming near them.
--   WAYWARD_FROM   a monster that vanishes nearer than this has died or been
--                  taken away; farther, it has walked off (or she has)
--   WAYWARD_REACH  this near its last place without finding it, it is given up
local WAYWARD_FROM, WAYWARD_REACH = 25, 8
-- Counting the rooms the client holds as searched (map.explore_mark_rooms)
-- was tried and is off: it took the walking of a Den clear from 1,100-2,000
-- steps to under 1,000 in the one clean run, and in two of five left
-- monsters in ground it had marked and never came near. How far off the
-- client really knows a monster is logged (`farthest`) to settle it.
local ROOMS = false

function explore.new(level, reset, avoid, keep_off)
  if reset then map.explore_reset(level) end
  return setmetatable({level=level, mover=move.new(avoid and {avoid=avoid} or nil), avoid=keep_off or avoid,
    loaded={}, blocked={}, excluded={}, known={}, wayward={}, farthest=0}, explore)
end

-- Whether a point lies on ground to keep off.
function explore:kept_off(point)
  for _, zone in ipairs(self.avoid and self.avoid() or {}) do
    if (point.x-zone.x)^2 + (point.y-zone.y)^2 <= zone.radius^2 then return true end
  end
  return false
end

function explore:remaining()
  return self.unseen, self.total
end

function explore:eligible(unit)
  return unit and unit.area == self.level and unit.x and unit.y and world.hostile(unit)
    and (not self.accept or self.accept(unit)) and not self:kept_off(unit)
end

-- Keep identities only; the next decision reads each unit's fresh client snapshot.
function explore:observe(me, units)
  if me.area ~= self.level then return end
  self.cross_since = nil
  local frame = charon.frame and charon.frame() or charon.now()
  if self.observed_frame == frame then return end
  self.observed_frame = frame
  local loaded, present = {}, {}
  for _, unit in ipairs(units or game.units('monster')) do
    if unit.id then present[unit.id] = true end
    if self:eligible(unit) then
      loaded[unit.id] = true
      if not self.loaded[unit.id] then self.new_monsters = true end
      self.known[unit.id] = { x = unit.x, y = unit.y }
      self.wayward[unit.id] = nil
      local away = math.sqrt((unit.x-me.x)^2 + (unit.y-me.y)^2)
      if away > self.farthest then self.farthest = away end
    end
  end
  -- Those she knew and no longer has. One still on the client has died (its
  -- body lies there) or is no concern of hers any more; one that is gone
  -- from it has walked off, unless it went from close by.
  for id, last in pairs(self.known) do
    if not loaded[id] then
      if not present[id] and (last.x-me.x)^2 + (last.y-me.y)^2 > WAYWARD_FROM*WAYWARD_FROM then
        self.wayward[id] = last
      end
      self.known[id] = nil
    end
  end
  self.loaded = loaded
  local last = self.marked
  if not last or (me.x-last.x)^2 + (me.y-last.y)^2 >= 4 then
    map.explore_mark(me.x, me.y, SIGHT)
    -- The rooms around her are known to the client with everything in them:
    -- nothing more is found there by walking up to every corner. (Searched
    -- by sight alone, the Den cost 1,100 to 2,000 steps a clear, three to
    -- five times what its size asks for, and the walking was most of it.)
    self.rooms_marked = ROOMS and map.explore_mark_rooms ~= nil and map.explore_mark_rooms() > 0
    self.marked = {x=me.x,y=me.y}
  end
end

function explore:cleanup()
  if self.exhaustive then return false end
  print(string.format('explore: the farthest monster the client held was %.0f subtiles off', self.farthest))
  self.exhaustive, self.target, self.queried_at = true, nil, nil
  self.mover:stop()
  print('explore: ' .. game.level_name(self.level) .. ' cleanup of small remaining patches')
  return true
end

function explore:next_target(me)
  local now, targets, exclude = charon.now(), {}, {}
  for id, until_at in pairs(self.blocked) do
    if now >= until_at then self.blocked[id] = nil end
  end
  -- Monsters worth walking to now: those within sight, which the fight
  -- would take up anyway, and those on ground already searched, which the
  -- search has left behind it. The rest stand on ground it has yet to reach
  -- and are met when it gets there. Walked to at once, each one took her
  -- across unsearched ground, and in wide areas the search then came back
  -- for what lay between: a quarter of its walking in the Blood Moor was
  -- legs of ninety steps and more.
  local beyond = {}
  for id in pairs(self.loaded) do
    local unit = game.unit('monster', id)
    if self:eligible(unit) and not self.blocked[id] then
      local at_hand = (unit.x-me.x)^2 + (unit.y-me.y)^2 <= SIGHT*SIGHT
        or map.explore_seen(math.floor(unit.x), math.floor(unit.y))
      local list = at_hand and targets or beyond
      if #list < 512 then list[#list+1] = {id=id,x=unit.x,y=unit.y} end
    end
  end
  -- And those that walked off, where they were last seen.
  for id, last in pairs(self.wayward) do
    local list = map.explore_seen(math.floor(last.x), math.floor(last.y)) and targets or beyond
    if not self.blocked[id] and #list < 512 then list[#list+1] = {id=id,x=last.x,y=last.y} end
  end
  local kept = {}
  for _, point in ipairs(self.excluded) do
    if now < point.until_at then
      kept[#kept+1] = point
      exclude[#exclude+1] = {x=point.x,y=point.y}
    end
  end
  self.excluded = kept
  local choice = map.explore_choose({minimum=self.exhaustive and 1 or MINIMUM, cell=CELL,
    targets=targets,exclude=exclude,heading_x=self.heading_x or 0,heading_y=self.heading_y or 0})
  -- With no ground left to search and nothing at hand, the rest of what is loaded.
  if not choice.found and #beyond > 0 then
    choice = map.explore_choose({minimum=self.exhaustive and 1 or MINIMUM, cell=CELL,
      targets=beyond,exclude=exclude,heading_x=self.heading_x or 0,heading_y=self.heading_y or 0})
  end
  self.queried_at, self.new_monsters = now, false
  self.unseen, self.total = choice.unseen, choice.total
  if not choice.found then return nil end
  if choice.kind == 'monster' and self.wayward[choice.id] then choice.kind = 'wayward' end
  print(string.format('explore: %s %s%s, %d steps, %d new cells, %d/%d unsearched (%.1f ms) at (%d,%d) from (%d,%d)',
    game.level_name(self.level), choice.kind, choice.id and (' ' .. choice.id) or '',
    choice.steps, choice.gain, choice.unseen, choice.total, choice.elapsed_ms or 0,
    choice.x or 0, choice.y or 0, me.x, me.y))
  return choice
end

function explore:failed(target, why)
  local now = charon.now()
  if target.kind == 'monster' then
    self.blocked[target.id] = now + RETRY_MS
  else
    self.excluded[#self.excluded+1] = {x=target.x,y=target.y,until_at=now+RETRY_MS}
    if #self.excluded > 64 then table.remove(self.excluded, 1) end
  end
  print(string.format('explore: defer %s %s (%s); ground remains unsearched',
    target.kind, tostring(target.id or (target.x .. ',' .. target.y)), why))
  self.target = nil
  self.mover:stop()
end

function explore:tick(me)
  if me.area ~= self.level then
    if not self.target then return 'outside' end
    self.cross_since = self.cross_since or charon.now()
    if charon.now() - self.cross_since >= CROSS_LEVEL_MS then self.mover:stop() end
    if self.mover.status == 'moving' then self.mover:tick() end
    if self.mover.status == 'failed' or self.mover.status == 'idle' then
      self.target, self.cross_since = nil, nil
      return 'outside'
    end
    return 'moving'
  end
  self:observe(me)
  local now = charon.now()
  local frame = charon.frame and charon.frame() or now
  local resumed = self.tick_frame and frame > self.tick_frame + 1
  self.tick_frame = frame
  local target = self.target
  if target and target.kind == 'wayward' then
    if self:eligible(game.unit('monster', target.id)) then
      target.kind = 'monster' -- found again: hers like any other
    elseif not self.wayward[target.id] or world.distance(me, target) <= WAYWARD_REACH
      or now - self.pursued_at >= PURSUE_MS then
      -- Not where it was: it is taken for gone until it shows itself again.
      self.wayward[target.id] = nil
      target, self.target = nil, nil
      self.mover:stop()
    end
  end
  if target and target.kind == 'monster' then
    local unit = game.unit('monster', target.id)
    if not self:eligible(unit) then
      -- Disappearance is loss of knowledge, not proof of death. One that is
      -- there and not there by turns is left alone for a while: chosen and
      -- dropped four times a second, it held her in the Den until the game
      -- was given up for want of progress.
      self.blocked[target.id] = now + RETRY_MS
      target, self.target = nil, nil
      self.mover:stop()
    elseif now - self.pursued_at >= PURSUE_MS then
      self:failed(target, 'pursuit timed out')
      return 'moving'
    elseif (unit.x-target.x)^2 + (unit.y-target.y)^2 >= 16 and now-self.planned_at >= 500 then
      target.x, target.y, self.planned_at = unit.x, unit.y, now
      if self.mover:to(target.x, target.y) == 'failed' then self:failed(target, 'target moved beyond reach') end
      return 'moving'
    end
  elseif target and map.explore_seen(target.x, target.y)
    and (resumed or self.rooms_marked or world.distance(me,target) <= COMMIT_DISTANCE) then
    target, self.target = nil, nil
  end
  local due = not self.queried_at or now-self.queried_at >= CHOOSE_MS
  if due and (not target or (target.kind == 'patch' and self.new_monsters)) then
    local choice = self:next_target(me)
    if target and (not choice or (choice.kind ~= 'monster' and choice.kind ~= 'wayward')) then choice = target end
    if choice ~= target then
      self.target, target = choice, choice
      if not target then
        if self.exhaustive and (next(self.blocked) or #self.excluded > 0) then return 'moving' end
        return 'done'
      end
      if target.kind == 'patch' and self:kept_off(target) then
        self:failed(target, 'on ground kept off')
        return 'moving'
      end
      local dx, dy = target.x-me.x, target.y-me.y
      local distance = math.sqrt(dx*dx+dy*dy)
      self.heading_x, self.heading_y = distance>0 and dx/distance or 0, distance>0 and dy/distance or 0
      self.pursued_at, self.planned_at = now, now
      if self.mover:to(target.x,target.y) == 'failed' then self:failed(target, 'route unavailable') end
      return 'moving'
    elseif not target then
      if self.exhaustive and (next(self.blocked) or #self.excluded > 0) then return 'moving' end
      return 'done'
    end
  end
  if not target then return 'moving' end
  local status = self.mover:tick()
  if status == 'failed' then
    self:failed(target, 'movement failed')
  elseif status ~= 'moving' then
    if target.kind == 'wayward' then
      self.wayward[target.id] = nil
      self.target = nil
    elseif target.kind == 'monster' then
      local unit = game.unit('monster', target.id)
      local seen = unit and unit.x and select(2, pcall(map.line_of_sight, me.x, me.y, unit.x, unit.y))
      self:failed(target, string.format('arrived without engagement, %s', unit and unit.x
        and string.format('%.0f from it, %s', math.sqrt((unit.x-me.x)^2+(unit.y-me.y)^2), seen == true and 'in sight' or 'out of sight')
        or 'and it is gone'))
    else
      -- A short step around a corner can reveal ground between regular observations.
      map.explore_mark(me.x, me.y, SIGHT)
      self.marked = {x=me.x,y=me.y}
      if map.explore_seen(target.x, target.y) then self.target = nil
      else self:failed(target, 'arrived without observing patch') end
    end
  end
  return 'moving'
end

return explore
