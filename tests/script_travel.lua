-- Dodge headings query the current route without issuing or renewing movement.
local travel = require('lib.travel')
local old_game, old_map, old_act = game, map, act
local me = {x=18.5,y=5.5,area=2}
local calls, door, path = {}, nil, {{x=18,y=5},{x=25,y=3},{x=30,y=0}}
game = {units=function() return door and {door} or {} end}
map = {
  path=function(x,y,options) calls[#calls+1]={'point',x,y,options.step}; return path end,
  path_to_level=function(level,options) calls[#calls+1]={'level',level,options.step}; return path end,
  line_of_sight=function() return false end,
}
act = setmetatable({}, {__index=function() error('heading query must not issue actions') end})
local trip = travel.new()
trip.status, trip.target = 'moving', 3
trip.hop = {from=2,to=3,kind='warp',x=30,y=0}
local mover = trip.mover
mover.status, mover.path, mover.index = 'moving', {{x=10,y=0}}, 1
mover.replans, mover.progress_at, mover.issued, mover.issued_at = 3, 1000, 1, 9000
local old_path = mover.path
local heading = trip:dodge_heading(me)
assert(heading.x==7 and heading.y==-2 and calls[1][1]=='point' and calls[1][2]==30,
  'combat displacement must query from the current position instead of following a stale waypoint backward')
assert(mover.path==old_path and mover.index==1 and mover.replans==3 and mover.progress_at==1000
  and mover.issued==1 and mover.issued_at==9000 and mover.status=='moving',
  'a heading query must preserve movement ownership, retry budget and progress deadline')
path={{x=19,y=6},{x=30,y=0}}
heading=trip:dodge_heading(me)
assert(heading.x==1 and heading.y==1,'the first short corner must not be skipped for a later point')
trip.hop.kind='border'; mover.route_step=3
trip:dodge_heading(me)
assert(calls[#calls][1]=='level' and calls[#calls][2]==3 and calls[#calls][3]==3,
  'border headings must query the next level with the current route spacing')
for _,change in ipairs({
  function() trip.status='failed' end,
  function() trip.status='idle' end,
  function() trip.hop=nil end,
  function() trip.hop.from=4 end,
  function() trip.target=me.area end,
  function() mover.status='failed' end,
  function() mover.door={id=1} end,
  function() trip.hop.kind='warp'; trip.hop.x=me.x+5; trip.hop.y=me.y end,
}) do
  trip.status, trip.target, trip.hop = 'moving', 3, {from=2,to=3,kind='border',x=30,y=0}
  mover.status, mover.door = 'moving', nil
  change()
  local before=#calls
  assert(not trip:dodge_heading(me) and #calls==before,
    'inactive, stale, door-owned or near-warp routes must not query a heading')
end
trip.status, trip.target, trip.hop = 'moving', 3, {from=2,to=3,kind='border',x=30,y=0}
mover.status, mover.door = 'moving', nil
path=nil
assert(not trip:dodge_heading(me),'unreachable exits must yield no heading')
path={{x=18,y=5}}
assert(not trip:dodge_heading(me),'a path containing only the current cell has no heading')
path={{x=25,y=5}}
door={id=1,x=20,y=5,door=true,mode=0}
assert(not trip:dodge_heading(me),'a closed door on the fresh segment must retain interaction ownership')
map.path_to_level=function() error('unloaded map') end
assert(not trip:dodge_heading(me),'a failed read-only path query must leave ordinary dodging available')
-- A route to a stair may cut through a corner of a neighbouring level: keep
-- following it instead of planning again from each side of the open border.
do
  local now=10000
  local saved_now=charon.now
  charon.now=function() return now end
  local player={x=0,y=0,area=2}
  game={me=function() return player end,units=function() return {} end,level_name=function(id) return 'area'..id end}
  local plans,ticks=0,0
  local detour=travel.new()
  detour.mover={status='moving',tick=function(self) ticks=ticks+1; return self.status end,
    to=function(self) plans=plans+1; self.status='moving' end,
    to_level=function(self) plans=plans+1; self.status='moving' end,stop=function() end}
  detour.status,detour.target='moving',8
  detour.hop={from=2,to=8,kind='warp',x=100,y=100,attempts=0,used_at=0}
  player.area=1
  for _=1,20 do now=now+40; assert(detour:tick()=='moving') end
  assert(plans==0 and ticks==20,'a warp route crossing a third level must keep its plan')
  player.area=2
  now=now+40; detour:tick()
  assert(plans==0 and detour.hop.cross_since==nil,'returning to the hop level clears the detour clock')
  player.area=1
  now=now+40; detour:tick()
  now=now+20001
  map.exits=function() return {} end
  detour:tick()
  assert(detour.status=='failed' or plans>0,'an expired detour releases the route to the actual area')
  charon.now=saved_now
end
game, map, act = old_game, old_map, old_act
