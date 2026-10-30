-- Small state machine for per-tick behaviour; no sleeping or waiting.
--
--   local fsm = require 'lib.fsm'
--   local bot = fsm.new('idle', {
--     idle = { tick = function(m) if something then return 'work' end end },
--     work = {
--       enter = function(m, from) ... end,
--       tick = function(m) if m:elapsed() > 2000 then return 'idle', 'timed out' end end,
--       exit = function(m, to) ... end,
--     },
--   })
--   function tick() bot:tick() end
--
-- A state's tick returns nothing to stay, or the next state's name plus an
-- optional reason (logged with the transition). Elapsed time is in milliseconds.

local fsm = {}
fsm.__index = fsm

function fsm.new(initial, states, options)
  assert(states[initial], 'unknown initial state ' .. tostring(initial))
  local machine = setmetatable({
    states = states,
    name = options and options.name or 'fsm',
    quiet = options and options.quiet or false,
    data = {},
  }, fsm)
  machine:go(initial)
  return machine
end

function fsm:go(state, reason)
  local target = self.states[state]
  assert(target, self.name .. ': unknown state ' .. tostring(state))
  local previous = self.state
  if previous and self.states[previous].exit then self.states[previous].exit(self, state) end
  self.state = state
  self.entered = charon.now()
  self.ticks = 0
  if not self.quiet then
    print(string.format('%s: %s -> %s%s', self.name, tostring(previous), state,
      reason and (' (' .. reason .. ')') or ''))
  end
  if target.enter then target.enter(self, previous) end
end

function fsm:elapsed()
  return charon.now() - self.entered
end

function fsm:tick(...)
  self.ticks = self.ticks + 1
  local current = self.states[self.state]
  if not current.tick then return end
  local nextState, reason = current.tick(self, ...)
  if nextState and nextState ~= self.state then self:go(nextState, reason) end
end

function fsm:is(state)
  return self.state == state
end

return fsm
