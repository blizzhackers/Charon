-- Counts of the consumables the character carries (belt and inventory).
--
--   local supplies = require 'lib.supplies'
--   local carried = supplies.carried()        -- { hp=, mp=, rv=, vps=, yps=, tsc= }
--   if supplies.mana() == 0 then ... end      -- mana potions plus rejuvenations
--   if supplies.healing() < 2 then ... end    -- healing potions plus rejuvenations
--   local life = supplies.life_points(me)     -- life those doses restore for this class
--
-- One pass over game.items() is shared by every caller within a tick.

local world = require 'lib.world'

local supplies = {}

-- Vanilla 1.14d patch_d2.mpq Misc.txt calc1, then Items.cpp's class coefficient.
supplies.HEALTH_DOSES = { hp1=30, hp2=60, hp3=100, hp4=180, hp5=320 }
supplies.HEALTH_COEFFICIENT = { amazon=1.5, sorceress=1, necromancer=1, paladin=1.5,
  barbarian=2, druid=1, assassin=1.5 }
supplies.REJUV_PERCENT = { rvs=35, rvl=100 } -- instant percent of the maximum
-- The mana column of the same table and its coefficients, from memory of the
-- data rather than re-read here: used only for estimates, never for dosing.
supplies.MANA_DOSES = { mp1=20, mp2=40, mp3=80, mp4=150, mp5=250 }
supplies.MANA_COEFFICIENT = { amazon=1.5, sorceress=2, necromancer=2, paladin=1.5,
  barbarian=1, druid=2, assassin=1.5 }

local counted_frame, counts

-- Potion counts by kind. hp/mp/rv cover every tier; vps, yps and tsc are exact codes.
function supplies.carried()
  local frame = charon.frame and charon.frame()
  if counts and frame and frame == counted_frame then return counts end
  local found = { hp = 0, mp = 0, rv = 0, vps = 0, yps = 0, tsc = 0, codes = {} }
  for _, item in ipairs(game.items()) do
    local code = item.code
    if code and (item.location == 'belt' or (item.location == 'container' and item.page == 0)) then
      local kind = found[code] and code or world.potion_kind(code)
      if kind then
        found[kind] = found[kind] + 1
        found.codes[code] = (found.codes[code] or 0) + 1
      end
    end
  end
  counted_frame, counts = frame, found
  return found
end

-- Doses that restore mana: mana potions and rejuvenations.
function supplies.mana()
  local carried = supplies.carried()
  return carried.mp + carried.rv
end

-- Doses that restore life: healing potions and rejuvenations.
function supplies.healing()
  local carried = supplies.carried()
  return carried.hp + carried.rv
end

-- Life the carried healing potions and rejuvenations restore for this character.
function supplies.life_points(me)
  local total = 0
  for code, count in pairs(supplies.carried().codes) do
    local dose = supplies.HEALTH_DOSES[code] and supplies.HEALTH_DOSES[code] * (supplies.HEALTH_COEFFICIENT[me.class] or 1)
      or supplies.REJUV_PERCENT[code] and me.max_life * supplies.REJUV_PERCENT[code] / 100 or 0
    total = total + dose * count
  end
  return total
end

-- Mana the carried mana potions and rejuvenations restore for this character.
function supplies.mana_points(me)
  local total = 0
  for code, count in pairs(supplies.carried().codes) do
    local dose = supplies.MANA_DOSES[code] and supplies.MANA_DOSES[code] * (supplies.MANA_COEFFICIENT[me.class] or 1)
      or supplies.REJUV_PERCENT[code] and me.max_mana * supplies.REJUV_PERCENT[code] / 100 or 0
    total = total + dose * count
  end
  return total
end

-- What one mana potion restores for this character: the best she carries,
-- and the smallest potion at the least (what she carries is what she could buy).
function supplies.mana_dose(me)
  local best = supplies.MANA_DOSES.mp1
  for code in pairs(supplies.carried().codes) do
    best = math.max(best, supplies.MANA_DOSES[code] or 0)
  end
  return best * (supplies.MANA_COEFFICIENT[me.class] or 1)
end

-- Gold a vendor can take: carried plus stashed.
function supplies.gold(me)
  return me.gold + (me.stash_gold or 0)
end

return supplies
