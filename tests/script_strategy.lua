-- Actual authored tensors and wrapper; native policy when installed by CTest.
package.path='scripts/?.lua;'..package.path
local backend=dofile('tests/policy_reference.lua')
local weights=require 'lib.strategy_weights'
local strategy=require 'lib.strategy'
assert(#weights.features==12 and #weights.w1==228 and #weights.b1==19 and #weights.w2==19)
local parameters=1
for _,name in ipairs{'w1','b1','w2'} do
  for _,n in ipairs(weights[name]) do
    assert(type(n)=='number' and n==n and math.abs(n)<math.huge,'strategy tensors must be finite')
    parameters=parameters+1
  end
end
assert(parameters==267 and weights.b2==0 and weights.version=='strategy-v2')
local function context(changes)
  local result={level=5,bolt_rank=3,nova=false,static=false,first_clear=true,
    committed=false,tower_unavailable=false,reset=false,tower_hops=2,progress_hops=1,
    tower_value=1,progress_value=1,failures=0,tower_ms=0}
  for name,value in pairs(changes or {}) do result[name]=value end
  return result
end
local function clamp(n) return math.max(0,math.min(1,n)) end
local function relu(n) return math.max(0,n) end
-- Independent scalar route equations. These priors are not measured XP rates.
local function scalar(c,tower)
  local p,t=tower and 0 or 1,tower and 1 or 0
  local bolt=clamp((4-c.bolt_rank)/4)
  local controls=(not c.nova and .5 or 0)+(not c.static and .5 or 0)
  local level=clamp((10-c.level)/4)
  local value=tower and c.tower_value or c.progress_value
  local sum=c.tower_value+c.progress_value
  local xp=sum>0 and value/sum or 0
  return 3*p+.5*t+2*xp-clamp((tower and c.tower_hops or c.progress_hops)/6)
    -(tower and c.reset and 1 or 0)+2*(tower and c.committed and 1 or 0)
    +8*relu(t+controls-1)+4*relu(t+bolt-1)+6*relu(t+level-1)
    +1.5*relu(t+(c.first_clear and 1 or 0)-1)
    -2*relu(t+clamp((c.tower_ms or 0)/600000)-1)
    -3*relu(t+clamp((c.failures or 0)/3)-1)
    -6*relu(p+controls+bolt-1)
end
local planner=strategy.new()
local function check(c)
  local action,scores=planner:choose(c)
  assert(action=='progress' or action=='tower')
  for index,name in ipairs{'progress','tower'} do
    local legal=index==1 and not c.committed or index==2 and not c.tower_unavailable
    if legal then
      assert(type(scores[index])=='number' and math.abs(scores[index]-scalar(c,index==2))<1e-9,
        'strategy wrapper and independent scalar disagree for '..name)
    else
      assert(scores[index]==nil,'an illegal route must have no utility score')
    end
  end
  local expected=c.committed and 'tower' or c.tower_unavailable and 'progress'
    or scores[2]>scores[1] and 'tower' or 'progress'
  assert(action==expected,'the model must choose the first maximum among legal routes')
  return action,scores
end

assert(check(context())=='tower','level five without controls should prepare in the Tower')
assert(check(context{level=6,bolt_rank=4,nova=true,static=true,first_clear=false})=='tower',
  'level six is too low for the Barracks: keep preparing even with the controls learned')
assert(check(context{level=10,bolt_rank=4,nova=true,static=true,first_clear=false})=='progress',
  'prepared level ten with a cleared Countess should continue the main route')
assert(check(context{level=8,bolt_rank=4,nova=true,static=true,first_clear=false,tower_ms=600000})=='progress',
  'ten minutes of preparation is enough at level eight')
assert(check(context{level=20,bolt_rank=20,nova=true,static=true,first_clear=false,
  committed=true,failures=10,tower_ms=1200000,reset=true})=='tower','finish an existing descent even when its utility is poor')
assert(check(context{tower_unavailable=true})=='progress','an unavailable Tower leaves only progress legal')
do
  local c=context();c.failures,c.tower_ms=nil,nil
  check(c) -- the only numeric defaults are zero-valued history counters
end

local original_rank=policy.rank
local observations=0
policy.rank=function(model,rows)
  for _,row in ipairs(rows) do
    assert(#row==12)
    for i,n in ipairs(row) do
      assert(type(n)=='number' and n==n and n>=0 and n<=1,'strategy features must be finite and normalized')
    end
    assert(row[1]+row[2]==1,'each candidate must identify exactly one route')
    observations=observations+1
  end
  return original_rank(model,rows)
end
for _,level in ipairs{1,5,6,7,20} do
  for _,bolt in ipairs{0,2,4,10} do
    for mask=0,3 do
      for _,edge in ipairs{0,1,2} do
        check(context{level=level,bolt_rank=bolt,nova=mask%2==1,static=mask>=2,
          first_clear=edge~=1,reset=edge==2,tower_value=edge,progress_value=2-edge,
          tower_hops=edge*6,progress_hops=(2-edge)*6,failures=edge*3,tower_ms=edge*600000})
      end
    end
  end
end
check(context{tower_value=0,progress_value=0})
-- Finite priors can overflow their naive sum: normalized utilities must remain valid.
local action,scores=planner:choose(context{tower_value=1e308,progress_value=1e308})
local expected_action,expected_scores=planner:choose(context())
assert(action==expected_action and math.abs(scores[1]-expected_scores[1])<1e-9
  and math.abs(scores[2]-expected_scores[2])<1e-9,'large finite priors must normalize without overflow')
policy.rank=original_rank
assert(observations>400,'exercise boundary observations through the real wrapper and model')

for _,field in ipairs{'level','bolt_rank','tower_hops','progress_hops','tower_value','progress_value','failures','tower_ms'} do
  for _,bad in ipairs{-1,math.huge,-math.huge,0/0,'1',false} do
    local c=context();c[field]=bad
    assert(not pcall(planner.choose,planner,c),'malformed numeric context must reject '..field)
  end
  if field~='failures' and field~='tower_ms' then
    local c=context();c[field]=nil
    assert(not pcall(planner.choose,planner,c),'material numeric context must be explicit: '..field)
  end
end
assert(not pcall(planner.choose,planner,context{level=0}),'level must be at least one')
for _,field in ipairs{'nova','static','first_clear','committed','tower_unavailable','reset'} do
  for _,bad in ipairs{0,1,'false'} do
    local c=context();c[field]=bad
    assert(not pcall(planner.choose,planner,c),'context flags must be booleans: '..field)
  end
  local c=context();c[field]=nil
  assert(not pcall(planner.choose,planner,c),'context flags must be explicit: '..field)
end
assert(not pcall(planner.choose,planner,{}))
assert(not pcall(planner.choose,planner,'context'))
assert(not pcall(planner.choose,planner,context{committed=true,tower_unavailable=true}),'conflicting route constraints must reject')
for _,index in ipairs{1,6,12} do
  local old=weights.features[index];weights.features[index]='incompatible'
  local ok,message=pcall(strategy.new)
  weights.features[index]=old
  assert(not ok and tostring(message):find('feature',1,true),'a renamed artifact input must refuse to load')
end
do -- Inputs are bound by name: a reordered artifact scores exactly as before.
  local before={planner:choose(context())}
  local f,w=weights.features,weights.w1
  f[3],f[4]=f[4],f[3]
  for h=0,18 do w[h*12+3],w[h*12+4]=w[h*12+4],w[h*12+3] end
  local after={strategy.new():choose(context())}
  f[3],f[4]=f[4],f[3]
  for h=0,18 do w[h*12+3],w[h*12+4]=w[h*12+4],w[h*12+3] end
  assert(after[1]==before[1] and math.abs(after[2][1]-before[2][1])<1e-12
    and math.abs(after[2][2]-before[2][2])<1e-12,'feature order must not matter to named producers')
end
local saved_policy=policy;policy=nil
assert(not pcall(strategy.new),'strategy must require the policy binding')
policy=saved_policy
print('strategy-v2 authored model and wrapper checks passed')
