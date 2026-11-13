-- Native movement refusal is a local path failure, not an issued command.
local move = require('lib.move')
local saved_charon = charon
local function scenario()
  local s = {now=10000, frame=1, ready=true, accepted=false, calls={}, steps={}}
  local me = {x=0,y=0,area=1,mode=1,stamina=20,max_stamina=20}
  charon = {now=function() return s.now end, frame=function() return s.frame end}
  game = {me=function() return me end, units=function() return {} end}
  map = {invalidate=function() end, path=function(x,y,options)
    s.steps[#s.steps+1]=options.step
    local path={}
    for at=me.x+options.step,x,options.step do path[#path+1]={x=at,y=y} end
    if #path==0 or path[#path].x~=x then path[#path+1]={x=x,y=y} end
    return path
  end}
  map.path_to_level=function(_,options) return map.path(30,0,options) end
  act = {ready=function() return s.ready end, move=function(x,y)
    s.calls[#s.calls+1]={x=x,y=y,at=s.now}
    return s.accepted
  end}
  function s:tick(mover, ms, frames)
    self.now, self.frame = self.now+(ms or 40), self.frame+(frames or 1)
    return mover:tick()
  end
  return s,me,move.new({step=15})
end

do
  local s,me,mover=scenario()
  mover:to(30,0)
  mover:tick()
  assert(#s.calls==1 and mover.issued==nil and mover.replans==1 and s.steps[2]==7,
    'a refused waypoint must replan immediately with a shorter step without issuing movement')
  assert(mover.progress_at==10000,'refusal must not renew the progress deadline')
  s:tick(mover,100,3)
  assert(#s.calls==1 and mover.replans==1,'resuming without displacement must preserve the refusal throttle and budget')
  s.accepted=true
  s:tick(mover,200)
  assert(#s.calls==2 and mover.issued==1 and mover.issued_at==10300,'a transient refusal must recover when its retry becomes due')
  me.x=7
  s:tick(mover,40)
  assert(mover.index==2 and mover.progress_at==10340,'observed movement must advance the route normally')
  mover:to(50,0)
  assert(s.steps[#s.steps]==15 and mover.step==15 and mover.replans==0 and not mover.retry_at,
    'future routes must restore the configured step and a fresh finite budget')
end

do
  -- What breaks and stands on the next leg is broken on the way, before the step is tried.
  local s,me,mover=scenario()
  local broken={}
  local barrel={id=7,name='Barrel',x=2,y=0,mode=0,operate=29}
  game.units=function(kind) return kind=='object' and {barrel,{id=8,name='Barrel',x=-2,y=0,mode=0,operate=29},
    {id=9,name='Shrine',x=1,y=0,mode=0,operate=2},{id=10,name='Barrel',x=3,y=6,mode=0,operate=29}} or {} end
  act.interact=function(object) broken[#broken+1]=object.id; barrel.mode=2 end
  mover:to(30,0)
  mover:tick()
  assert(#broken==1 and broken[1]==7 and #s.calls==0 and mover.replans==0 and mover.status=='moving',
    'the barrel on the way is struck before a step is tried: not the one behind, the one off to the side, or the shrine')
  s:tick(mover,300)
  assert(#s.calls==0,'the step waits for the blow to land')
  s.accepted=true
  s:tick(mover,400)
  assert(#s.calls==1 and mover.issued==1,'with the barrel gone the step is taken')
  for i=1,40 do
    barrel.mode=0
    s:tick(mover,700)
  end
  assert(#broken==12,'a route breaks twelve at most: more than that is a storeroom, got '..#broken)
end

do
  local s,me,mover=scenario()
  mover:to(30,0)
  mover:tick()
  s.ready=false
  s:tick(mover,100,4)
  assert(#s.calls==1 and mover.replans==1,'animation readiness must prevent refusal retries')
  s.ready=true
  for i=1,40 do
    if s:tick(mover,40,3)=='failed' then break end
  end
  assert(mover.status=='failed' and mover.reason=='movement refused' and #s.calls==5,
    'repeated refusal, including resumed ticks without displacement, must exhaust the finite budget')
  assert(s.now<=11340 and mover.progress_at==10000,'refusals must fail promptly without manufacturing progress')
  assert(s.steps[1]==15 and s.steps[2]==7 and s.steps[3]==3 and s.steps[4]==3 and s.steps[5]==3,
    'refused routes must halve their local step down to three while retaining the original setting')
  for i=2,#s.calls do
    assert(s.calls[i].at-s.calls[i-1].at>=300,'refused movement must be throttled')
  end
end

do
  local s,me,mover=scenario()
  s.accepted=true
  mover:to(30,0)
  mover:tick()
  s.ready=false
  s:tick(mover,1600,40)
  assert(mover.replans==0 and mover.progress_at==10000,'not-ready interruptions without displacement must not renew progress or consume retries')
  s.ready=true
  s:tick(mover,40,3)
  assert(mover.replans==1,'resuming without movement must retain the expired no-progress deadline')
end

do
  local s,me,mover=scenario()
  mover:to_level(2); mover:tick()
  assert(s.steps[2]==7 and mover.replans==1,'level routes must also shorten refused native waypoints')
  me.area=2
  assert(s:tick(mover)=='arrived','a real area change must still complete a level route')
  mover:to_level(3)
  assert(s.steps[#s.steps]==15 and mover.replans==0,'the next level route must restore normal waypoint spacing')
end

do
  local s,me,mover=scenario()
  s.accepted=true
  me.x,me.y=12601.5,9549.5
  map.path=function() return {{x=12601,y=9551},{x=12616,y=9551}} end
  map.line_clear=function(x,y,to_x,to_y)
    assert(x==me.x and y==me.y and to_x==12616 and to_y==9551,
      'corner smoothing must query movement clearance from the actual position to the next waypoint')
    return false
  end
  map.line_of_sight=function() return true end
  mover:to(12616,9551)
  mover:tick()
  assert(mover.index==1 and #s.calls==1 and s.calls[1].x==12601 and s.calls[1].y==9551,
    'an intermediate corner within the arrival radius must be reached before requesting its next segment')
  me.y=9551.1
  s:tick(mover)
  assert(mover.index==2 and #s.calls==2 and s.calls[2].x==12616,
    'entering the corner cell must advance the route even with a fractional player position')
  me.x,me.y=12614.5,9551.5
  assert(s:tick(mover)=='arrived','the final goal must keep its normal arrival radius')
end

for _,clear in ipairs({false,true}) do
  local s,me,mover=scenario()
  s.accepted=true
  me.x,me.y=5.1,0.5
  map.path=function() return {{x=7,y=0},{x=15,y=0}} end
  if clear then
    map.line_clear=function(x,y,to_x,to_y)
      assert(x==5.1 and y==0.5 and to_x==15 and to_y==0)
      return true
    end
  end
  mover:to(15,0); mover:tick()
  assert(mover.index==(clear and 2 or 1) and s.calls[1].x==(clear and 15 or 7),
    'a nearby spacing point may advance only when onward movement clearance is available and true')
end

do -- ground to keep off is handed to the planner, and the route says whether it could
  local kept_map,kept_game=map,game
  local asked
  local outcome=true
  game={me=function() return {x=0,y=0,area=1,mode=1} end,units=function() return {} end}
  map={invalidate=function() end,path=function(x,y,options)
    asked=options
    return {{x=x,y=y}},outcome
  end}
  map.path_to_level=function(_,options) return map.path(30,0,options) end
  local zones={{x=10,y=0,radius=12}}
  local mover=move.new({avoid=function() return zones end})
  mover:to(30,0)
  assert(asked.avoid==zones and mover.detour and not mover.through,'a route around what is kept off is marked as one')
  outcome=nil
  mover:to_level(2)
  assert(asked.avoid==zones and not mover.detour and mover.through,'a route that could not go around says so')
  zones={}
  mover:to(30,0)
  assert(asked.avoid==nil and not mover.detour and not mover.through,'nothing to keep off is an ordinary route')
  move.new():to(30,0)
  assert(asked.avoid==nil,'a mover without the option plans as before')
  map,game=kept_map,kept_game
end

charon = saved_charon
