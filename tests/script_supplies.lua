-- Inventory supplies use exact codes and never occupy health/mana belt slots.
package.path='scripts/?.lua;'..package.path
package.loaded['lib.services']=nil
package.loaded['lib.loot']=nil
local services=require('lib.services')
local loot=require('lib.loot')
local t,uses,buys,belts=10000,0,{},0
local items={}
local me={name='SuppliesTest',alive=true,in_town=true,gold=500,stash_gold=0,
  life=84,max_life=84,mana=60,max_mana=60,x=0,y=0}
local vendor={id=90}
local stock={
  {id=91,code='hp1'},{id=92,code='hp5'},{id=96,code='mp1'},{id=97,code='mp5'},
  {id=93,code='vps'},{id=94,code='yps'},{id=95,code='vp1'},
}
local saved={}
store={get=function(k) return saved[k] end,set=function(k,v) saved[k]=v end}
charon={now=function() return t end}
game={me=function() return me end,items=function(v) return v and stock or items end,
  ui=function(p) if p then return false end; return {} end}
town={price=function() return 10 end,close=function() end,buy=function(_,item)
  buys[#buys+1]=item.code; me.gold=me.gold-town.price(item)
end}
act={ready=function() return true end,use_item=function() uses=uses+1 end,to_belt=function() belts=belts+1 end}
local function item(id,code)
  return {id=id,code=code,location='container',page=0,width=1,height=1}
end
local function buying(wanted,order)
  local visit=services.new(wanted,order)
  visit.step,visit.akara='buy',vendor
  return visit
end

-- Enter through the public visit path: the vendor quote, not a cash cutoff,
-- decides what a poor character can buy after healing in town.
do
  local old_game,old_town,old_act,old_me=game,town,act,me
  local akara={id=90,class_id=148,x=0,y=0}
  local market={{id=91,code='hp1',price=30}}
  local carried,requests,selected,menu,shop,refuse
  game={me=function() return me end,items=function(v) return v and market or carried end,
    units=function(kind) assert(kind=='monster'); return {akara} end,
    ui=function(name) return name=='npc_menu' and menu or name=='npc_shop' and shop or false end}
  act={ready=function() return true end,
    interact=function(npc)
      assert(npc==akara); menu=true; me.life,me.mana=me.max_life,me.max_mana
    end,
    to_belt=function(potion) potion.location='belt' end}
  town={price=function(potion,npc,mode)
      assert(npc==akara and mode=='buy'); return potion.price
    end,
    trade=function(npc) assert(npc==akara); requests=requests+1; menu,shop=false,true end,
    close=function() menu,shop=false,false end,
    buy=function(npc,potion)
      assert(npc==akara); selected[#selected+1]=potion
      if refuse then return end
      local cash=math.min(me.gold,potion.price)
      me.gold,me.stash_gold=me.gold-cash,me.stash_gold-(potion.price-cash)
      carried[#carried+1]=item(410+#carried,potion.code)
    end}
  local function run(gold,bank,healthy,stocked,refused)
    me={name='PoorRestock',alive=true,in_town=true,gold=gold,stash_gold=bank,
      life=healthy and 84 or 41,max_life=84,mana=60,max_mana=60,x=0,y=0}
    carried={item(401,'mp1'),item(402,'mp1')}
    for _,potion in ipairs(carried) do potion.location='belt' end
    if stocked then
      carried[#carried+1]=item(403,'hp1'); carried[#carried].location='belt'
      carried[#carried+1]=item(404,'hp1'); carried[#carried].location='belt'
    end
    requests,selected,menu,shop,refuse=0,{},false,false,refused
    local visit=services.new({hp=2,mp=2},{'mp','hp'})
    local needed=visit:needs(me)
    local expected_balance
    for i=1,20 do
      local status=visit:tick(me)
      if visit.buy_pending then expected_balance=expected_balance or visit.buy_pending.balance end
      if status=='done' then return visit,needed,expected_balance end
      t=t+1000
    end
    error('low-gold visit did not finish')
  end
  local visit,needed,balance=run(37,0,true)
  assert(needed and requests==1 and #selected==1 and selected[1].code=='hp1' and me.gold==7 and balance==7,
    '37 gold must open trade and buy a 30-gold heal through start/akara/buy')
  run(37,0,false)
  assert(requests==1 and #selected==1 and me.life==84,'healing must not skip the affordable restock')
  visit,needed=run(0,0,true)
  assert(not needed and requests==0 and #selected==0,'healthy penniless character with no loot avoids trade')
  visit,needed=run(37,0,true,true)
  assert(not needed and requests==0 and #selected==0,'healthy stocked character avoids trade')
  market[#market+1]={id=92,code='hp5',price=80}
  market[#market+1]={id=93,code='hp5',price=70}
  run(37,0,true)
  assert(#selected==1 and selected[1]==market[1],'expensive preferred tier must not hide an affordable lower tier')
  visit,needed,balance=run(37,60,true)
  assert(#selected==1 and selected[1]==market[2] and balance==17 and me.gold+me.stash_gold==17,
    'stash funds the preferred tier, equal tiers keep their first quote and pending balance')
  run(29,0,true)
  assert(requests==1 and #selected==0,'no affordable tier sends no purchase request')
  visit=run(37,0,true,false,true)
  assert(requests==1 and #selected==1 and visit.buy_refused.hp and me.gold==37,
    'refused affordable purchase ends the visit without retrying or buying a lower tier')
  game,town,act,me=old_game,old_town,old_act,old_me
end

local visit=buying({hp=1},{'hp'})
visit:tick(me)
assert(buys[1]=='hp5','hp matches potion tiers and selects the best one')
visit=buying({mp=1},{'mp'})
visit:tick(me)
assert(buys[2]=='mp5','mp also matches potion tiers')
visit=buying({vps=1,yps=1})
visit:tick(me)
assert(buys[3]=='vps','default order includes exact supply codes')
t=t+1500; visit:tick(me)
assert(buys[4]=='yps','refused stamina purchase must not starve antidotes')
t=t+1500; visit:tick(me)
assert(#buys==4,'refused supplies are attempted only once per visit')
visit=buying({vps=2},{'vps'})
visit:tick(me)
items={item(20,'vps')}
t=t+1000; visit:tick(me)
assert(#buys==6,'acknowledged purchase can continue toward requested stock')
items[#items+1]=item(21,'vps')
t=t+1000; visit:tick(me)
assert(#buys==6,'stop purchasing once requested stock is present')

items={item(1,'vps'),item(2,'yps')}
visit=buying({vps=1,yps=1})
assert(not visit:short(),'exact codes count carried inventory supplies')
visit.stasher.wanted=function() return false end
visit.step='belt'; visit:tick(me)
assert(belts==0,'inventory supplies must never be moved to the belt')
items={item(1,'junk')}; items[1].width,items[1].height=10,4
visit=buying({vps=4,yps=2})
assert(not visit:short(),'full inventory does not request more supplies')
visit:tick(me)
assert(#buys==6,'full inventory must not buy supplies')
local picker=loot.new()
assert(not picker:wanted({code='vps'},0),'no pickup without inventory room')
items={}
assert(picker:wanted({code='vps'},1) and picker:wanted({code='yps'},1))
for i=1,4 do items[#items+1]=item(i,'vps') end
items[#items+1]=item(5,'yps'); items[#items+1]=item(6,'yps')
assert(not picker:wanted({code='vps'},1) and not picker:wanted({code='yps'},1),'stock limits bound pickup')
assert(not loot.sellable(items[1]) and not loot.sellable(items[5]),'useful stock survives selling')
items[#items+1]=item(7,'vps')
assert(loot.sellable(items[1]),'excess supply stock may be sold')
for _,code in ipairs({'jav','tkf'}) do
  local weapon={code=code,quality=2,width=1,height=3}
  assert(loot.sellable(weapon),'plain vendor weapon is worth selling')
  assert(picker:wanted(weapon,7),'vendor weapon fits while keeping four cells free')
  assert(not picker:wanted(weapon,6),'vendor weapon preserves the four-cell supply budget')
end
assert(not loot.sellable({code='tk1',quality=2,width=1,height=3}),'vendor weapon matching is exact')

-- The first small mana reserve takes priority over configured travel supplies.
items={}
visit=buying({vps=4,mp=10},{'vps','mp'})
visit:tick(me)
assert(buys[#buys]=='mp5','mana reserve precedes stamina even when stamina is ordered first')
items={item(30,'mp1')}; t=t+1000; visit:tick(me)
assert(buys[#buys]=='mp5','fill two reserve slots regardless of potion tier')
items[#items+1]=item(31,'mp1'); t=t+1000; visit:tick(me)
assert(buys[#buys]=='vps','resume configured order once the mana reserve is present')
items={}; visit=buying({vps=4,mp=0},{'vps','mp'})
visit:tick(me)
assert(buys[#buys]=='vps','zero wanted mana must not force a mana purchase')
visit=buying({vps=4,mp=1},{'vps','mp'})
visit:tick(me)
assert(buys[#buys]=='mp5')
items={item(32,'mp1')}; t=t+1000; visit:tick(me)
assert(buys[#buys]=='vps','mana reserve never inflates a one-potion target')
items={}; visit=buying({vps=4,mp=10},{'vps','mp'})
visit:tick(me)
local reserve_buys=#buys
t=t+1000; visit:tick(me)
assert(#buys==reserve_buys,'refused reserve requests must not spend the budget on optional stock')
local price=town.price
town.price=function(i) return i.code:sub(1,2)=='mp' and 600 or 10 end
visit=buying({vps=4,mp=10},{'vps','mp'}); visit:tick(me)
assert(#buys==reserve_buys,'unaffordable reserve saves cash instead of buying optional stock')
town.price=price

-- Scarce cash buys the missing attack/heal reserve before scrolls or stamina.
do
  local buy=town.buy
  stock[#stock+1]={id=98,code='tsc'}
  town.price=function(i)
    if i.code=='tsc' then return 100 end
    if i.code=='vps' then return 25 end
    if i.code:sub(1,2)=='mp' then return 60 end
    return 30
  end
  town.buy=function(_,potion)
    buys[#buys+1]=potion.code
    me.gold=me.gold-town.price(potion)
    items[#items+1]=item(100+#items,potion.code)
  end
  me.gold=47; items={item(41,'hp1'),item(42,'hp1'),item(43,'vps'),item(44,'vps')}
  visit=buying({mp=10,hp=4,vps=4},{'mp','vps','hp'})
  local before=#buys; visit:tick(me)
  assert(#buys==before and me.gold==47,'47 gold with an empty mana belt must be saved, not spent on stamina')
  items={item(40,'mp1'),item(41,'vps'),item(42,'vps'),item(43,'vps')}
  me.gold=150
  visit=buying({mp=10,hp=4,vps=4,yps=2,tsc=1},{'vps','mp','hp','yps'})
  local n=#buys
  for i=1,3 do visit:tick(me); t=t+1000 end
  assert(buys[n+1]=='tsc' and buys[n+2]=='hp5' and #buys==n+2,
    'a way home is bought first, then what reserve the rest affords, before optional supplies')
  assert(me.gold==20,'the scroll and one heal must leave the expected budget')
  items={item(60,'mp1'),item(61,'mp1'),item(62,'hp1'),item(63,'hp1')}
  me.gold=250
  visit=buying({mp=10,hp=4,vps=4,tsc=1},{'mp','vps','hp'})
  n=#buys
  for i=1,4 do visit:tick(me); t=t+1000 end
  assert(buys[n+1]=='tsc' and buys[n+2]=='mp5' and buys[n+3]=='mp5' and buys[n+4]=='vps',
    'after basic reserves and a portal, campaign cash buys mana before optional stamina')
  items={}; me.gold=150
  visit=buying({hp=4,vps=4,tsc=1},{'vps','hp'})
  n=#buys
  visit:tick(me); t=t+1000; visit:tick(me); t=t+1000
  assert(buys[n+1]=='tsc' and buys[n+2]=='hp5','melee callers buy the scroll, then healing without forced mana')
  items={item(50,'junk')}; items[1].width,items[1].height=10,4
  me.gold=500; visit=buying({mp=2,hp=2,tsc=1},{'mp','hp'})
  n=#buys; visit:tick(me)
  assert(#buys==n,'full inventory must not repeat reserve or scroll purchases')
  town.price,town.buy=price,buy
end

-- Trade pacing follows ownership acknowledgements, with one request in flight.
do
  items={}; me.gold=500
  visit=buying({vps=2,yps=1},{'vps','yps'})
  local n=#buys
  visit:tick(me)
  for i=1,9 do t=t+100; visit:tick(me) end
  assert(#buys==n+1 and not visit.buy_refused.vps,'late purchase reply is neither duplicated nor refused early')
  items={item(200,'vps')}
  t=t+100; visit:tick(me)
  assert(#buys==n+2 and buys[#buys]=='vps','late acknowledged purchase continues toward stock')
  items[#items+1]=item(201,'vps')
  t=t+239; visit:tick(me)
  assert(#buys==n+2,'immediate purchase acknowledgement still respects minimum cadence')
  t=t+1; visit:tick(me)
  assert(#buys==n+3 and buys[#buys]=='yps','acknowledged purchase permits the next kind after 240ms')
  -- Another kind arriving, or a changed gold balance, cannot acknowledge yps.
  items[#items+1]=item(202,'vps'); me.gold=490
  t=t+1499; visit:tick(me)
  assert(#buys==n+3 and visit.buy_pending,'only the requested carried kind acknowledges a purchase')
  t=t+1; visit:tick(me)
  assert(#buys==n+3 and visit.buy_refused.yps and visit.step=='belt','refused purchase finishes finitely without duplicate stock')
  t=t+100; visit:tick(me)
  assert(visit.step=='belt','closing the shop keeps the ordinary 700ms delay')

  items={}; visit=buying({mp=2,vps=4},{'vps','mp'})
  n=#buys; visit:tick(me)
  t=t+1499; visit:tick(me)
  assert(#buys==n+1 and visit.buy_pending,'missing reserve waits for acknowledgement')
  t=t+1; visit:tick(me)
  assert(#buys==n+1 and visit.buy_refused.mp and visit.step=='belt','refused reserve preserves optional supply budget')

  local sells={}
  town.sell=function(_,sold) sells[#sells+1]=sold.id; me.gold=me.gold+town.price(sold); return true end
  local function sale(id)
    local result=item(id,'jav'); result.quality=2; result.height=3; return result
  end
  items={sale(210),sale(211),sale(212)}
  visit=buying({},{}); visit.step='sell'
  visit:tick(me)
  for i=1,9 do t=t+100; visit:tick(me) end
  assert(#sells==1 and visit.step=='sell','request success cannot acknowledge a sale or skip remaining loot')
  table.remove(items,1)
  t=t+100; visit:tick(me)
  assert(#sells==2 and sells[2]==211,'late sale acknowledgement permits the next item')
  table.remove(items,1)
  t=t+239; visit:tick(me)
  assert(#sells==2,'immediate sale acknowledgement respects minimum cadence')
  t=t+1; visit:tick(me)
  assert(#sells==3 and sells[3]==212,'acknowledged sale permits the next item after 240ms')
  t=t+1499; visit:tick(me)
  assert(#sells==3 and visit.sell_pending,'sale is not refused before its acknowledgement deadline')
  items[#items+1]=sale(213)
  t=t+1; visit:tick(me)
  assert(#sells==4 and sells[4]==213 and visit.sell_refused[212],'refused sale skips only that identity')
  t=t+1500; visit:tick(me)
  assert(#sells==4 and visit.step=='belt','refused sales finish finitely without repeat requests')
  town.sell=nil

  -- The visit timeout must still settle the stash-owned cursor before closing.
  local closes=0
  local close=town.close
  town.close=function() closes=closes+1 end
  visit=buying({},{}); visit.started=t-90001
  local settled=false
  visit.stasher.settle=function(_,_,abandon)
    assert(abandon); if not settled then return 'busy' end
  end
  assert(visit:tick(me)=='busy' and closes==0,'timeout waits for owned cursor recovery')
  settled=true
  assert(visit:tick(me)=='done' and closes==1,'timeout closes only after cursor recovery')
  town.close=close
end

-- The real high-resolution clock advances between calls within one Lua tick.
do
  local clock=charon.now
  charon.now=function() t=t+0.01; return t end
  items={}; me.gold=500
  visit=buying({vps=2},{'vps'})
  local n=#buys; visit:tick(me)
  items={item(220,'vps')}; t=t+241; visit:tick(me)
  assert(#buys==n+2,'fractional advancing clocks must permit acknowledged purchases after 240ms')
  items={item(221,'mp1'),item(222,'mp1'),item(223,'hp1'),item(224,'hp1')}
  visit=buying({mp=3,hp=2,tsc=1},{'mp','hp'})
  n=#buys; visit:tick(me)
  assert(buys[n+1]=='tsc')
  items[#items+1]=item(225,'tsc'); t=t+241; visit:tick(me)
  assert(#buys==n+2 and buys[n+2]=='mp5','scroll acknowledgement permits the next request after 240ms with a live clock')
  local sells=0
  town.sell=function(seller,sold) sells=sells+1; me.gold=me.gold+town.price(sold,seller,'sell') end
  items={item(226,'jav'),item(227,'jav')}
  visit=buying({},{}); visit.step='sell'; visit:tick(me)
  table.remove(items,1); t=t+241; visit:tick(me)
  assert(sells==2,'sale acknowledgement permits the next request after 240ms with a live clock')
  town.sell=nil; charon.now=clock
end

-- Item and balance replies arrive independently; do not spend stale vendor gold.
do
  local buy,price=town.buy,town.price
  local requests,sales=0,0
  town.price=function() return 60 end
  town.buy=function() requests=requests+1 end
  town.sell=function() sales=sales+1 end
  items={}; me.gold,me.stash_gold=51,60
  visit=buying({mp=2},{'mp'})
  visit:tick(me)
  items={item(230,'mp1')}
  t=t+700; visit:tick(me)
  assert(requests==1 and visit.buy_pending and visit.step=='buy','inventory-first purchase must wait for its debit')
  t=t+500; visit:tick(me)
  assert(requests==1,'stale combined balance must not fund a second mana potion')
  me.stash_gold=0
  t=t+100; visit:tick(me)
  assert(requests==1 and visit.step=='belt','updated carried plus stash balance decides actual affordability')

  -- A debit larger than the quote also settles; equality is unnecessary.
  items={}; me.gold=200
  visit=buying({vps=2},{'vps'}); visit:tick(me)
  items={item(231,'vps')}; me.gold=139
  t=t+700; visit:tick(me)
  assert(requests==3,'a balance below the expected post-purchase amount permits progress')

  items={item(232,'jav')}; me.gold=1
  visit=buying({mp=1},{'mp'}); visit.step='sell'; visit:tick(me)
  items={}
  t=t+700; visit:tick(me)
  assert(sales==1 and requests==3 and visit.step=='sell','inventory-first sale must wait before affordability checks')
  me.stash_gold=61
  t=t+100; visit:tick(me)
  assert(requests==4 and visit.step=='buy','sale credit into stash, including a larger balance, funds the next purchase')

  items={}; me.gold,me.stash_gold=200,0
  visit=buying({vps=2},{'vps'}); visit:tick(me)
  items={item(233,'vps')}
  t=t+1499; visit:tick(me)
  assert(requests==5 and visit.buy_pending,'received item waits within the existing balance deadline')
  t=t+1; visit:tick(me)
  assert(requests==5 and visit.step=='belt' and not visit.buy_refused.vps and not visit.buy_pending,
    'missing debit ends trading finitely without calling the received purchase refused')

  items={item(234,'jav'),item(235,'jav')}
  visit=buying({mp=1},{'mp'}); visit.step='sell'; visit:tick(me)
  table.remove(items,1)
  t=t+1499; visit:tick(me)
  assert(sales==2 and visit.sell_pending,'received sale waits for its credit')
  t=t+1; visit:tick(me)
  assert(sales==2 and requests==5 and visit.step=='belt' and not visit.sell_refused[234] and not visit.sell_pending,
    'missing credit ends trading without another sale or stale affordability check')

  town.price=function() return 0 end
  items={}; visit=buying({vps=2},{'vps'}); visit:tick(me)
  items={item(236,'vps')}; t=t+700; visit:tick(me)
  assert(requests==7,'zero-price purchase needs no balance change')
  items={item(237,'jav'),item(238,'jav')}
  visit=buying({},{}); visit.step='sell'; visit:tick(me)
  table.remove(items,1); t=t+700; visit:tick(me)
  assert(sales==4,'zero-price sale needs no balance change')
  town.buy,town.price,town.sell=buy,price,nil
end

items={item(10,'yps'),item(11,'yps'),item(12,'yps')}
assert(not services.prepare_antidote(me) and uses==0,'no town antidote use')
me.in_town=false
assert(services.prepare_antidote(me) and uses==1)
t=t+100; assert(services.prepare_antidote(me) and uses==1,'wait for acknowledgement')
table.remove(items,1)
package.loaded['lib.services']=nil
services=require('lib.services')
t=t+1500; assert(services.prepare_antidote(me) and uses==2,'reload retains pending acknowledgement')
table.remove(items,1)
t=t+1500; assert(not services.prepare_antidote(me) and uses==2,'at most two acknowledged doses')
for i=1,20 do t=t+1000; assert(not services.prepare_antidote(me)) end
assert(uses==2,'do not consume every tick after preparation')
t=t+60000
local before=uses
for i=1,12 do services.prepare_antidote(me); t=t+1500 end
assert(uses-before<=6 and not services.prepare_antidote(me),'refused use has cooldown and bounded total wait')
me.alive=false; t=t+60000
assert(not services.prepare_antidote(me),'dead players cannot prepare')
do
  local t,escapes=10000,0
  charon={now=function() return t end}
  game={items=function() return {} end,ui=function(name)
    if name then return name=='quest_log' end
    return {quest_log=true}
  end}
  act={key=function() escapes=escapes+1 end}
  local visit=require('lib.services').new()
  visit.step='belt'
  visit.stasher={wanted=function() return false end}
  assert(visit:tick({in_town=true})=='done' and escapes==0,
    'quest notification alone must not open the pause menu')
end
do
  local world=require('lib.world')
  local carried={}
  for id=1,4 do carried[id]={id=id,code='hp1',location='belt'} end
  carried[5]={id=5,code='mp1',location='container',page=0}
  game.items=function() return carried end
  game.ui=function() return false end
  act.to_belt=function() error('full starter belt must not receive a transfer') end
  assert(world.starter_belt_full())
  local visit=services.new()
  visit.step='belt'; visit.stasher={wanted=function() return false end}
  assert(visit:tick({in_town=true})=='done','full starter belt must not delay leaving town')
  carried[#carried+1]={location='equipped',body_location=8}
  assert(not world.starter_belt_full(),'equipped belt rows must not be guessed from starter capacity')
end
do
  -- Equipped belt capacity is unknown; one refused transfer per kind is enough.
  charon={now=function() return t end}
  local carried={{id=300,location='equipped',body_location=8}}
  for id=301,318 do
    local code=id<307 and 'hp1' or id<313 and 'mp1' or 'rvs'
    carried[#carried+1]=item(id,code)
  end
  game.items=function() return carried end
  game.ui=function() return false end
  local sends={}
  act.to_belt=function(potion) sends[#sends+1]=potion.id end
  local visit=services.new()
  visit.step='belt'; visit.stasher={wanted=function() return false end}
  for i=1,4 do
    local status=visit:tick({in_town=true})
    assert(status==(i==4 and 'done' or 'busy'))
    t=t+700
  end
  assert(#sends==3,'full equipped belt attempts at most one potion of each kind')
  -- A later pass resets kind refusals but keeps each item's retry cooldown.
  visit.step='belt'; visit:tick({in_town=true})
  assert(#sends==4 and sends[4]~=sends[1],'later visit retries the kind without flooding the same item')
  t=t+700
  visit:tick({in_town=true})
  t=t+700
  visit:tick({in_town=true})
  t=t+700
  assert(visit:tick({in_town=true})=='done')
  visit.step='belt'; visit:tick({in_town=true})
  assert(sends[#sends]==sends[1],'original item becomes eligible again after its retry cooldown')

  carried={{id=320,location='equipped',body_location=8},item(321,'hp1'),item(322,'hp1')}
  sends={}
  visit=services.new(); visit.step='belt'; visit.stasher={wanted=function() return false end}
  visit:tick({in_town=true})
  t=t+300; visit:tick({in_town=true})
  assert(#sends==1 and visit.belt_pending,'fresh ticks wait for the belt receipt')
  carried[2].location='belt'
  t=t+399; visit:tick({in_town=true})
  assert(#sends==1,'delayed belt receipt still respects action cadence')
  t=t+1; visit:tick({in_town=true})
  assert(#sends==2 and sends[2]==322,'successful belt receipt permits the next transfer')
  carried[3].location='belt'
  t=t+700
  assert(visit:tick({in_town=true})=='done' and #sends==2,'successful transfers finish once inventory potions are gone')
end
do
  game.items=function() return {} end
  game.ui=function() return false end
  local visit=services.new({},{})
  local stashed=0
  visit.stasher={wanted=function() return true end,tick=function() stashed=stashed+1; return 'done' end}
  visit.talk=function() error('a stash-only visit must not walk to Akara') end
  assert(visit:tick({in_town=true,life=100,max_life=100,mana=100,max_mana=100,gold=500})=='done' and stashed==1)
end
print('supply regressions passed')
