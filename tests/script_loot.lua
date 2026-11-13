-- Vendor value broadens income without selling the supplies that keep us alive.
package.loaded['lib.loot']=nil
local loot=require('lib.loot')
local t,quotes,price=10000,0,250
charon={now=function() return t end}
game={items=function() return {} end}
town={sell_value=function() quotes=quotes+1; return price end}
local picker=loot.new()
local gem={id=100,class_id=1,code='gcb',quality=2,width=1,height=1,identified=true}
assert(loot.sellable(gem) and picker:wanted(gem,5),'a valuable chipped gem must fund potions')
assert(quotes==1,'repeated decisions reuse the short-lived quote')
local weapon={id=101,class_id=2,code='axe',quality=4,width=2,height=3}
price=600
assert(loot.sellable(weapon) and picker:wanted(weapon,10),'valuable large gear is not rejected by a four-cell whitelist')
assert(not picker:wanted(weapon,9),'vendor loot must retain room for supplies')
price=100; t=t+1001
assert(not loot.sellable(weapon),'low value per occupied cell must not consume inventory space')
do
  local me={class='sorceress',level=2,gold=14}
  game.me=function() return me end
  local armor={id=102,class_id=3,code='lea',quality=2,width=2,height=3}
  price=240
  assert(loot.sellable(armor) and picker:wanted(armor,10),'an underfunded opening can sell leather armor for four mana potions')
  me.gold=300
  assert(not loot.sellable(armor),'available funds restore the normal value-per-cell filter')
  me.gold=0; me.level=5
  assert(not loot.sellable(armor),'cheap opening loot must not clutter the later route')
  me.level=2
  game.items=function() return {{code='mp1',location='belt'},{code='mp1',location='belt'},
    {code='mp1',location='container',page=0},{code='mp1',location='container',page=0}} end
  assert(not loot.sellable(armor),'carried mana reserves also end the cheap-loot exception')
  armor.location,armor.page='container',0
  me.level=5; me.gold=5000
  assert(loot.sellable(armor),'funding the reserve must not strand already-carried opening loot unsold')
  game.me=nil; game.items=function() return {} end
end
price=1000; weapon.identified=true
assert(loot.sellable(weapon),'identification changes must refresh the quote immediately')
price=0; t=t+1001
assert(not loot.sellable(weapon),'a zero quote is a valid rejection, not a missing answer')
price=5000
for _,code in ipairs({'mp1','hp5','rvs','rvl','r01','r33','gld','tsc','isc'}) do
  assert(not loot.sellable({id=200,code=code,width=1,height=1}),'protected supplies/runes must never become vendor loot: '..code)
