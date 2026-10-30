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

-- Belt slot holding a potion whose code starts with `prefix` ('hp', 'mp', 'rv').
-- Bottom row first, matching what the belt hotkeys use.
function world.belt_slot(prefix)
  local best
  for _, item in ipairs(game.items()) do
    if item.location == 'belt' and item.code and item.code:sub(1, #prefix) == prefix then
      if not best or item.grid_x < best then best = item.grid_x end
    end
  end
  return best
end

-- A potion of a kind in the inventory (page 0), for when the belt has none.
function world.inventory_potion(prefix)
  for _, item in ipairs(game.items()) do
    if item.location == 'container' and item.page == 0 and item.code and item.code:sub(1, #prefix) == prefix
      and #item.code == 3 then
      return item
    end
  end
end

function world.count_belt(prefix)
  local count = 0
  for _, item in ipairs(game.items()) do
    if item.location == 'belt' and item.code and item.code:sub(1, #prefix) == prefix then count = count + 1 end
  end
  return count
end

return world
