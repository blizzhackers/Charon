-- Out-of-game menu helpers: creating a character and entering an existing one.
--
--   local menus = require 'lib.menus'
--   local creator = menus.creator('sorceress')   -- or 'random'
--   local enterer = menus.enterer('Lortar')
--   function tick() creator:tick() end
--
-- Machines end in state 'returned' when the menus come back after the game.
--
-- Each machine acts at most once per settle period and re-reads the screen
-- every tick, because menus change after every action.

local fsm = require 'lib.fsm'
local names = require 'lib.names'

local menus = {}

local SETTLE_MS = 400
local CLASSES = { 'amazon', 'sorceress', 'necromancer', 'paladin', 'barbarian', 'druid', 'assassin' }

function menus.find(controls, predicate)
  for _, c in ipairs(controls) do
    if predicate(c) then return c end
  end
end

function menus.by_role(controls, role)
  return menus.find(controls, function(c) return c.role == role end)
end

function menus.click(control, what)
  local ok, why = oog.click(control)
  if not ok then print('could not click ' .. what .. ': ' .. tostring(why)) end
  return ok
end

-- Logs the popup's text and presses its first enabled button.
function menus.dismiss_popup(controls)
  for _, c in ipairs(controls) do
    if c.modal and c.text then print('popup: ' .. c.text:gsub('\n', ' ')) end
  end
  local button = menus.find(controls, function(c) return c.modal and c.type == 'button' and c.enabled end)
  return button and menus.click(button, 'popup button')
end

local function settle()
  return { tick = function(m) if m:elapsed() >= SETTLE_MS then return 'navigate' end end }
end

-- Waits while the game starts and runs; 'returned' once the menus are back.
local function entering()
  return {
    tick = function(m)
      local screen = oog.screen()
      if screen == 'popup' then return 'navigate', 'popup while entering' end
      if screen ~= 'transition' and screen ~= 'none' and m:elapsed() > 5000 then return 'returned', 'back at ' .. screen end
    end,
  }
end

-- Creates a single player character of `class` with a random name, publishes
-- the name as store 'character' and enters the game.
function menus.creator(class)
  if class == 'random' then class = CLASSES[math.random(#CLASSES)] end
  local name
  return fsm.new('navigate', {
    settle = settle(),
    navigate = {
      tick = function(m)
        local screen, controls = oog.screen(), oog.controls()
        if screen == 'main_menu' then
          local button = menus.by_role(controls, 'single_player')
          if button and menus.click(button, 'Single Player') then return 'settle', 'single player' end
        elseif screen == 'character_select' then
          local button = menus.by_role(controls, 'create_new')
          if button and menus.click(button, 'Create New') then return 'settle', 'create new' end
        elseif screen == 'create_class' then
          for _, c in ipairs(controls) do
            -- Portraits reject clicks while any of them is animating (states 2 and 4).
            if c.role == 'class' and (c.anim_state == 2 or c.anim_state == 4) then return end
          end
          local portrait = menus.find(controls, function(c) return c.role == 'class' and c.class == class end)
          if portrait and menus.click(portrait, class) then return 'settle', 'picked ' .. class end
        elseif screen == 'create_name' then
          local edit, ok = menus.by_role(controls, 'name'), menus.by_role(controls, 'create_ok')
          if not edit or not edit.active then return end
          name = name or names.random_character_name()
          if edit.text ~= name then
            oog.set_text(edit, name)
            return 'settle', 'name ' .. name
          end
          if ok and ok.enabled and ok.active and menus.click(ok, 'OK') then
            store.set('character', name)
            return 'entering', 'creating ' .. name
          end
        elseif screen == 'popup' then
          -- A taken name shows a one-button popup: try another name.
          name = nil
          if menus.dismiss_popup(controls) then return 'settle', 'dismissed popup' end
        elseif screen == 'difficulty' then
          local button = menus.by_role(controls, 'normal')
          if button and menus.click(button, 'Normal') then return 'settle', 'normal difficulty' end
        end
      end,
    },
    entering = entering(),
    returned = {},
  }, { name = 'create' })
end

-- Enters the existing single player character `name` on Normal.
function menus.enterer(name)
  return fsm.new('navigate', {
    settle = settle(),
    navigate = {
      tick = function(m)
        local screen, controls = oog.screen(), oog.controls()
        if screen == 'main_menu' then
          local button = menus.by_role(controls, 'single_player')
          if button and menus.click(button, 'Single Player') then return 'settle', 'single player' end
        elseif screen == 'character_select' then
          local slot = menus.find(controls, function(c)
            return c.role == 'character_slot' and c.text and c.text:find(name, 1, true)
          end)
          if not slot then
            -- More characters than fit on screen are not scrolled to yet.
            if m:elapsed() > 3000 then print('character ' .. name .. ' is not on screen') end
            return
          end
          menus.click(slot, name)
          local ok = menus.by_role(controls, 'select_ok')
          if ok and menus.click(ok, 'OK') then return 'entering', 'entering ' .. name end
        elseif screen == 'difficulty' then
          local button = menus.by_role(controls, 'normal')
          if button and menus.click(button, 'Normal') then return 'entering', 'normal difficulty' end
        elseif screen == 'popup' then
          if menus.dismiss_popup(controls) then return 'settle', 'dismissed popup' end
        end
      end,
    },
    entering = entering(),
    returned = {},
  }, { name = 'enter' })
end

return menus
