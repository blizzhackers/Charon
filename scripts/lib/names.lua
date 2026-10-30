-- Character name helpers. The game accepts 2..15 ASCII letters with at most one
-- '-' or '_', which may not be first or last.

local names = {}

local ONSETS = { 'b', 'br', 'c', 'd', 'dr', 'f', 'g', 'gr', 'h', 'k', 'l', 'm', 'n', 'p', 'r', 's', 'st', 't', 'th', 'v', 'w', 'z' }
local VOWELS = { 'a', 'e', 'i', 'o', 'u', 'ae', 'ai', 'or', 'ar', 'en' }
local CODAS = { '', '', 'n', 'r', 's', 'th', 'l', 'k', 'm', 'x' }

local function pick(list)
  return list[math.random(#list)]
end

function names.is_valid_character_name(name)
  if type(name) ~= 'string' or #name < 2 or #name > 15 then return false end
  if not name:match('^[%a%-_]+$') then return false end
  local _, separators = name:gsub('[%-_]', '')
  if separators > 1 then return false end
  return not name:match('^[%-_]') and not name:match('[%-_]$')
end

function names.random_character_name()
  local name
  repeat
    name = ''
    for _ = 1, math.random(2, 3) do
      name = name .. pick(ONSETS) .. pick(VOWELS)
    end
    name = name .. pick(CODAS)
    name = name:sub(1, 1):upper() .. name:sub(2)
  until #name >= 4 and #name <= 15 and names.is_valid_character_name(name)
  return name
end

return names
