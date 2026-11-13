-- Small queries over `game` snapshots shared by in-game scripts.

local world = {}

function world.distance(a, b)
  return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2)
end

-- Nearest unit in `list` to `from` that passes `accept` (optional); returns unit, distance.
function world.nearest(list, from, accept)
  local best, best_distance
  for _, unit in ipairs(list) do
    if unit.x and (not accept or accept(unit)) then
      local d = world.distance(from, unit)
      if not best_distance or d < best_distance then
        best, best_distance = unit, d
      end
    end
  end
  return best, best_distance
end

function world.hostile(unit)
  return unit.type == 'monster' and unit.alive and unit.attackable and not unit.npc
    and unit.owner == -1 and not unit.mercenary
end

-- 'hp', 'mp' or 'rv' for a healing, mana or rejuvenation potion of any tier,
-- nil for anything else. Exact codes only: a Military Pick is 'mpi'.
function world.potion_kind(code)
  if not code or #code ~= 3 then return nil end
  local kind = code:sub(1, 2)
  if (kind == 'hp' or kind == 'mp') and code:find('^%a%a[1-5]$') then return kind end
  if code == 'rvs' or code == 'rvl' then return 'rv' end
end

-- `kind` is a potion kind ('hp', 'mp', 'rv') or an exact item code ('vps', 'yps').
local function is_kind(code, kind)
  if #kind == 3 then return code == kind end
  return world.potion_kind(code) == kind
end

-- Belt slot holding a potion of `kind` ('hp', 'mp', 'rv').
-- Bottom row first, matching what the belt hotkeys use.
function world.belt_slot(prefix)
  local best
  for _, item in ipairs(game.items()) do
    if item.location == 'belt' and is_kind(item.code, prefix) then
      if not best or item.grid_x < best then best = item.grid_x end
    end
  end
  return best
end

-- A potion of a kind in the inventory (page 0), for when the belt has none.
function world.inventory_potion(prefix)
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and is_kind(item.code, prefix) then
      return item
    end
  end
end

function world.count_belt(prefix)
  local count = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'belt' and is_kind(item.code, prefix) then count = count + 1 end
  end
  return count
end

-- Without an equipped belt there are exactly four slots. Equipped belts can
-- add rows; leave those transfers to the server until their capacity is exposed.
function world.starter_belt_full()
  local count = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'equipped' and item.body_location == 8 then return false end -- BODYLOC_BELT, ghidra/enums.h
    if item.location == 'belt' then count = count + 1 end
  end
  return count >= 4
end

return world
