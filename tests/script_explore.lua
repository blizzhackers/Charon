-- Movement resumed after combat must start from the player's current position.
local move = require('lib.move')
local explore = require('lib.explore')
local t, frame, me, requests, plans
local function scenario()
  t = 10000
  frame = 1
  me = {x=0,y=0,area=2,stamina=20,max_stamina=20,mode=1}
  requests, plans = {}, {}
  charon = {now=function() return t end,frame=function() return frame end}
  game = {me=function() return me end,units=function() return {} end,level_name=function() return 'test' end}
  act = {ready=function() return true end,move=function(x,y) requests[#requests+1]={x=x,y=y} end}
  map = {invalidate=function() end,path=function(x,y)
    plans[#plans+1]={x=x,y=y,from_x=me.x,from_y=me.y}
    if me.x==0 then return {{x=10,y=0},{x=x,y=y}} end
    return {{x=x,y=y}}
  end}
end

do
  scenario()
  local mover=move.new()
  mover:to(30,0); mover:tick()
  assert(requests[1].x==10)
  -- Combat crossed the old waypoint and then moved off the old route.
  me.x,me.y,t=18,5,t+4000
  frame=frame+100
  mover:tick()
  assert(#plans==2 and plans[2].from_x==18 and #requests==1,
    'resume must replan before sending a stale waypoint behind the player')
  t=t+40; frame=frame+1; mover:tick()
  assert(requests[2].x==30 and mover.replans==0,'combat displacement is not a stuck retry')
end
do
  scenario()
  local mover=move.new()
  mover:to(30,0); mover:tick()
  me.mode=3
  t,frame=t+40,frame+1
  act.move(0,2.5) -- one loot-owned tick, before a newly exposed enemy cancels the approach
  me.y=0.5; t,frame=t+40,frame+1
  mover:tick()
  assert(#plans==1 and #requests==3 and requests[3].x==10 and requests[3].y==0,
    'travel must immediately replace a yielded loot command even after less than one tile of displacement')
end

do
  scenario()
  local mover=move.new()
  mover:to(30,0); mover:tick()
  me.x,t,frame=4,t+40,frame+1; mover:tick()
  assert(#plans==1,'normal route following should not replan each moving tick')
  -- Slow regular ticks against a real obstruction must still fail finitely.
  for i=1,60 do t=t+1300; frame=frame+1; mover:tick() end
  assert(mover.status=='failed','stationary gaps must not reset the stuck watchdog')
end
do
  scenario()
  local mover=move.new()
  mover:to(30,0); mover:tick()
  t,frame=t+5000,frame+125; mover:tick()
  assert(mover.replans==1,'an interruption without displacement must not renew the expired stuck deadline')
  for i=1,60 do t=t+1300; frame=frame+1; mover:tick() end
  assert(mover.status=='failed','the stuck watchdog must resume after combat')
end
do
  scenario()
  local door={id=7,door=true,mode=0,x=25,y=0}
  map.path=function() return nil,'blocked' end
  game.units=function() return {door} end
  game.unit=function() return door end
  local mover=move.new()
  mover:to(30,0)
  assert(mover.door and mover.tried_doors[7])
  local nested=0
  mover.door_mover={tick=function() nested=nested+1; return 'moving' end}
  me.x,t,frame=10,t+4000,frame+100
  mover:tick()
  assert(mover.door and mover.door.id==7 and nested==1,'resume must preserve the pending door approach')
end
-- The selector fixture models its API, not native path ranking (covered in C++).
local function survey_scenario(points)
  scenario()
  local state={points=points or {},seen={},marks={},choices={},resets=0,monsters={}}
  game.units=function(kind) return kind=='monster' and state.monsters or {} end
  game.unit=function(kind,id)
    for _,u in ipairs(state.monsters) do if u.id==id then return u end end
  end
  map.line_of_sight=function() error('loaded navigation targets must not require direct sight') end
  map.explore_reset=function() state.resets=state.resets+1; state.seen={} end
  map.explore_seen=function(x,y) return state.seen[x..':'..y] or false end
  map.explore_mark=function(x,y,r)
    state.marks[#state.marks+1]={x=x,y=y,r=r}
    for _,p in ipairs(state.points) do
      if (p.x-x)^2+(p.y-y)^2<=r*r then state.seen[p.x..':'..p.y]=true end
    end
  end
  map.explore_choose=function(options)
    state.choices[#state.choices+1]=options
    local function excluded(p)
      for _,e in ipairs(options.exclude or {}) do if e.x==p.x and e.y==p.y then return true end end
      return false
    end
    local result={found=false,unseen=3,total=100,elapsed_ms=0,steps=60,gain=30}
    for _,u in ipairs(options.targets or {}) do
      if not excluded(u) then
        result.found,result.kind,result.id,result.x,result.y=true,'monster',u.id,u.x,u.y
        return result
      end
    end
    for _,p in ipairs(state.points) do
      if not map.explore_seen(p.x,p.y) and not excluded(p) and (p.gain or 30)>=options.minimum then
        result.found,result.kind,result.x,result.y=true,'patch',p.x,p.y
        return result
      end
    end
    return result
  end
  return state
end
local function monster(id,x,area)
  return {id=id,type='monster',x=x,y=0,area=area or 2,alive=true,attackable=true,owner=-1}
end
local function advance(ms) t=t+(ms or 40); frame=frame+1 end

do
  local state=survey_scenario({{x=30,y=0},{x=60,y=0},{x=90,y=0}})
  local survey=explore.new(2)
  survey:tick(me)
  assert(plans[#plans].x==30)
  -- Observation still runs while combat/loot owns movement, without moving us.
  local commands=#requests
  me.x=30; advance(1000); survey:observe(me)
  me.x=60; advance(1000); survey:observe(me)
  local before=#state.marks
  for i=1,20 do advance(); survey:observe(me) end
  assert(#state.marks==before and #requests==commands,'stationary combat must not rescan or issue navigation')
  assert(state.marks[1].r==24,'clearing must observe the wider wall-limited sight radius')
  me.area=3; me.x=100; survey:observe(me)
  assert(#state.marks==before,'do not mark a different level through an old explorer')
  me.area=2; me.x=60; survey:tick(me)
  assert(plans[#plans].x==90,'skip ground observed during combat and looting')
end

do
  local state=survey_scenario()
  state.monsters={monster(7,60),monster(8,70,3),monster(9,80),monster(10,90)}
  state.monsters[3].alive=false; state.monsters[4].owner=1
  local survey=explore.new(2)
  survey:tick(me)
  assert(#state.choices[1].targets==0,'a monster out of sight on unsearched ground is not walked to while there is ground to search')
  local targets=state.choices[#state.choices].targets
  assert(#targets==1 and targets[1].id==7,'only living hostile monsters in the current level are navigation goals')
  assert(survey.target.id==7 and plans[#plans].x==60,
    'loaded monsters on searched ground and behind walls must still be pursued')
  state.monsters={monster(7,75)} -- A fresh snapshot, never mutate the original table.
  advance(600); survey:tick(me)
  assert(plans[#plans].x==75,'a moving monster must be replanned to its fresh current position')
  local marks=#state.marks
  state.monsters={}; advance(300); survey:tick(me)
  assert(not survey.target and #state.marks==marks,'a vanished monster releases pursuit without inventing observed ground or a kill')
end

do
  local state=survey_scenario({{x=60,y=0},{x=100,y=0}})
  local survey=explore.new(2); survey:tick(me)
  local choices=#state.choices
  advance(40); survey:tick(me)
  assert(#state.choices==choices,'target selection must be throttled rather than flood-filling every tick')
  me.x=40; advance(300); survey:tick(me)
  assert(map.explore_seen(60,0) and survey.target.x==60,
    'a committed patch remains the destination when seen ahead but still more than twelve subtiles away')
  me.x=49; advance(300); survey:tick(me)
  assert(survey.target.x==100,'an observed patch releases after the player comes within twelve subtiles')
end

do -- ground in the rooms the client holds is searched without walking up to it
  local state=survey_scenario({{x=60,y=0},{x=100,y=0}})
  local survey=explore.new(2); me.x=0; survey:tick(me)
  assert(survey.target.x==60,'the nearest patch first')
  local asked=0
  map.explore_mark_rooms=function() asked=asked+1; state.seen['60:0']=true; return 144 end
  me.x=3; advance(300); survey:tick(me)
  assert(asked==0 and survey.target.x==60,'the rooms the client holds are not counted as searched: that left monsters behind')
  map.explore_mark_rooms=nil
end

do -- a monster that walks out of what the client holds is gone back for where it was last seen
  local state=survey_scenario()
  me.x=0
  state.monsters={monster(7,40),monster(8,10)}
  local survey=explore.new(2); survey:tick(me)
  state.monsters={} -- both gone from the client: one from far off, one from close by
  state.seen['40:0']=true
  advance(300); survey:tick(me)
  advance(300); survey:tick(me)
  assert(survey.target and survey.target.kind=='wayward' and survey.target.id==7 and plans[#plans].x==40,
    'the one that went from far off has walked away: she goes to where it was')
  assert(not survey.wayward[8],'the one that went from beside her is dead or gone, and not looked for')
  state.monsters={monster(7,44)}
  advance(300); survey:tick(me)
  assert(survey.target.kind=='monster' and survey.target.id==7,'found again, it is hers like any other')
  state.monsters={}; me.x=0
  advance(300); survey:tick(me) -- lost again, from 44 off
  assert(not survey.target and survey.wayward[7],'lost while she was after it, it is left a moment (as any target that flickers is)')
  advance(16000); survey:tick(me)
  assert(survey.target and survey.target.kind=='wayward','and then looked for again')
  me.x=38; advance(300); survey:tick(me)
  assert(not survey.target and not survey.wayward[7],'at its last place without finding it, it is given up')
  me.x=0
end

do
  local state=survey_scenario({{x=60,y=0}})
  local survey=explore.new(2); survey:tick(me)
  state.monsters={monster(7,me.x+60)}
  advance(300); survey:tick(me)
  assert(survey.target.kind=='patch','a monster loaded far off on unsearched ground does not take her from her patch')
  state.seen[(me.x+60)..':0']=true
  state.monsters={monster(8,me.x+60)}
  advance(300); survey:tick(me)
  assert(survey.target.id==8,'one on ground already searched does: the search has left it behind')
  survey.target=nil
  state.monsters={monster(7,me.x+20)}
  advance(300); survey:tick(me)
  assert(survey.target.id==7 and plans[#plans].x==me.x+20,'a newly loaded monster within sight may preempt a committed terrain patch')
  state.monsters[1].area=3; advance(300); survey:tick(me)
  assert(not survey.target or survey.target.kind~='monster','a monster changing areas must release pursuit')
end

do
  local state=survey_scenario({{x=60,y=0}})
  map.path=function() return nil,'blocked' end
  local survey=explore.new(2)
  survey:tick(me)
  for _,mark in ipairs(state.marks) do
    assert(mark.x==me.x and mark.y==me.y,'a failed route must never mark its destination as observed')
  end
  assert(not map.explore_seen(60,0),'route failure cannot manufacture exploration coverage')
  survey:cleanup(); advance(300)
  assert(survey:tick(me)=='moving','exhaustive cleanup must wait for temporary route exclusions to expire')
  map.path=function(x,y) plans[#plans+1]={x=x,y=y}; return {{x=x,y=y}} end
  advance(16000); survey:tick(me)
  assert(plans[#plans].x==60,'failed patches must become eligible again after their cooldown')
end

do
  local state=survey_scenario({{x=60,y=0,gain=3}})
  local survey=explore.new(2)
  assert(survey:tick(me)=='done' and not survey.exhaustive,'normal clearing may leave small unseen patches')
  local unseen,total=survey:remaining()
  assert(unseen==3 and total==100,'an exhausted selector must still expose its coverage summary')
  survey:cleanup()
  assert(survey.exhaustive and state.resets==0,'cleanup must preserve real observation and switch to exhaustive selection')
  advance(300); assert(survey:tick(me)=='moving')
  assert(state.choices[#state.choices].minimum==1 and plans[#plans].x==60,'cleanup must route to isolated unseen crumbs')
end

do
  local state=survey_scenario()
  state.monsters={monster(7,60)}
  local survey=explore.new(2); survey:tick(me)
  survey.mover.tick=function() return 'moving' end
  for i=1,32 do advance(500); survey:tick(me) end
  assert(not survey.target or survey.target.id~=7,'pursuit of an undying or unreachable loaded monster must have a finite deadline')
  local marks=#state.marks
  advance(1000); survey:tick(me)
  assert(not survey.target and #state.marks==marks,'pursuit cooldown must not immediately reacquire or fake coverage')
  advance(16000); survey:tick(me)
  assert(survey.target and survey.target.id==7,'a deferred live monster must become eligible again after cooldown')
end

for _,revealed in ipairs({true,false}) do
  local state=survey_scenario({{x=6,y=5,gain=1}})
  me.x,me.y=4.9,4.9
  map.path=function() return {} end
  map.explore_mark=function(x,y,r)
    state.marks[#state.marks+1]={x=x,y=y,r=r}
    if revealed and x==6.1 and y==5.1 then state.seen['6:5']=true end
  end
  local survey=explore.new(2)
  survey:observe(me)
  survey:cleanup()
  -- Combat rounded a corner without reaching the regular observation threshold.
  me.x,me.y=6.1,5.1
  advance(); survey:tick(me)
  assert(#state.marks==1 and survey.target,'cleanup selects the unseen cell beneath the player')
  advance(); survey:tick(me)
  assert(#state.marks==2 and state.marks[2].x==me.x and state.marks[2].y==me.y,
    'arrival must refresh sight at the actual player position, even after a tiny movement')
  assert(not survey.target and #requests==0)
  for i=1,100 do advance(); survey:tick(me) end
  if revealed then
    assert(map.explore_seen(6,5) and #survey.excluded==0,'real observation must finish the patch')
  else
    assert(not map.explore_seen(6,5) and #survey.excluded==1,
      'an unseen patch inside arrival tolerance must be deferred without fabricating coverage')
  end
  assert(#state.marks==2,'arrival must not repeatedly select and rescan the same unresolved cell')
end

do
  local state=survey_scenario({{x=30,y=0},{x=60,y=0}})
  local survey=explore.new(2)
  survey:tick(me); advance(); survey:tick(me)
  assert(requests[#requests].x==10 and survey.target.x==30)
  me.area=1; me.x=10; advance(); survey:tick(me)
  assert(requests[#requests].x==30 and survey.target.x==30,
    'an active exploration route must finish a neighboring-level detour without abandoning its target')
  me.x=28; advance()
  assert(survey:tick(me)=='moving' and survey.mover.status=='arrived' and survey.target.x==30,
    'arrival tolerance in the wrong area must wait for the native command to finish crossing')
  me.area=2; me.x=30; advance(300); survey:tick(me)
  assert(survey.target.x==60 and not survey.cross_since,
    'returning from the detour must advance through real coverage and reset its deadline')
  me.area=1
  assert(survey:tick(me)=='moving')
  advance(4000); me.area=2; survey:observe(me)
  assert(not survey.cross_since,'an observe-only return during combat must reset the detour deadline')
  me.area=1; advance()
  assert(survey:tick(me)=='moving','a later excursion must receive its own deadline')
  advance(5000)
  assert(survey:tick(me)=='outside' and survey.mover.status=='idle' and not survey.target,
    'a detour exceeding five seconds must release movement without marking unseen coverage')
  assert(survey:tick(me)=='outside','an explorer without an active target must not own foreign-level movement')
  me.area=2; me.x=0; advance()
  state.seen={}
  survey=explore.new(2); survey:tick(me)
  me.area=1; me.x=10; advance(); survey:tick(me)
  me.x=28; advance()
  assert(survey:tick(me)=='moving' and survey.mover.status=='arrived')
  advance(5000)
  assert(survey:tick(me)=='outside' and not survey.target,
    'arrival in the wrong area must also expire instead of waiting forever for native completion')
  me.area=2; me.x=0; advance()
  state.seen={}
  survey=explore.new(2); survey:tick(me)
  me.area=1; survey.mover.status='failed'
  assert(survey:tick(me)=='outside' and not survey.target,'a failed mover must release foreign-level ownership immediately')
end
local travel=require('lib.travel')
local function travel_scenario(kind, target)
  scenario()
  me.area=26
  map.exits=function(area)
    if area==26 then return {{level=27,kind=kind or 'border',x=-20,y=0}} end
    if area==27 then return {{level=28,kind='border'}} end
    if area==7 then return {{level=26,kind='border'}} end
    return {}
  end
  map.path_to_level=function(level)
    plans[#plans+1]={level=level,from=me.area}
    if level==27 then return {{x=-10,y=0},{x=-20,y=0}} end
    return {{x=10,y=0},{x=20,y=0}}
  end
  local trip=travel.new()
  trip:to(target or 28)
  trip:tick()
  return trip
end

do
  local trip=travel_scenario()
  local hop=trip.hop
  t,frame=t+40,frame+1; trip:tick()
  assert(requests[#requests].x==-10)
  me.area,me.x,t,frame=7,-4,t+40,frame+1
  trip:tick()
  assert(trip.hop==hop and #plans==1 and requests[#requests].x==-10,
    'a third-area border detour must retain the existing forward command instead of planning back to the previous area')
  me.x,t,frame=-9.5,t+40,frame+1
  trip:tick()
  assert(trip.hop==hop and #plans==1 and requests[#requests].x==-20,
    'the existing border mover must continue its next forward waypoint during the detour')
  me.area,me.x,t,frame=26,-12,t+40,frame+1
  trip:tick()
  assert(not hop.cross_since,'returning to the source area must reset the border detour deadline')
  me.area,t,frame=7,t+40,frame+1
  trip:tick()
  assert(hop.cross_since==t,'a later detour must get a fresh deadline')
  me.area,t,frame=27,t+40,frame+1
  trip:tick()
  assert(trip.hop.from==27 and trip.hop.to==28 and not hop.cross_since,
    'reaching an intermediate destination must start the next normal hop')
  me.area=28
  assert(trip:tick()=='arrived' and trip.mover.status=='idle','the final destination must still finish normally')
end

for _,status in ipairs({'failed','idle','arrived'}) do
  local trip=travel_scenario()
  local hop=trip.hop
  me.area=7
  trip.mover.tick=function(self) self.status=status; return status end
  trip:tick()
  assert(trip.hop~=hop and trip.hop.from==7 and trip.hop.to==26 and plans[#plans].level==26,
    'an unsuccessful or unexpectedly finished third-area mover must release the old route: '..status)
end

do
  local trip=travel_scenario()
  local hop=trip.hop
  me.area=7
  local ticks=0
  trip.mover.tick=function() ticks=ticks+1; return 'moving' end
  trip:tick()
  local started=t
  t=started+4999; trip:tick()
  assert(trip.hop==hop and ticks==2,'a moving border detour retains its route strictly before five seconds')
  t=started+5000; trip:tick()
  assert(trip.hop~=hop and trip.hop.from==7 and ticks==2,
    'the five-second deadline must fall back from the actual area without another old-route tick')
end

do
  local trip=travel_scenario(nil,27)
  local hop=trip.hop
  me.area=7
  trip.mover.tick=function() return 'moving' end
  trip:tick()
  me.area=27
  assert(trip:tick()=='arrived' and trip.mover.status=='idle' and not hop.cross_since,
    'arrival at a final border destination must clear the detour deadline')
end

do
  local trip=travel_scenario('warp')
  local hop=trip.hop
  me.area,me.x=7,me.x+500 -- a stair or portal: the old path does not lead from here
  trip:tick()
  assert(trip.hop~=hop and trip.hop.from==7,
    'warp hops must still replan immediately after a jump to an unexpected area')
  me.x=me.x-500
  trip=travel_scenario('warp')
  hop=trip.hop
  me.area=7 -- walked over an open border beside the route
  trip.mover.tick=function() return 'moving' end
  trip:tick()
  assert(trip.hop==hop,'a warp route that walks through a corner of a neighbouring level keeps its plan')
  trip=travel_scenario('warp')
  me.area=27
  trip:tick()
  assert(trip.hop.from==27 and trip.hop.to==28,'successful warp transitions must start the next normal hop')
end
do -- ground kept off is searched around: no monster or patch on it is gone to
  scenario()
  world = nil
  local zones={{x=10,y=0,radius=5}}
  local explorer=explore.new(2,nil,function() return zones end)
  assert(explorer:kept_off({x=12,y=3}) and not explorer:kept_off({x=20,y=0}),'a point is kept off inside a zone only')
  assert(not explorer:eligible({area=2,x=12,y=0,alive=true,hostile=true}),'a monster on ground kept off is not walked up to')
  zones={}
  assert(not explorer:kept_off({x=12,y=3}),'with nothing kept off every point is open')
  assert(not explore.new(2):kept_off({x=12,y=3}),'an explorer without the option searches as before')
end
print('exploration interruption regressions passed')
