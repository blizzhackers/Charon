-- Exhaustion should slow travel to a walk, not stop movement commands.
package.loaded['lib.stamina']=nil
package.loaded['lib.move']=nil
local stamina=require('lib.stamina')
local move=require('lib.move')
local me={x=0,y=0,area=2,mode=1,stamina=0,max_stamina=100}
local commands={}
charon={now=function() return 10000 end,frame=function() return 1 end}
game={me=function() return me end,units=function() return {} end}
map={path=function() return {{x=10,y=0},{x=20,y=0}} end}
act={ready=function() return true end,move=function(x,y,walk) commands[#commands+1]={x=x,y=y,walk=walk} end}
local mover=move.new()
mover:to(20,0); mover:tick()
assert(#commands==1 and commands[1].walk,'zero stamina must still issue walking movement')
me.stamina=100; assert(not stamina.walk(me))
me.stamina=19; assert(stamina.walk(me),'walk before the next segment can exhaust the reserve')
me.stamina=50; assert(stamina.walk(me),'keep walking while rebuilding stamina')
me.stamina=61; assert(not stamina.walk(me),'resume running with a useful reserve')
me.max_stamina=0; assert(not stamina.walk(me),'missing stamina maximum must remain safe')
print('stamina movement regressions passed')

-- Requests wait for acknowledgement and refuse neither town nor dead players.
do
  package.loaded['lib.stamina']=nil
  local stamina=require('lib.stamina')
  local t,uses=10000,0
  local items={{id=80,code='vps',location='container',page=0}}
  charon.now=function() return t end
  game.items=function() return items end
  act.use_item=function() uses=uses+1 end
  me.alive,me.in_town,me.max_stamina,me.stamina=true,true,100,30
  assert(not stamina.upkeep(me) and uses==0,'never drink in town')
  me.in_town,me.alive=false,false
  assert(not stamina.upkeep(me) and uses==0,'never drink while dead')
  me.alive=true
  assert(stamina.upkeep(me) and uses==1,'use before walking threshold')
  t=t+1000; assert(not stamina.upkeep(me) and uses==1,'wait after a refused request')
  t=t+2000; assert(stamina.upkeep(me) and uses==2,'retry a refused potion after cooldown')
  items={}; t=t+100; assert(not stamina.upkeep(me),'disappearance acknowledges use')
  items={{id=81,code='vps',location='container',page=0}}
  t=t+3000; assert(not stamina.upkeep(me) and uses==2,'acknowledged use has a travel cooldown')
  t=t+20000; assert(stamina.upkeep(me) and uses==3)
  me.stamina=100; t=t+100; stamina.upkeep(me)
  me.stamina=30; t=t+3000
  assert(not stamina.upkeep(me) and uses==3,'stamina refill also acknowledges use')
end
print('stamina potion regressions passed')
