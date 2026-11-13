-- Named-feature front end for a directly authored policy network.
--
--   local scorer = require('lib.scorer').new(require 'lib.combat_weights', { 'damage', 'move', 'wait' })
--   local rows = { scorer:row{ damage = 0.4 }, scorer:row{ move = true }, scorer:row{ wait = true } }
--   local best, scores = scorer:rank(rows)   -- one-based winner, score per row
--
-- Inputs are addressed by the names in the weights artifact, never by position,
-- so the code that produces features cannot drift from the tensor's input
-- order. `produced` lists the features the caller fills in: it must match the
-- artifact exactly, so a renamed or added input fails at load instead of
-- silently reading zero. Values are clamped to 0..1 (the range the native
-- scorer accepts); true/false become 1/0; a feature left out of a row is 0.

local scorer = {}
scorer.__index = scorer

function scorer.new(weights, produced)
  assert(policy and policy.compile and policy.rank, 'scorer requires the native policy binding')
  assert(type(weights) == 'table' and type(weights.features) == 'table' and #weights.features > 0,
    'scorer: weights must name their features')
  local index = {}
  for i, name in ipairs(weights.features) do
    assert(type(name) == 'string' and index[name] == nil, 'scorer: duplicate or invalid feature name at input ' .. i)
    index[name] = i
  end
  local wanted = {}
  for _, name in ipairs(produced) do
    assert(index[name], 'scorer: ' .. tostring(weights.version) .. ' has no feature named ' .. tostring(name))
    wanted[name] = true
  end
  for _, name in ipairs(weights.features) do
    assert(wanted[name], 'scorer: ' .. tostring(weights.version) .. ' expects a feature nobody produces: ' .. name)
  end
  return setmetatable({ model = policy.compile(weights), index = index, inputs = #weights.features,
    version = weights.version }, scorer)
end

function scorer:set(row, name, value)
  local i = self.index[name]
  if not i then error('scorer: unknown feature ' .. tostring(name), 2) end
  if value == true then value = 1 elseif not value then value = 0 end
  if type(value) ~= 'number' or value ~= value then error('scorer: feature ' .. name .. ' is not a number', 2) end
  row[i] = math.max(0, math.min(1, value))
end

function scorer:get(row, name)
  return row[self.index[name]]
end

function scorer:row(values)
  local row = {}
  for i = 1, self.inputs do row[i] = 0 end
  for name, value in pairs(values) do self:set(row, name, value) end
  return row
end

function scorer:rank(rows)
  return policy.rank(self.model, rows)
end

return scorer
