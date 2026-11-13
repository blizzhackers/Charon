-- The authored combat-v4 tensors: named scenarios, then a differential check
-- against an independent scalar statement of the same equations.
package.path='scripts/?.lua;'..package.path
local backend=dofile('tests/policy_reference.lua')
local weights=require 'lib.combat_weights'
local inputs,hidden=#weights.features,#weights.b1
assert(weights.version=='combat-v4' and inputs==18 and hidden==25
  and #weights.w1==inputs*hidden and #weights.w2==hidden)
local model=backend.compile(weights)
local indices={}
for i,name in ipairs(weights.features) do indices[name]=i end
local function row(values)
  local result={}
  for i=1,inputs do result[i]=0 end
  result[indices.mana],result[indices.thrift]=1,1
  for name,v in pairs(values) do assert(indices[name],name); result[indices[name]]=v end
  return result
end
local function chooses(label, choices, expected)
  local rows={}
  for i,choice in ipairs(choices) do rows[i]=row(choice) end
  local index,scores=backend.rank(model,rows)
  assert(index==expected,label..': chose '..index..' with '..table.concat(scores,','))
end
do
  local _,scores=backend.rank(model,{row{wait=1}})
  assert(math.abs(scores[1]+.2)<1e-9,'quiet waiting costs only its small idle penalty')
end
-- One weak monster in our face is no reason to stop shooting it.
chooses('a trivial threat is shot where it stands', {
  {tempo=1,finish=1,lethality_after=.09,mana_cost=.08,time=.55},
  {move=1,tempo=.3,lethality_after=.03,time=.7}, {wait=1,lethality_after=.09}},1)
chooses('a pack that takes nearly half our remaining life is kited when a step truly relieves it', {
  {tempo=1,finish=.3,lethality_after=.3,mana_cost=.08,time=.55,engaged=1},
  {move=1,tempo=.3,lethality_after=.1,time=.7,engaged=1}, {wait=1,lethality_after=.3,engaged=1}},2)
chooses('the same pack is fought when no step relieves it', {
  {tempo=1,finish=.3,lethality_after=.3,mana_cost=.08,time=.55,engaged=1},
  {move=1,tempo=.3,lethality_after=.28,time=.7,engaged=1}, {wait=1,lethality_after=.3,engaged=1}},1)
chooses('a pack that takes all our remaining life is kited', {
  {tempo=1,finish=.3,lethality_after=.5,mana_cost=.08,time=.55},
  {move=1,tempo=.3,lethality_after=.17,time=.7}, {wait=1,lethality_after=.5}},2)
chooses('the same pack is fought in place with a deep life pool', {
  {tempo=1,finish=.3,lethality_after=.2,mana_cost=.08,time=.55},
  {move=1,tempo=.3,lethality_after=.06,time=.7}, {wait=1,lethality_after=.2}},1)
chooses('a fight that is won where we stand is finished there, whatever it costs meanwhile', {
  {tempo=1,finish=.25,lethality_after=.38,outlast=.5,mana_cost=.15,time=.55,engaged=1,target_priority=1},
  {move=1,tempo=.1,lethality_after=.15,outlast=.2,time=.7,engaged=1}, {wait=1,lethality_after=.38,engaged=1}},1)
chooses('the same fight is left when it would take all we have', {
  {tempo=1,finish=.25,lethality_after=.38,mana_cost=.15,time=.55,engaged=1,target_priority=1},
  {move=1,tempo=.1,lethality_after=.15,time=.7,engaged=1}, {wait=1,lethality_after=.38,engaged=1}},2)
chooses('closing on a boss is worth its reach when the fight is won from there', {
  {tempo=.2,finish=.05,lethality_after=.2,outlast=.1,mana_cost=.12,time=.55,engaged=1,target_priority=1},
  {move=1,tempo=.7,lethality_after=.38,outlast=.5,time=.6,engaged=1,target_priority=1},
  {wait=1,lethality_after=.2,engaged=1}},2)
chooses('a volley that mostly misses from here closes the distance', {
  {tempo=.4,finish=.4,mana_cost=.08,time=.55}, {move=1,tempo=.8,time=.6}, {wait=1}},2)
chooses('a volley that mostly connects is not interrupted by a short step', {
  {tempo=.85,finish=.6,mana_cost=.08,time=.55}, {move=1,tempo=.8,time=.6}, {wait=1}},1)
chooses('closing into a lethal pack is refused in favour of ranged shots', {
  {tempo=.4,finish=.2,lethality_after=.15,mana_cost=.08,time=.55},
  {move=1,tempo=.8,lethality_after=.5,time=.6}, {wait=1,lethality_after=.15}},1)
chooses('a weak pack beside the firing spot is no reason to stay away', {
  {move=1,tempo=.7,lethality_after=.09,time=.6,engaged=1}, {wait=1,engaged=1},
  {tempo=.3,finish=.2,time=.55,engaged=1}},1)
chooses('with a target and nothing better, any useful step beats idling', {
  {move=1,tempo=.05,time=.9,engaged=1}, {wait=1,engaged=1}},1)
chooses('travel does not sidestep for nothing', {
  {move=1,time=.5}, {wait=1}},2)
chooses('an approach still short of range beats standing idle', {
  {move=1,progress=.4,time=.6}, {wait=1}},1)
chooses('a step that gains nothing is not taken', {
  {move=1,time=.3}, {wait=1}},2)
chooses('incoming missiles are sidestepped before another animation', {
  {tempo=1,finish=1,missile_after=1,time=.55}, {move=1,missile_after=0,time=.4}},2)
chooses('a dodge that does not leave the lane is not worth the step', {
  {wait=1,missile_after=.5}, {move=1,missile_after=.5,time=.4}},1)
chooses('the stronger of two attacks wins on damage, not mana', {
  {tempo=1,finish=.5,mana_cost=.12,time=.55}, {tempo=.5,finish=.25,mana_cost=.06,time=.55}},1)
chooses('equal damage is dealt with the cheaper cast', {
  {tempo=1,finish=1,mana_cost=.12,time=.55}, {tempo=1,finish=1,mana_cost=.06,time=.55}},2)
chooses('a last sliver of mana is not spent on a weak cast', {
  {tempo=.2,finish=.1,mana=.15,mana_cost=.15,time=.55,nova=1}, {tempo=1,finish=.3,mana=.15,mana_cost=.1,time=.55}},2)
chooses('below half a pool, a cast that buys little waits for a better use of the mana', {
  {tempo=.26,finish=.02,mana=.16,mana_cost=.14,thrift=.05,time=.55,engaged=1},
  {wait=1,mana=.16,engaged=1}},2)
chooses('the same cast is made once the pool covers it and the better one', {
  {tempo=.26,finish=.02,mana=.4,mana_cost=.14,thrift=.05,time=.55,engaged=1},
  {wait=1,mana=.4,engaged=1}},1)
chooses('with little mana the cast that buys most with it is still made', {
  {tempo=1,finish=.25,mana=.16,mana_cost=.15,thrift=1,time=.55,engaged=1},
  {wait=1,mana=.16,engaged=1}},1)
chooses('defensive Nova is worth it when it slows what is killing us and little else helps', {
  {tempo=.3,finish=.1,lethality_after=.6,time=.55},
  {nova=1,tempo=.2,finish=.05,lethality_after=.47,mana_cost=.2,time=.55}},2)
chooses('defensive Nova still yields to an imminent missile', {
  {nova=1,tempo=.3,lethality_after=.3,missile_after=1,time=.55,target_chill=1,kite_intent=1},
  {move=1,missile_after=0,time=.4,target_chill=1,kite_intent=1}},2)
for _,intent in ipairs{0,1} do
  chooses('chill setup follows the baseline kite intent '..intent, {
    {nova=1,tempo=.1,finish=.02,lethality_after=.5,mana_cost=.09,time=.55,target_chill=1,kite_intent=intent},
    {move=1,tempo=.3,lethality_after=.2,time=.7,target_chill=1,kite_intent=intent}},intent==1 and 1 or 2)
end
chooses('an elite or resurrector breaks a tie between equal attacks', {
  {tempo=1,finish=.5,time=.55}, {tempo=1,finish=.5,time=.55,target_priority=1}},2)
-- Independent scalar statement of combat-v4.
local function relu(x) return math.max(0,x) end
local function scalar(x)
  return 6*x.tempo+2*x.finish+2*x.progress-14*x.missile_after
    -.4*x.move-.5*x.time-.5*x.mana_cost-.1*x.nova+.15*x.continuity+.3*x.target_priority-.2*x.wait
    -14*relu(x.lethality_after-.6*x.outlast-.1)-30*relu(x.lethality_after-.6*x.outlast-.2)
    -14*relu(x.lethality_after-.6*x.outlast-.4)-1.5*relu(x.wait+x.engaged-1)
    -3*relu(x.mana_cost-.35*x.mana+.1)-12*relu(.5-x.mana-x.thrift)
    +24*relu(x.nova+x.target_chill+x.kite_intent-x.missile_after-2)
end
local rows,named={},{}
for a=1,64 do
  rows[a],named[a]={},{}
  for i,name in ipairs(weights.features) do
    rows[a][i]=((a*37+i*13)%101)/100
    named[a][name]=rows[a][i]
  end
end
local _,scores=backend.rank(model,rows)
for a,x in ipairs(rows) do
  assert(math.abs(scores[a]-scalar(named[a]))<1e-9,'authored tensors disagree with the scalar equations')
  local expected=weights.b2
  for h=1,hidden do
    local z=weights.b1[h]
    for i=1,inputs do z=z+weights.w1[(h-1)*inputs+i]*x[i] end
    expected=expected+math.max(0,z)*weights.w2[h]
  end
  assert(math.abs(scores[a]-expected)<1e-9,'native/reference tensor disagreement')
end
print('authored combat weight regressions passed')
