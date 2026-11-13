-- Optional Tower versus main-route utility; no game queries or actions.
--   local planner = require('lib.strategy').new()
--   local action, scores = planner:choose(context)
-- Every context field is required except failures and tower_ms, which default
-- to zero. Scores use [1]=progress, [2]=tower; an illegal action has no score.
-- XP-per-damage priors are comparable inputs, not measured experience rates.
local scorer = require 'lib.scorer'
local weights = require 'lib.strategy_weights'
local strategy = {}
strategy.__index = strategy
-- Every input this module fills in; lib.scorer checks it against the artifact.
local FEATURES = {'progress','tower','bolt_gap','control_gap','level_gap','xp_value',
  'travel_cost','reset_cost','first_clear','commitment','failures','tower_time'}
local ACTIONS = {'progress','tower'}
local function number(context,name,minimum,default)
  local n=context[name]
  if n==nil then n=default end
  assert(type(n)=='number' and n==n and n<math.huge and n>=minimum,
    'strategy context '..name..' must be finite and >= '..minimum)
  return n
end
local function boolean(context,name)
  local value=context[name]
  assert(type(value)=='boolean','strategy context '..name..' must be boolean')
  return value
end

function strategy.new()
  local s=scorer.new(weights,FEATURES)
  return setmetatable({scorer=s,model=s.model,version=weights.version},strategy)
end

function strategy:choose(context)
  assert(type(context)=='table','strategy context must be a table')
  local level=number(context,'level',1)
  local bolt=number(context,'bolt_rank',0)
  local nova,static=boolean(context,'nova'),boolean(context,'static')
  local first=boolean(context,'first_clear')
  local committed,unavailable=boolean(context,'committed'),boolean(context,'tower_unavailable')
  assert(not (committed and unavailable),'strategy cannot commit to an unavailable Tower')
  local reset=boolean(context,'reset')
  local hops={number(context,'progress_hops',0),number(context,'tower_hops',0)}
  local values={number(context,'progress_value',0),number(context,'tower_value',0)}
  local failures=number(context,'failures',0,0)
  local tower_ms=number(context,'tower_ms',0,0)
  local bolt_gap=(4-bolt)/4
  local control_gap=((nova and 0 or 1)+(static and 0 or 1))/2
  local level_gap=(10-level)/4 -- the Barracks and Jail punish a sorceress below nine
  -- Scaling the common denominator avoids overflow for finite large priors.
  local scale=math.max(values[1],values[2])
  local total=scale>0 and values[1]/scale+values[2]/scale or 0
  local rows,legal={},{}
  for index=1,2 do
    local tower=index==2
    if (tower and not unavailable) or (not tower and not committed) then
      rows[#rows+1]=self.scorer:row{progress=not tower,tower=tower,bolt_gap=bolt_gap,
        control_gap=control_gap,level_gap=level_gap,
        xp_value=total>0 and (values[index]/scale)/total or 0,travel_cost=hops[index]/6,
        reset_cost=tower and reset,first_clear=first,commitment=tower and committed,
        failures=failures/3,tower_time=tower_ms/600000}
      legal[#legal+1]=index
    end
  end
  local chosen,ranked=self.scorer:rank(rows)
  local scores={}
  for i,index in ipairs(legal) do scores[index]=ranked[i] end
  return ACTIONS[legal[chosen]],scores
end
return strategy