end
assert(picker:wanted({code='tsc'},1),'a free portal scroll avoids a purchase')
game.items=function() return {{code='tsc',location='container',page=0}} end
assert(not picker:wanted({code='tsc'},1),'portal pickup is bounded')
local carried = {}
game.items=function() return carried end
for _,family in ipairs({{prefix='hp',codes={'hp1','hp2','hp3','hp4','hp5'},cap=4},
  {prefix='mp',codes={'mp1','mp2','mp3','mp4','mp5'},cap=10},
  {prefix='rv',codes={'rvs','rvl'},cap=4}}) do
  carried = {
    {code=family.codes[1],location='container',page=1},
    {code=family.codes[1],location='equipped'},
    {code=family.codes[1],location='ground'},
    {code=family.prefix=='mp' and 'hp1' or 'mp1',location='container',page=0},
  }
  for i=1,family.cap-1 do
    carried[#carried+1]={code=family.codes[(i-1)%#family.codes+1],location=i==1 and 'belt' or 'container',page=0}
  end
  for _,code in ipairs(family.codes) do
    assert(picker:wanted({code=code},1),'below cap must count only carried same-family potions: '..code)
  end
  carried[#carried+1]={code=family.codes[#family.codes],location='container',page=0}
  for _,code in ipairs(family.codes) do
    assert(not picker:wanted({code=code},40),'all tiers share the carried family cap: '..code)
  end
  assert(picker:wanted({code='gld'},0),'gold is always wanted at potion cap')
end
carried = {{code='hp1',location='belt'},{code='hp1',location='belt'},
  {code='hp1',location='belt'},{code='hp1',location='belt'}}
assert(not picker:wanted({code='mp1'},0),'a full starter belt needs inventory room')
assert(picker:wanted({code='mp1'},1),'inventory room permits pickup with a full starter belt')
carried = {}
assert(picker:wanted({code='hp1'},0),'an available belt does not require inventory space')
town.sell_value=function() return nil end
assert(loot.sellable({id=300,code='jav',quality=2,width=1,height=3}),'unavailable quotes retain the known early vendor items')
local ground, visible = {}, true
game.units=function() return ground end
map={line_of_sight=function() return visible end}
town.sell_value=function(item) return item.price end
local traveler=loot.new(6)
local me={x=0,y=0}
local near={id=400,code='rin',location='ground',x=5,y=0,price=120}
local rich={id=401,code='rin',location='ground',x=10,y=0,price=600}
ground={near,rich}
assert(traveler:nearby(me)==near,'cheap nearby vendor loot remains eligible')
ground={rich}
assert(not traveler:nearby(me),'valuable radius expansion is opt-in')
traveler.valuable_radius=12
for _,distance in ipairs({8,10,12}) do
  rich.x=distance
  assert(traveler:nearby(me)==rich,'visible valuable drops inside expanded radius must be collected')
end
rich.x=13
assert(not traveler:nearby(me),'valuable drops beyond expanded radius are excluded')
rich.x=10; visible=false
assert(not traveler:nearby(me),'walls prevent valuable radius detours')
visible=true
for _,item in ipairs({
  {id=402,code='rin',price=599}, {id=403,code='rin'},
  {id=404,code='hp1',price=600}, {id=405,code='r01',price=600},
  {id=406,code='vps',price=600}, {id=407,code='tsc',price=600},
  {id=408,code='gld',price=600},
  {id=409,code='axe',price=600,width=3,height=4},
}) do
  item.location,item.x,item.y='ground',10,0
  ground={item}
  assert(not traveler:nearby(me),'low/unknown value and protected supplies do not expand pickup range: '..item.code)
end
ground={rich}
carried={{location='container',page=0,width=9,height=4}}
assert(not traveler:nearby(me),'valuable detours still preserve free supply cells')
carried={}; ground={rich,near}
assert(traveler:nearby(me)==near,'expanded radius retains nearest valid selection')
traveler.ignored[near.id]=t+1000
assert(traveler:nearby(me)==rich,'ignore times remain effective with expanded radius')
do
  package.loaded['lib.loot']=nil
  local quick=require('lib.loot')
  local picker,gatherer=quick.new(),quick.new(30)
  local requests,moves={},0
  local accepted=true
  me={x=0,y=0,alive=true,can_act=false,mode=10,stamina=20,max_stamina=20}
  carried={}
  t=20000
  game.me=function() return me end
  act={ready=function() error('nearby pickups must not wait for casting/movement readiness') end,
    pickup=function(item,cursor,nearby)
      assert(cursor==false and nearby==true,'quick grabs require native nearby-only validation')
      requests[#requests+1]=item.id
      return accepted
    end}
  picker.mover.stop=function() error('quick grabs must not stop the movement owner') end
  picker.mover.to=function() error('quick grabs must not plan a route') end
  picker.mover.tick=function() error('quick grabs must not advance a movement owner') end
  local gold={id=500,code='gld',location='ground',x=2,y=0}
  local junk={id=501,code='axe',location='ground',x=1,y=0,price=0}
  ground={junk,gold}
  assert(picker:grab(me) and requests[1]==gold.id,'casting must allow a filtered nearby gold pickup')
  assert(picker:nearby(me)==gold and picker:tick(me)=='looting',
    'pending pickup cooldown must not make a final gather declare the last drop gone')
  assert(not gatherer:grab(me) and #requests==1,'normal and gather instances share pickup pacing')
  local potion={id=502,code='mp1',location='ground',x=2.5,y=0}
  ground={gold,potion}; t=t+501
  assert(gatherer:grab(me) and requests[2]==potion.id,'a pending pile must not starve another useful item')
  ground={gold}; t=t+1000
  assert(picker:grab(me),'an unacknowledged pickup becomes retryable after its bounded cooldown')
  gold.id=503; gold.x=3.01; t=t+501
  assert(not picker:grab(me) and #requests==3,'quick grabs must never approach an out-of-range item')
  gold.x=2; accepted=false
  assert(not picker:grab(me) and requests[4]==gold.id,'native blocked/range refusals must not be reported as sent')
  assert(picker:nearby(me)==gold and picker:tick(me)=='looting',
    'a native pickup refusal must remain visible to collection completion checks')
  t=t+40; ground={gold,potion}; potion.id=504; accepted=true
  assert(picker:grab(me) and requests[5]==potion.id,'a refused nearby item must yield to another candidate')
  t=t+2000; ground={gold}
  for _,field in ipairs({'cursor_item','in_town'}) do
    me[field]=true
    assert(not picker:grab(me) and #requests==5,'owned UI/cursor work excludes opportunistic pickups')
    me[field]=nil
  end
  me.alive=false
  assert(not picker:grab(me) and #requests==5,'dead players must not pick up items')
  me.alive=true
  local ring={id=505,code='rin',location='ground',x=6,y=0,price=120}
  ground={ring}
  local safe,ready=true,true
  local function safe_item(item) assert(item==ring); return safe end
  act.ready=function() return ready end
  act.move=function(x,y) assert(math.abs(x*x+y*y-2.5^2)<0.001,'loot walks must commit only a short step'); moves=moves+1; return true end
  assert(picker:approach(me,safe_item) and moves==1,'a safe simple approach may take priority over discretionary combat')
  t=t+40
  assert(picker:approach(me,safe_item) and moves==1,'approach requests must be paced')
  ring.id=506; ring.x=0; ring.y=6
  assert(picker:approach(me,safe_item) and moves==2,'a new safe target must immediately replace the old movement direction')
  safe=false
  assert(not picker:approach(me,safe_item) and moves==2,'new danger must release approach ownership immediately')
  safe=true; ready=false; t=t+200
  assert(not picker:approach(me,safe_item) and moves==2,'walking to loot must not interrupt an unfinished cast')
  ring.x=2; ring.y=0
  assert(picker:approach(me,safe_item) and requests[6]==ring.id,'a valid nearby item can be grabbed without waiting for that cast')
  ring.id=507; ring.x=6; ready=true; t=t+2000
  assert(picker:approach(me,safe_item))
  t=t+12001
  assert(not picker:approach(me,safe_item) and picker.ignored[ring.id]>t,
    'an approach that never succeeds must stop retrying finitely')
end
do
  package.loaded['lib.loot']=nil
  local pacing=require('lib.loot')
  local picker,other=pacing.new(),pacing.new(30)
  local requests,proofs,fallbacks,accepted=0,0,0,true
  local safe=true
  t=80000
  me={x=0,y=0,alive=true,mode=10,gold=0}
  carried={}
  game.me=function() return me end
  act={ready=function() error('in-reach pacing must not need animation readiness') end,
    move=function() error('an in-reach pickup must not issue movement') end,
    pickup=function() requests=requests+1; return accepted end}
  local first={id=810,code='gld',location='ground',x=1,y=0}
  local second={id=811,code='gld',location='ground',x=2,y=0}
  local third={id=812,code='gld',location='ground',x=2,y=0}
  ground={first}
  t=100
  assert(not picker:approach(me,function() return true end) and requests==0,
    'initial clock pacing without a successful pickup must not claim ownership')
  t=80000
  assert(other:grab(me) and requests==1,'another pickup instance must start the shared successful-pickup pacing')
  local function reflex_tick()
    if not picker:approach(me,function() proofs=proofs+1; return safe end) then fallbacks=fallbacks+1 end
  end
  ground={second}
  for offset=40,480,40 do t=80000+offset; reflex_tick() end
  t=80499; reflex_tick()
  assert(fallbacks==0 and requests==1 and proofs==13,
    'each fresh safe in-reach candidate must own paced ticks without pickup packets or fallback route movement')
  t=80500; reflex_tick()
  assert(fallbacks==0 and requests==2,'the next pickup must resume at the unchanged 500 ms boundary')
  t=80540; reflex_tick()
  assert(fallbacks==1 and requests==2,'an unconfirmed item must yield rather than owning its receipt retry delay')
  ground={third}; safe=false; t=80580; reflex_tick()
  assert(fallbacks==2 and requests==2,'new danger must release ownership even while shared pacing is active')
  ground={}; t=80620; reflex_tick()
  assert(fallbacks==3,'a missing candidate must release ownership during pacing')
  ground={{id=813,code='axe',location='ground',x=1,y=0,price=0}}; t=80660; reflex_tick()
  assert(fallbacks==4,'an unwanted item must not hold pickup ownership')
  ground={third}; safe=true
  for _,field in ipairs({'alive','in_town','cursor_item'}) do
    me[field]=field~='alive'
    reflex_tick()
    me[field]=field=='alive' or nil
  end
  assert(fallbacks==7 and requests==2,'death, town and cursor ownership must still exclude the paced pickup')
  t=81000; accepted=false; reflex_tick()
  assert(fallbacks==8 and requests==3,'a genuine native refusal must yield instead of claiming all pickup failures')
  t=81040; reflex_tick()
  assert(fallbacks==9 and requests==3,'a refused pending identity must continue yielding during its retry delay')
  ground={{id=814,code='gld',location='ground',x=2,y=0}}; accepted=true; reflex_tick()
  assert(fallbacks==9 and requests==4,'a refusal must not start the successful-pickup pacing for another fresh item')
end

-- Requests require ownership receipts; missing ground units and gold deltas are not ID receipts.
do
  package.loaded['lib.loot']=nil
  local receipts=require('lib.loot')
  local picker,other=receipts.new(),receipts.new()
  local requests,logs,frame,reads=0,{},0,0
  local original_print=print
  print=function(line) logs[#logs+1]=line end
  t=100000
  me={x=0,y=0,mode=10,alive=true,gold=0}
  carried={}
  game.me=function() return me end
  game.items=function() reads=reads+1; return carried end
  local resolved={}
  game.unit=function(kind,id) assert(kind=='item'); return resolved[id] end
  charon.frame=function() return frame end
  act.pickup=function(item,cursor,nearby)
    assert(not cursor and nearby,'receipt tracking must preserve nearby casting/running pickup')
    requests=requests+1
    return true
  end
  local function advance(ms) t=t+ms; frame=frame+1 end
  local function logged(text)
    for _,line in ipairs(logs) do if line:find(text,1,true) then return true end end
    return false
  end
  local potion={id=600,code='mp1',location='ground',x=2,y=0}
  ground={potion}
  assert(picker:grab(me))
  ground={}; advance(40)
  other:nearby(me)
  assert(not logged('ack carried'),'ground disappearance cannot confirm an acquisition')
  -- A tracked carried unit could belong to another TCP/IP player.
  resolved[600]={id=600,code='mp1',location='belt'}
  advance(40); other:nearby(me)
  assert(not logged('ack carried id=600'),'another player carrying a tracked item is not our receipt')
  carried={resolved[600]}
  advance(40); other:nearby(me)
  assert(logged('ack carried id=600'),'membership in our inventory confirms the delayed receipt')
  carried={}

  local ring={id=601,code='rin',location='ground',x=2,y=0,price=120}
  advance(1500); ground={ring}; assert(other:grab(me))
  carried={{id=601,code='rin',location='container',page=0}}
  ground={}; advance(40); picker:nearby(me)
  assert(logged('ack carried id=601'),'inventory identity confirms a pickup across instances')
  carried={}
  -- A delayed receipt is still useful after the first retry window has elapsed.
  local late={id=607,code='mp1',location='ground',x=2,y=0}
  advance(1500); ground={late}; assert(picker:grab(me))
  advance(1500); ground={}; picker:nearby(me)
  carried={{id=607,code='mp1',location='belt'}}
  advance(40); picker:nearby(me)
  assert(logged('ack carried id=607'),'late carried receipts remain valid after an unconfirmed timeout')
  carried={}

  local failed={id=602,code='mp1',location='ground',x=2,y=0}
  advance(1500); ground={failed}; assert(picker:grab(me))
  for attempt=2,3 do advance(1500); assert(other:grab(me)) end
  local before=requests
  advance(1500)
  assert(not picker:grab(me) and not other:nearby(me) and other:tick(me)=='done',
    'three unconfirmed attempts must suppress the drop for all pickup modes and instances')
  assert(requests==before and logged('timeout suppressed id=602'))
  local alternative={id=603,code='mp1',location='ground',x=2.5,y=0}
  ground={failed,alternative}
  assert(other:grab(me) and requests==before+1,'suppressed nearest drop must yield to an alternative')
  advance(60001); ground={failed}
  assert(picker:grab(me),'suppression must expire to allow a later retry')

  -- A cursor is not inventory ownership, and pickup restrictions still hold.
  resolved[602]={id=602,code='mp1',location='cursor'}
  advance(40); me.cursor_item=true
  assert(not picker:grab(me) and not picker:approach(me,function() return true end))
  assert(not logged('ack carried id=602'),'cursor identity is not a completed pickup')
  me.cursor_item=nil

  local first={id=604,code='gld',location='ground',x=2,y=0}
  local second={id=605,code='gld',location='ground',x=2.5,y=0}
  advance(1500); ground={first,second}; assert(picker:grab(me))
  advance(500); assert(other:grab(me))
  me.gold=37; ground={}; advance(40)
  picker:nearby(me)
  assert(logged('gold delta=37 pending=2') and not logged('ack carried id=604') and not logged('ack carried id=605'),
    'gold balance growth with overlapping requests must remain unattributed')
  local scanned=reads
  other:nearby(me); picker:grab(me)
  assert(reads==scanned,'pending ownership must be scanned only once per frame across instances')
  advance(1500); ground={first}
  assert(picker:grab(me),'ambiguous gold evidence must not falsely acknowledge a particular pile')
  -- Native refusal consumes the same finite request budget without pretending it was sent.
  local refused={id=606,code='mp1',location='ground',x=1,y=0}
  act.pickup=function() requests=requests+1; return false end
  ground={refused}; advance(1500)
  for attempt=1,3 do assert(not picker:grab(me)); advance(1500) end
  before=requests
  assert(not other:grab(me) and requests==before and not other:nearby(me),
    'native refusals must also stop after the shared attempt budget')
  assert(logged('request refused id=606') and logged('timeout suppressed id=606'))
  -- Churning failed IDs cannot grow tracking beyond its fixed game-local budget.
  package.loaded['lib.loot']=nil
  local bounded=require('lib.loot').new()
  act.pickup=function() requests=requests+1; return true end
  carried={}; resolved={}; advance(60001)
  for id=700,763 do
    ground={{id=id,code='gld',location='ground',x=2,y=0}}
    assert(bounded:grab(me),'available tracking slots must allow requests')
    advance(500)
  end
  before=requests
  ground={{id=764,code='mp1',location='ground',x=2,y=0}}
  assert(bounded:grab(me) and requests==before+1,
    'bounded tracking must evict absent history so fresh supplies remain collectible')
  assert(not logged('ack carried id=700'),'evicting absent history must not invent a receipt')
  advance(60001)
  assert(bounded:grab(me),'expired tracking records must release capacity')
  charon.frame=nil
  print=original_print
end
print('loot value and opportunistic pickup regressions passed')
