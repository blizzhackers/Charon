-- Exercise campaign decisions with a synthetic act graph; no game launch.
-- The fixture's unknown areas are monster level 30; the level gate has its own cases below.
local gated_build = require('lib.builds').sorceress
local saved_enter_gap, saved_tower_prep = gated_build.enter_gap, gated_build.tower_prep
gated_build.enter_gap = nil
gated_build.tower_prep = true -- the optional Tower preparation is exercised here whether or not the build uses it
local saved_gold_farm = gated_build.gold_farm
gated_build.gold_farm = true -- likewise the gold detour
local saved_follow_pay = gated_build.follow_pay
gated_build.follow_pay = false -- the route by level margin is exercised here; following the pay has its own cases below
local saved_by_gold = gated_build.players_by_gold
gated_build.players_by_gold = false -- likewise the hop-by-hop players rule; the count by gold has its own case below
local saved_act_ready = act.ready
act.ready = function() return false end -- travel restock is tested with explicit action readiness below
local saved = {}
local function replace(name, module)
  saved[name] = package.loaded[name]
  package.loaded[name] = module
end
local me = {name='RouteTest',class='sorceress',difficulty=0,level=1,area=2,in_town=false,
  x=0,y=0,gold=1000,stash_gold=0,experience=0,life=100,max_life=100}
local den, states, chosen, threats, route_goal, bot = false, nil, nil, 0, nil, nil
local data = {}
charon={now=function() return 10000 end}
store={get=function(k) return data[k] end,set=function(k,v) data[k]=v end}
game={me=function() return me end,quest=function(id) return {completed=id==1 and den} end,
  unit=function() return nil end,monster_info=function() return nil end,state=function() return false end,
  players=function() return {supported=false} end,
  units=function() return {} end,items=function() return {} end,waypoints=function() return {} end,
  level_info=function(id) return {monster_level=({[2]=1,[3]=2,[25]=7})[id] or 30,name='area'..id} end,
  level_name=function(id) return 'area'..id end}
town={mercenary=function() return nil end}
local fighter={kills=0,off_build=function() return false end,
  known_skills=function() return {['Static Field']={base_level=1,level=1}} end,
  observe_missiles=function() end,
  ground_clear=function() return true end,
  monsters=function() return {} end,
  threat=function() threats=threats+1 end,hostiles=function() return {} end,
  upkeep=function() end,out_of_mana=function() return false end,winning=function() return false end,dry=function() return false end,pack_lethality=function() return 0 end,dodge=function() return false end}
local trip={mover={},to=function(self,goal) chosen=goal end,tick=function() return 'moving' end}
replace('lib.fsm',{new=function(_,s)
  states=s
  bot={state='decide',data={},is=function(self,state) return self.state==state end,
    go=function(self,state) self.state=state end,tick=function() end}
  return bot
end})
replace('lib.combat',{new=function(_,options)
  assert(options==nil,'the campaign must use the sole neural combat policy without an opt-in')
  fighter.neural={version='fixture'}
  return fighter
end})
local travel_options
replace('lib.travel',{new=function(options) travel_options=options or travel_options; return trip end,route=function(from,to)
  route_goal=to
  if from==to then return {} end
  if to==8 then return {{to=8}} end
  if to==37 then return {{to=3},{to=4},{to=5},{to=6},{to=37}} end
  return {{to=to}}
end})
local approach={}
replace('lib.move',{new=function() return approach end})
local reward_talks,last_order,last_wanted=0,nil,nil
local service_needed, service_tick = false, nil
local service={new=function(wanted,order) last_order,last_wanted=order,wanted; return {needs=function() return service_needed end,
  tick=function() if service_tick then service_tick() end; return 'done' end,talk=function(_,_,name)
    assert(name=='Akara'); reward_talks=reward_talks+1; return {}
  end} end}
service.scrolls=function() return 0 end
replace('lib.services',service)
replace('lib.stamina',{upkeep=function() end,walk=function() return false end})
local loot_checks, container_checks, quick_grabs = 0, 0, 0
local safe_approach = false
replace('lib.loot',{new=function() return {
  nearby=function() return false end,
  approach=function() loot_checks=loot_checks+1; return safe_approach end,
  grab=function() quick_grabs=quick_grabs+1; return true end,
} end,
  sellable=function(item) return item.code=='jav' end})
replace('lib.containers',{new=function() return {nearby=function() container_checks=container_checks+1; return false end} end})
local gear_offer, gear_candidate -- what lib/gear's plan and candidate answer in a case that sets them
replace('lib.gear',{new=function() return {plan=function() return gear_offer end,
    candidate=function(_,_,item) return gear_candidate ~= nil and gear_candidate(item) or false end} end,
  room=function() return true end,free_cell=function() return 0,0 end,wearable=function(item) return item.gear==true end})
local exploration_stub
replace('lib.explore',{new=function()
  local explorer=exploration_stub or {}
  explorer.observe=explorer.observe or function() end
  return explorer
end})
local candidates, no_farm
replace('lib.leveling',{hops_from=function() return {[2]=1,[3]=1,[17]=2,[18]=3} end,
  choose=function(_,_,options) candidates=options.candidates; if not no_farm then return candidates[1],'test' end end,
  prior=function() return 1 end,
  record=function() end})
dofile('scripts/game/act1.lua')
assert(last_order[1]=='mp','campaign mana stock must precede optional stamina purchases')
assert(last_wanted.tsc==1,'a town portal scroll is wanted on every visit, before the Den as after it')
me.class='barbarian'
dofile('scripts/game/act1.lua')
assert(last_order[1]=='hp','non-spell builds must retain healing-first purchases')
me.class='sorceress'
dofile('scripts/game/act1.lua')
local function machine()
  return {data={quests_synced=true,waypoints_synced=true,town_done=true},elapsed=function() return 0 end}
end
do
  local old_now,old_act,old_ui,old_revision=charon.now,act,game.ui,game.quest_revision
  local t,revision,panel,requests=10000,11,false,{}
  charon.now=function() return t end
  game.ui=function(name) assert(name=='quests'); return panel end
  game.quest_revision=function() return revision end
  act={quest_log=function(open)
    assert(type(open)=='boolean','quest panel requests must have a boolean desired state')
    requests[#requests+1]=open
    return panel==open
  end}
  local function fresh()
    local m=machine(); local started=t
    m.elapsed=function() return t-started end
    states.sync_quests.enter(m)
    assert(not m.data.quests_synced,'synchronization must discard any earlier ready flag')
    return m
  end
  local function step(m,ms)
    t=t+(ms or 40)
    local before=#requests
    local state=states.sync_quests.tick(m)
    assert(#requests-before<=1,'quest synchronization must act at most once per tick')
    return state
  end
  local m=fresh()
  step(m); assert(requests[#requests]==true,'a closed quest panel must be opened without an NPC trip')
  panel=true
  step(m); assert(#requests==1 and not m.data.quests_synced,'opening the panel is not a reply acknowledgement')
  revision=revision+1 -- A valid fresh reply may contain exactly the same quest bits.
  step(m); assert(requests[#requests]==false and not m.data.quests_synced,'acknowledgement must close the panel before continuing')
  step(m); assert(#requests==2 and not m.data.quests_synced,'a pending close must not finish or close the panel again')
  panel=false
  assert(step(m)=='decide' and m.data.quests_synced,'unchanged quest bits still synchronize after a fresh record arrives')

  panel=true; requests={}; m=fresh()
  step(m); assert(requests[1]==false,'an already-open panel must close before requesting another record')
  panel=false
  step(m); assert(#requests==1,'native quest-panel reopen debounce must be respected')
  step(m,250); assert(requests[2]==true,'reopen after the native debounce must request a fresh record')
  panel=true; revision=revision+1
  step(m); assert(requests[3]==false and not m.data.quests_synced,'an existing panel must still receive a new acknowledgement')
  panel=false
  assert(step(m)=='decide' and m.data.quests_synced)

  requests={}; m=fresh()
  step(m)
  for _=1,2 do step(m,1000); step(m) end
  assert(#requests==3,'a failed quest open should retry at most three times')
  step(m,1000); assert(#requests==3 and not m.data.quests_synced)
  assert(step(m,10001)=='save_exit' and not m.data.quests_synced,
    'a failed request must reset the game instead of trusting stale quest bits')

  requests={}; m=fresh(); step(m); panel=true
  assert(step(m,10001)==nil and requests[2]==false and not m.data.quests_synced,
    'a missing reply must close an open quest panel before resetting')
  panel=false
  assert(step(m)=='save_exit' and not m.data.quests_synced)
  charon.now,act,game.ui,game.quest_revision=old_now,old_act,old_ui,old_revision
end

do
  local m=machine()
  chosen=nil
  assert(states.decide.tick(m)=='travel' and chosen==8,
    'the sorceress heads for the Den at level 1: same monster level as the Moor, and denser')
  states.farm.enter(m)
  assert(#candidates==1 and candidates[1]==2,'leveling before the Den quest must stay in Blood Moor')
  me.level=2; chosen=nil
  assert(states.decide.tick(machine())=='travel' and chosen==8,'Den must be entered at level 2')
  threats=0
  assert(states.farm.tick(m)=='decide' and threats==0,'ding must stop farming before another fight')
end
do
  den=true; me.level=3; chosen=nil
  assert(states.decide.tick(machine())=='travel' and chosen==3,
    'completed Den must allow forward travel without the old level-12 or area-level gate')
  me.level=8; chosen=nil
  assert(states.decide.tick(machine())=='travel' and chosen==3,
    'an incomplete optional Countess quest must not divert a supplied character')
  me.in_town=true; me.area=1; chosen=nil
  assert(states.decide.tick(machine())=='travel','sorceress must not stop to hire a Rogue')
end
do
  me.area=8; den=true
  service.portal_scrolls=function() return 1 end
  local m=machine()
  assert(states.clear.tick(m)=='portal_heal' and m.data.portal_town_only,
    'completed Den should portal to town without planning a return to the cleared cave')
  states.portal_heal.enter(m)
  me.in_town=true; me.area=1
  game.state=function() return false end
  town.close=function() end
  states.portal_heal.tick(m)
  assert(reward_talks==1,'healthy, stocked Den returns must still talk to Akara for the skill point')
  assert(states.portal_heal.tick(m)=='decide','quest return must stay in town after services finish')
end
do
  local level_info, waypoints, items, route = game.level_info, game.waypoints, game.items, package.loaded['lib.travel'].route
  me.level=7; me.area=1; me.in_town=true; me.gold=0
  game.level_info=function(id) return {monster_level=({[2]=1,[3]=2,[4]=4,[25]=7})[id] or 30} end
  game.waypoints=function() return {[3]=true} end
  package.loaded['lib.travel'].route=function(from,to)
    if to==37 then return {{to=2},{to=3},{to=4},{to=37}} end
    return route(from,to)
  end
  for count=0,1 do
    game.items=function()
      return count==1 and {{code='mp1',location='belt'}} or {}
    end
    local m=machine()
    assert(states.decide.tick(m)=='farm','zero or one mana reserve must recover before starting the Tower')
    m.data.farm_commit={area=4,mode='gold'}
    states.farm.enter(m)
    assert(m.data.farm_area~=4,'dry recovery abandons an older farm outside staff range')
    assert(#candidates>0,'poor characters need a reachable recovery area')
    for _,area in ipairs(candidates) do
      assert(game.level_info(area).monster_level<=2,'dry recovery must stay where staff melee is allowed')
    end
  end
  game.items=function() return {{code='mp1',location='belt'},{code='mp1',location='belt'}} end
  assert(states.decide.tick(machine())=='farm','an underfunded bolt sorceress must not spend recovery potions on a Tower detour')
  me.class='barbarian'
  assert(states.decide.tick(machine())=='travel' and chosen==25,'other builds retain the supplied Countess option')
  me.class='sorceress'
  game.items=function() return {} end
  me.in_town=false; me.area=21
  local m=machine(); states.portal_heal.enter(m)
  me.in_town=true; me.area=1; me.gold=0
  assert(states.portal_heal.tick(m)=='decide' and m.data.town_done,
    'an underfunded restock must not return through a hard-area portal')
  assert(data['act1.RouteTest.gold_farm'],'an underfunded restock requests resource recovery')
  me.gold=360
  assert(states.portal_heal.tick(m)=='save_exit',
    'failed purchases with six affordable potions must reset instead of clearing recovery and resuming hard travel')
  me.gold=0
  game.items=function() return {{code='mp1',location='belt'},{code='mp1',location='belt'}} end
  assert(states.portal_heal.tick(m)=='decide',
    'active recovery with two carried potions must not return through a hard-area portal before reaching six')
  game.items=function() return {} end
  me.gold=360
  states.farm.enter(machine())
  for _,area in ipairs(candidates) do
    assert(game.level_info(area).monster_level<=2,'unspent cash cannot replace a missing carried reserve')
  end
  me.gold=0; me.in_town=false; me.area=2
  game.level_info,game.waypoints,game.items,package.loaded['lib.travel'].route=level_info,waypoints,items,route
end
do
  local items, level_info, route = game.items, game.level_info, package.loaded['lib.travel'].route
  den=true; me.area=2; me.in_town=false; me.gold=0
  data['act1.RouteTest.gold_farm']=true
  local carried=2
  game.items=function()
    local result={}
    for i=1,carried do result[i]={code='mp1',location='belt'} end
    return result
  end
  game.level_info=function(id) return {monster_level=id==4 and 4 or 1} end
  local m=machine(); states.farm.enter(m)
  m.data.farm_area=4; m.data.farm_commit={area=4,mode='gold'}
  carried=1; threats=0; chosen=nil
  assert(states.farm.tick(m)=='decide' and not m.data.farm_commit,
    'spending the last travel reserve must abandon a farm outside staff range')
  assert(threats==0 and chosen==nil,'dry recovery must replan before fighting or travelling')
  carried=0; me.gold=360
  assert(states.decide.tick(machine())=='retreat', 'cash for six potions must trigger shopping before forward travel')
  assert(data['act1.RouteTest.gold_farm'],'affordable potions must not clear recovery without carried stock')
  m=machine(); m.data.farm_area=2
  assert(states.farm.tick(m)=='retreat','affordable farm proceeds must be converted into carried potions')
  package.loaded['lib.travel'].route=function(from,to)
    if to==1 then return {{to=3},{to=1}} end
    return route(from,to)
  end
  assert(states.farm.tick(m)=='save_exit','distant recovery proceeds must return through a fresh game')
  package.loaded['lib.travel'].route=route
  me.in_town=true; me.area=1
  m=machine(); m.data.town_done=nil
  assert(states.decide.tick(m)=='town','unspent recovery cash in town must request a service visit')
  m.data.town_done=true
  assert(states.decide.tick(m)=='save_exit','a completed visit with unspent recovery cash must reset instead of looping')
  carried=6; me.gold=0
  assert(states.decide.tick(machine())=='travel' and not data['act1.RouteTest.gold_farm'],
    'six carried potions must end recovery and resume forward progression')
  me.in_town=false; me.area=2
  game.items,game.level_info=items,level_info
end
do
  me.in_town=false; me.area=2; data['act1.RouteTest.gold_farm']=true; me.gold=0
  local m=machine(); states.farm.enter(m)
  for _,area in ipairs(candidates) do assert(area~=17 and area~=18,'resource planning must exclude burial/crypt detours') end
  no_farm=true; m=machine(); states.farm.enter(m)
  assert(states.farm.tick(m)=='save_exit','exhausted resource areas must restart instead of cycling decide/farm')
end
do
  me.alive=true; me.can_act=true; me.id=1; me.mode=1
  game.ready=function() return true end
  fighter.out_of_mana=function() return true end
  fighter.threat=function(_,_,_,accept)
    local straggler={id=100,x=10,y=0,life_percent=100}
    if not accept or accept(straggler) then return straggler end
  end
  bot.state='travel'
  tick()
  assert(bot.state=='travel','a skipped travel target must not trigger an early mana retreat')
  assert(fighter.melee_ok,'poor early characters must retain their melee fallback')
  me.gold=120; tick()
  assert(not fighter.melee_ok,'affordable mana reserves should favor restocking')
  me.gold=0
  game.items=function() return {{code='jav',location='container',page=0}} end
  tick()
  assert(not fighter.melee_ok,'saleable carried loot should fund a restock')
  game.items=function() return {} end
end
do
  local observed
  fighter.out_of_mana=function() return false end
  fighter.threat=function(_,_,_,accept) observed=accept end
  trip.hop={from=2}; trip.mover={path={{x=10,y=0}},index=1}
  bot.state='travel'; me.area=2
  fighter.out_of_mana=function() return true end
  tick()
  assert(observed and observed({x=8,y=1}), 'fight an ordinary forward blocker before running into it')
  for _,flag in ipairs{'unique','superunique','boss','champion','minion'} do
    assert(observed({x=-8,y=0,[flag]=true}), 'fight a nearby rear '..flag..' during travel')
  end
  assert(observed({x=-4,y=0}), 'fight close ordinary enemies even behind the route')
  assert(not observed({x=-8,y=0}) and not observed({x=8,y=6}), 'do not chase rear or lateral stragglers')
  local old_pack=fighter.pack_lethality
  fighter.pack_lethality=function() return 0.5 end
  assert(not observed({x=-8,y=0,unique=true}) and not observed({x=8,y=6,champion=true}),
    'an elite whose pack would take half our life is not sought out on the way')
  assert(observed({x=-4,y=0,unique=true}) and observed({x=8,y=1,unique=true}),
    'the same elite is still fought when it is on us or in the way')
  fighter.pack_lethality=old_pack
  fighter.out_of_mana=function() return false end
  trip.hop=nil
end
do
  local original_threat, original_hostiles, original_trip = fighter.threat, fighter.hostiles, trip.tick
  local advances = 0
  fighter.threat=function() return nil end -- rear enemies deliberately skipped by travel
  fighter.hostiles=function() return {{id=50,x=-8,y=0}} end
  trip.tick=function() advances=advances+1; return 'moving' end
  me.in_town=false; me.area=2; fighter.target_id=nil
  loot_checks,container_checks=0,0
  states.travel.tick(machine())
  assert(advances==1 and loot_checks==1 and container_checks==0,
    'a declined safe-loot approach must keep travel moving past skipped threats without opening barrels')
  fighter.hostiles=function() return {} end
  states.travel.tick(machine())
  assert(advances==2 and loot_checks==2 and container_checks==1,
    'travel resumes loot and container checks once nearby threats are gone')
  do -- with nothing to fight with, the walk goes on past what stands near
    local fought,old_fight,old_dry,old_melee=0,fighter.fight,fighter.dry,fighter.melee_ok
    fighter.melee_ok=false -- not an area for staff hits
    fighter.fight=function() fought=fought+1; return 'fighting' end
    fighter.threat=function() return {id=51,x=3,y=0} end
    fighter.hostiles=function() return {{id=51,x=3,y=0}} end
    fighter.dry=function() return true end
    local before=advances
    states.travel.tick(machine())
    assert(fought==0 and advances==before+1,'a dry traveller keeps moving instead of standing among monsters')
    fighter.dry=function() return false end
    states.travel.tick(machine())
    assert(fought==1 and advances==before+1,'with mana the same monster is fought')
    fighter.fight,fighter.dry,fighter.melee_ok=old_fight,old_dry,old_melee
  end
  fighter.threat,fighter.hostiles,trip.tick=original_threat,original_hostiles,original_trip
end
do
  data['act1.RouteTest.gold_farm']=true; me.area=2; me.gold=0
  no_farm=false
  local m=machine(); states.farm.enter(m)
  m.data.farm_area=2; m.data.farm_supply=0; m.data.farm_gain_at=-80000
  assert(states.farm.tick(m)=='decide' and m.data.spent[2], 'unproductive resource sweeps must end after 90 s')
  m=machine(); states.farm.enter(m); m.data.farm_area=2
  game.items=function() return {{code='jav',location='container',page=0}} end
  assert(states.farm.tick(m)=='retreat', 'recovery loot should be sold without finishing the sweep')
  game.items=function() return {} end
end
do
  local saved_map=map
  local original_threat,original_hostiles=fighter.threat,fighter.hostiles
  me.area=37; me.in_town=false
  game.units=function() return {{id=200,class_id=156,alive=true,x=10,y=0}} end
  service.prepare_antidote=function() return true end -- pending/refused item request
  local fought,accept,sight_checks=0,nil,0
  map={line_of_sight=function(x,y,tx,ty)
    assert(x==me.x and y==me.y and tx==10 and ty==0,'boss visibility must use current positions')
    sight_checks=sight_checks+1; return true
  end}
  fighter.fight=function(_,_,radius,filter,focus_id)
    fought=fought+1; accept=filter
    assert(radius==40 and focus_id==200,'visible Andariel must be the explicit combat focus')
  end
  states.andariel.tick(machine())
  assert(fought==1,'pending antidote acknowledgement must not suspend boss combat')
  assert(sight_checks==1 and accept,'visible Andariel must engage with a target filter')
  assert(accept({id=200,x=10,y=0}),'Andariel must remain an accepted target beyond blocker range')
  assert(accept({id=201,x=3,y=4}),'immediate blockers within five subtiles must remain accepted')
  assert(not accept({id=202,x=6,y=0}),'distant trash must not distract from the boss')
  do -- her company goes first: no forced focus while any of it is near
    local kept_fight,kept_hostiles,kept_sight,kept_fought,focus=fighter.fight,fighter.hostiles,sight_checks,fought,false
    fighter.hostiles=function() return {{id=203,x=8,y=0}} end
    local wide
    fighter.fight=function(_,_,_,filter,focus_id) wide,focus=filter,focus_id end
    states.andariel.tick(machine())
    assert(focus==nil and wide({id=203,x=8,y=0}) and wide({id=200,x=10,y=0}) and not wide({id=204,x=20,y=0}),
      'a boss with company near is fought by the usual target choice, the company included')
    fighter.fight,fighter.hostiles,sight_checks,fought=kept_fight,kept_hostiles,kept_sight,kept_fought
  end
  do -- seen from the way in, she is met back along it rather than in her hall
    local kept_fight,kept_sight,kept_to,kept_tick,kept_x=fighter.fight,sight_checks,approach.to,approach.tick,me.x
    local fights,walk=0,nil
    fighter.fight=function() fights=fights+1 end
    approach.to=function(self,x,y) walk={x=x,y=y}; self.x,self.y,self.status=x,y,'moving' end
    approach.tick=function() return 'moving' end
    local m=machine()
    m.data.andariel_trail={{x=-18,y=0},{x=-9,y=0},{x=0,y=0}}
    states.andariel.tick(m); states.andariel.tick(m)
    assert(fights==0 and walk and walk.x==-18 and walk.y==0 and not m.data.lured,
      'the first sight of Andariel starts the walk back along the approach')
    me.x=-17
    states.andariel.tick(m)
    assert(fights==1 and m.data.lured,'at the meeting place the fight is taken')
    me.x=kept_x
    states.andariel.tick(m)
    assert(fights==2,'the walk back is made once')
    fighter.fight,sight_checks,approach.to,approach.tick=kept_fight,kept_sight,kept_to,kept_tick
  end
  map.line_of_sight=function() sight_checks=sight_checks+1; return false end
  map.presets=function() return {{type='monster',class_id=156,x=10,y=0}} end
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  local moved=0
  approach.tick=function() return 'done' end
  approach.to=function(_,x,y) assert(x==10 and y==0); moved=moved+1 end
  states.andariel.tick(machine())
  assert(sight_checks==2,'boss visibility must be checked again on each tick')
  assert(fought==1 and moved==1,'an obscured boss must use the lair approach instead of stalling in combat')
  fighter.threat=function() return {id=201,x=3,y=4} end
  fighter.fight=function(_,_,radius) assert(radius==12); fought=fought+1 end
  states.andariel.tick(machine())
  assert(fought==2 and moved==1,'an obscured boss must still allow nearby reflex combat before approaching')
  fighter.threat,fighter.hostiles=original_threat,original_hostiles
  map=saved_map
end
do
  local old_units,old_quest,old_items=game.units,game.quest,game.items
  local old_area,old_gold=me.area,me.gold
  me.area,me.gold=37,1000
  game.units=function() return {{id=200,class_id=156,alive=false,x=10,y=0}} end
  game.quest=function() return {completed=false,reward_pending=false} end
  game.items=function() error('a dead boss must refresh credit before any resupply') end
  local m=machine()
  m.data.andariel_last_seen={id=200,x=3,y=4}
  assert(states.andariel.tick(m)=='sync_quests','observed Andariel death must request a fresh quest record')
  assert(m.data.andariel_last_seen.x==3 and m.data.andariel_last_seen.y==4 and not m.data.andariel_goal,
    'a corpse must not become a new live observation or approach goal')
  assert(states.andariel.tick(m)=='save_exit','an uncredited refresh attempt must not repeat on the same corpse')
  game.units=function() return {} end
  assert(states.andariel.tick(m)=='save_exit','a disappearing corpse must not discard the finite refresh attempt')
  game.quest=function(id) return {reward_pending=id==6} end
  m.data.quests_synced=true
  assert(states.andariel.tick(m)=='done','refreshed quest credit must complete the campaign')
  game.units,game.quest,game.items=old_units,old_quest,old_items
  me.area,me.gold=old_area,old_gold
end
do
  local saved_map=map
  local fought,warmed=0,0
  me.in_town=false
  fighter.threat=function() return {id=1} end
  fighter.fight=function() fought=fought+1 end
  map={exits=function() warmed=warmed+1; return {} end}
  local m=machine(); states.warm.enter(m); states.warm.tick(m)
  assert(fought==1 and warmed==1,'hot reload must defend while warming the map one level per tick')
  map=saved_map
end
do
  local old_now,old_act,old_state,old_units,old_items=charon.now,act,game.state,game.units,game.items
  local old_threat,old_walk=fighter.threat,package.loaded['lib.stamina'].walk
  local t,casts,moves,clicks,ready=20000,0,0,0,true
  charon.now=function() return t end
  game.state=function() return false end
  fighter.threat=function() return nil end
  package.loaded['lib.stamina'].walk=function() return false end
  act={ready=function() return ready end,use_item=function() casts=casts+1 end,
    move=function() moves=moves+1 end,interact=function() clicks=clicks+1 end}
  me.area=37; me.in_town=false
  local portal={id=90,portal=true,owner_name=me.name,x=2,y=0,mode=0}
  local scroll={id=91,code='tsc',location='container',page=0}
  game.units=function(kind) return kind=='object' and {portal} or {} end
  game.items=function() return {scroll} end
  local m=machine(); states.portal_heal.enter(m)
  states.portal_heal.tick(m)
  assert(clicks==1 and casts==0 and moves==0,'a nearby portal must be reused without spending a scroll')
  portal.x=26
  m=machine(); states.portal_heal.enter(m)
  ready=false; states.portal_heal.tick(m)
  assert(casts==0 and not m.data.cast_at and moves==0,'fresh portal casting must wait until the player can act')
  ready=true; states.portal_heal.tick(m)
  assert(casts==1 and m.data.cast_at==t and moves==0,'a far portal must be replaced with one fresh scroll')
  t=t+1000; states.portal_heal.tick(m)
  assert(casts==1 and moves==0 and clicks==1,'the old far portal must not hijack movement while the new cast is pending')
  portal={id=92,portal=true,owner_name=me.name,x=1,y=0,mode=0}
  states.portal_heal.tick(m)
  assert(clicks==2 and casts==1,'the newly appeared nearby portal must be entered immediately')
  portal.x=26; game.items=function() return {} end
  m=machine(); states.portal_heal.enter(m)
  states.portal_heal.tick(m)
  assert(moves==1 and casts==1,'without a scroll the far portal remains a usable escape')
  game.items=function() return {scroll} end
  m=machine(); states.portal_heal.enter(m); states.portal_heal.tick(m)
  assert(casts==2)
  t=t+4000; states.portal_heal.tick(m)
  assert(casts==2 and m.data.cast_at and moves==1,'a pending cast must receive the full four-second reply window')
  t=t+1; states.portal_heal.tick(m)
  assert(not m.data.cast_at and casts==2 and moves==1,'a cast timeout resets once without moving to the old far portal')
  states.portal_heal.tick(m)
  assert(casts==3 and m.data.cast_at==t,'a timed-out cast may retry once on the next tick')
  game.items=function() return {} end
  t=t+4001; states.portal_heal.tick(m); states.portal_heal.tick(m)
  assert(casts==3 and moves==2,'after a timeout with no scroll left the old portal remains the fallback')
  game.units=function() return {} end
  m=machine(); states.portal_heal.enter(m)
  assert(states.portal_heal.tick(m)=='retreat','without a portal or scroll escape falls back to retreat')
  local old_x,old_y=me.x,me.y
  me.x,me.y=0,0; portal.x,portal.y=6,0
  game.units=function() return {portal} end
  local before_moves,before_clicks=moves,clicks
  m=machine(); states.portal_heal.enter(m); states.portal_heal.tick(m)
  t=t+100; states.portal_heal.tick(m)
  assert(moves==before_moves+1 and clicks==before_clicks,
    'portal approach requests must retain their movement cooldown')
  me.x=4; states.portal_heal.tick(m)
  assert(clicks==before_clicks+1,
    'reaching the portal must permit immediate entry despite a recent approach request')
  t=t+1499; states.portal_heal.tick(m)
  assert(clicks==before_clicks+1,'entry retries must retain their own cooldown')
  t=t+2; states.portal_heal.tick(m)
  assert(clicks==before_clicks+2,'entry may retry after its interaction cooldown expires')
  states.portal_heal.enter(m)
  assert(m.data.portal_move_at==0 and m.data.used_at==0,'a new recovery must reset both request timers')
  me.x,me.y=old_x,old_y
  charon.now,act,game.state,game.units,game.items=old_now,old_act,old_state,old_units,old_items
  fighter.threat,package.loaded['lib.stamina'].walk=old_threat,old_walk
end
do
  local old_act,old_map,old_state,old_units,old_items,old_waypoints=act,map,game.state,game.units,game.items,game.waypoints
  local old_monsters=fighter.monsters
  local old_life,old_max_life=me.life,me.max_life
  local clicks,known,snapshot=0,true,{}
  local portal={id=90,portal=true,owner_name=me.name,x=2,y=0,mode=0}
  act={ready=function() return true end,interact=function() clicks=clicks+1 end}
  map={line_of_sight=function(x,y,tx)
    assert(x==portal.x and y==portal.y,'return safety must check line of sight from the portal endpoint')
    return tx~=3
  end}
  game.state=function() return false end
  game.units=function(kind) return kind=='object' and {portal} or {} end
  game.items=function()
    local potions={}
    for i=1,6 do potions[i]={code='mp1',location='belt'} end
    return potions
  end
  game.waypoints=function() return known and {[4]=true} or {} end
  fighter.monsters=function() return snapshot end
  local function monster(x,elite)
    return {type='monster',alive=true,attackable=true,owner=-1,x=x,y=0,unique=elite}
  end
  me.area=4; me.in_town=false; me.life=100; me.max_life=100
  local m=machine(); states.portal_heal.enter(m)
  snapshot={monster(6.8),monster(6.9)}
  states.portal_heal.tick(m)
  assert(m.data.portal_return_unsafe and clicks==1,'two reachable enemies near the portal must mark an unsafe return even beyond five subtiles from the player')
  me.area=1; me.in_town=true
  local before=clicks
  assert(states.portal_heal.tick(m)=='decide' and m.data.town_done and clicks==before,
    'successful restocking must avoid an unsafe portal when a progression waypoint is known')
  assert(states.decide.tick(m)=='take_waypoint' and m.data.waypoint_to==4,
    'avoiding an unsafe portal must resume through the existing progression waypoint without resetting the game')
  me.area=4; me.in_town=false; states.portal_heal.enter(m)
  assert(m.data.portal_return_unsafe==nil,'a new service visit must clear the earlier portal danger')
  snapshot={monster(6.9,true)}; states.portal_heal.tick(m)
  assert(not m.data.portal_return_unsafe,'a routine healthy supply visit may return past one elite')
  me.area=1; me.in_town=true; m.data.used_at=0; before=clicks
  states.portal_heal.tick(m)
  assert(clicks==before+1 and m.data.visit_done,'a routine lone-elite departure must return through its portal after shopping even with a known waypoint')
  for _,case in ipairs{{life=100,urgent=true,unsafe=true},{life=49,unsafe=true},{life=50,unsafe=false}} do
    me.area=4; me.in_town=false; me.life=case.life; states.portal_heal.enter(m)
    m.data.portal_urgent_at=case.urgent and charon.now() or nil
    states.portal_heal.tick(m)
    assert(m.data.portal_return_unsafe==case.unsafe,'a lone elite is unsafe only for urgent recovery or departure below half life')
    me.area=1; me.in_town=true; me.life=100; m.data.used_at=0; before=clicks
    local next_state=states.portal_heal.tick(m)
    if case.unsafe then
      assert(next_state=='decide' and clicks==before,'urgent or badly hurt lone-elite departures must retain waypoint diversion after healing')
    else
      assert(clicks==before+1,'half-life routine departures may return past a lone elite')
    end
  end
  me.area=4; me.in_town=false; states.portal_heal.enter(m)
  snapshot={monster(3,true),monster(7.1,true),monster(6.9)}
  m.data.used_at=0; states.portal_heal.tick(m)
  assert(m.data.portal_return_unsafe==false,'the latest outbound attempt must replace the tag; walled and distant enemies do not count, and one nearby ordinary enemy is safe')
  local old_clear=fighter.ground_clear
  fighter.ground_clear=function(_,point) assert(point==portal); return false end
  m.data.used_at=0; states.portal_heal.tick(m)
  assert(m.data.portal_return_unsafe,'persistent fire at the portal must mark its return unsafe')
  me.area=1; me.in_town=true; before=clicks
  assert(states.portal_heal.tick(m)=='decide' and clicks==before,
    'ground-fire departures must retain waypoint diversion after shopping')
  fighter.ground_clear=old_clear
  me.area=4; me.in_town=false; states.portal_heal.enter(m)
  m.data.used_at=0; states.portal_heal.tick(m)
  me.area=1; me.in_town=true
  m.data.used_at=0
  before=clicks; states.portal_heal.tick(m)
  assert(clicks==before+1,'a safe portal remains reusable after shopping')
  me.area=4; me.in_town=false; states.portal_heal.enter(m)
  snapshot={monster(6.8),monster(6.9)}; states.portal_heal.tick(m)
  known=false; me.area=1; me.in_town=true
  m.data.used_at=0
  before=clicks; states.portal_heal.tick(m)
  assert(clicks==before+1,'an unsafe endpoint without a known progression waypoint must retain the portal rather than force a full backtrack')
  me.area=4; me.in_town=false; states.portal_heal.enter(m)
  m.data.portal_town_only=true; states.portal_heal.tick(m)
  assert(m.data.portal_return_unsafe==nil,'quest reward returns must not acquire an emergency return tag')
  me.area=1; me.in_town=true
  assert(states.portal_heal.tick(m)==nil and states.portal_heal.tick(m)=='decide','quest reward returns must still finish in town')
  do -- a boss fight left with her company in sight is rejoined on foot, from two levels away
    local kept_waypoints,kept_hostiles=game.waypoints,fighter.hostiles
    local company=2
    game.waypoints=function() return {[5]=true} end
    fighter.hostiles=function(_,_,radius)
      local seen={{id=77}}
      for i=1,radius==24 and company or 0 do seen[#seen+1]={id=i} end
      return seen
    end
    snapshot={}
    local function leave(from)
      local boss=machine(); boss.data.boss_id=77
      me.area=37; me.in_town=false; states.portal_heal.enter(boss,from)
      boss.data.used_at=0; states.portal_heal.tick(boss)
      me.area=1; me.in_town=true; boss.data.used_at=0
      return boss
    end
    local boss=leave('andariel')
    before=clicks
    assert(boss.data.portal_crowd==2 and states.portal_heal.tick(boss)=='decide' and clicks==before,
      'a portal left among her company is not the way back to a boss')
    assert(states.decide.tick(boss)=='take_waypoint' and boss.data.waypoint_to==5,
      'the fight is rejoined from the waypoint two levels back')
    company=1; boss=leave('andariel'); before=clicks
    states.portal_heal.tick(boss)
    assert(clicks==before+1,'a boss left nearly alone is rejoined through the portal')
    company=2; boss=leave('travel'); before=clicks
    states.portal_heal.tick(boss)
    assert(clicks==before+1,'on the road a clear portal two levels from the waypoint is still used')
    fighter.hostiles=function(_,_,radius) return radius==24 and {{id=1},{id=2},{id=3}} or {} end
    boss=leave('andariel'); before=clicks
    states.portal_heal.tick(boss)
    assert(boss.data.portal_crowd==0 and clicks==before+1,
      'monsters met on the way in, with the boss not in sight, are rejoined through the portal')
    game.waypoints,fighter.hostiles=kept_waypoints,kept_hostiles
  end
  act,map,game.state,game.units,game.items,game.waypoints=old_act,old_map,old_state,old_units,old_items,old_waypoints
  fighter.monsters=old_monsters
  me.life,me.max_life=old_life,old_max_life
end
do
  local old_threat,old_dodge,old_items=fighter.threat,fighter.dodge,game.items
  fighter.threat=function() return nil end
  fighter.dodge=function() return false end
  game.items=function() return {} end
  me.in_town=false; me.alive=true; me.area=37; me.life=me.max_life*0.5
  bot.state='travel'; tick()
  assert(bot.state=='travel','half life without healing remains above the retreat boundary')
  me.life=me.max_life*0.49; tick()
  assert(bot.state=='portal_heal','exhausted healing must trigger escape below half life before the old 35 percent threshold')
  do -- a boss with less of her life left than we have of ours is finished, not left
    local old_unit,old_tick=game.unit,bot.tick
    bot.tick=function() end
    bot.state='andariel'; bot.data.boss_id=77
    game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=25} or nil end
    tick()
    assert(bot.state=='andariel','out of potions at half life, a boss at a quarter of hers is fought on')
    game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=80} or nil end
    tick()
    assert(bot.state=='portal_heal','a boss still ahead of us is left for healing as before')
    do -- with potions in hand, a boss fight going down slowly goes to town before it is a chicken
      local old_hostiles,old_scrolls,old_now=fighter.hostiles,service.portal_scrolls,charon.now
      -- Each step is its own moment: the life lost between them is not a burst.
      local t=charon.now()
      local function later() t=t+3000 end
      charon.now=function() return t end
      fighter.hostiles=function() return {{id=77,x=1,y=0}} end
      service.portal_scrolls=function() return 1 end
      game.items=function() return {{code='hp1',location='belt'}} end
      game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=80} or nil end
      later(); bot.state='andariel'; me.life=me.max_life*0.55; tick()
      assert(bot.state=='andariel','with potions in hand and life above the line the duel goes on')
      later(); me.life=me.max_life*0.48; tick()
      assert(bot.state=='portal_heal','a boss fight going down slowly is taken to town, got '..bot.state)
      game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=10} or nil end
      later(); bot.state='andariel'; tick()
      assert(bot.state=='andariel','a boss nearly dead is finished instead')
      service.portal_scrolls=function() return 0 end
      game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=80} or nil end
      later(); tick()
      assert(bot.state=='andariel','without a scroll there is no town to go to')
      -- The portal a boss fight asked for is still that fight: the chicken
      -- waits for the boss threshold while it is being opened.
      service.portal_scrolls=function() return 1 end
      later(); bot.state='travel'; me.life=me.max_life; tick()
      t=t+500; me.life=me.max_life*0.5
      bot.state='portal_heal'; bot.data.portal_boss=true; bot.data.visit_done=false
      bot.data.portal_urgent_at=nil -- as entering the state does
      tick()
      assert(bot.state=='portal_heal','half her life gone in a boss fight is the portal, not yet a new game, got '..bot.state)
      bot.data.portal_boss=false
      tick()
      assert(bot.state=='save_exit','the same loss on the road is a chicken as before, got '..bot.state)
      -- A boss a cast or two from dead is finished: only the lowest threshold still leaves.
      game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=8} or nil end
      bot.data.portal_boss,bot.data.visit_done,bot.data.portal_urgent_at=nil,nil,nil
      later(); bot.state='andariel'; me.life=me.max_life*0.75; tick()
      t=t+500; me.life=me.max_life*0.36; tick()
      assert(bot.state=='andariel','a third of our life gone in a blow, with the boss at 8%, is not a reason to leave, got '..bot.state)
      later(); me.life=me.max_life*0.21; tick()
      assert(bot.state=='andariel','nor is a fifth of our life left, got '..bot.state)
      later(); me.life=me.max_life*0.14; tick()
      assert(bot.state=='save_exit','the last-ditch chicken still leaves, got '..bot.state)
      bot.data.portal_boss,bot.data.visit_done,bot.data.portal_urgent_at=nil,nil,nil
      fighter.hostiles,service.portal_scrolls,charon.now=old_hostiles,old_scrolls,old_now
      bot.state='travel'
    end
    game.unit,bot.tick,bot.data.boss_id=old_unit,old_tick,nil
  end
  me.life=me.max_life
  fighter.threat,fighter.dodge,game.items=old_threat,old_dodge,old_items
end
do
  local old_now,old_act,old_state,old_units,old_items=charon.now,act,game.state,game.units,game.items
  local old_go,old_tick,old_bot_state,old_data=bot.go,bot.tick,bot.state,bot.data
  local old_hostiles,old_threat,old_dodge,old_dry=fighter.hostiles,fighter.threat,fighter.dodge,fighter.out_of_mana
  local old_scrolls=service.portal_scrolls
  local old_me={area=me.area,in_town=me.in_town,life=me.life,max_life=me.max_life,can_act=me.can_act,experience=me.experience}
  local t,near,scrolls,portal,ready,accepted=200000,1,1,nil,true,true
  local ranged={}
  local casts,clicks,dodges,exits=0,0,0,0
  charon.now=function() return t end
  game.state=function() return false end
  game.units=function(kind) return kind=='object' and portal and {portal} or {} end
  game.items=function()
    local items={{code='hp1',location='belt'}} -- remaining healing must not postpone danger recovery
    if scrolls>0 then items[#items+1]={id=91,code='tsc',location='container',page=0} end
    return items
  end
  service.portal_scrolls=function() return scrolls end
  fighter.hostiles=function(_,_,radius)
    if radius==16 then return ranged end
    return near==2 and {{},{}} or near==1 and {{}} or {}
  end
  fighter.threat=function() return nil end
  fighter.out_of_mana=function() return false end
  fighter.dodge=function() dodges=dodges+1; return true end
  act={ready=function() return ready end,use_item=function() casts=casts+1; return accepted end,
    interact=function() clicks=clicks+1 end}
  bot.go=function(self,state)
    self.state=state
    if state=='portal_heal' then states.portal_heal.enter(self) end
    if state=='save_exit' then exits=exits+1 end
  end
  bot.tick=function(self)
    if self.state=='portal_heal' and not me.in_town then
      local next_state=states.portal_heal.tick(self)
      if next_state then self:go(next_state) end
    end
  end
  me.area,me.in_town,me.max_life,me.can_act=37,false,100,true
  local function sample(start,finish)
    t=t+2500; me.experience=me.experience+1; bot.state,bot.data='travel',{}; me.life=start; tick()
    t=t+40; me.life=finish; tick()
  end
  sample(55,55)
  assert(bot.state=='travel','low life with potions and no recent loss must not start urgent recovery')
  near=0; sample(80,55)
  assert(bot.state=='travel','rapid life loss without a nearby hostile must not start urgent recovery')
  near=1; scrolls=0; sample(80,55)
  assert(bot.state=='travel','rapid life loss without a usable portal must not start urgent recovery')
  portal={id=90,portal=true,owner_name=me.name,x=me.x+6.1,y=me.y,mode=0}
  sample(80,55)
  assert(bot.state=='travel','a distant old portal alone must not start urgent recovery')
  portal=nil; scrolls=1
  local before_casts,before_grabs=casts,quick_grabs
  sample(75,55)
  assert(bot.state=='portal_heal' and casts==before_casts+1 and quick_grabs==before_grabs+1,
    'rapid moderate damage must immediately cast despite healing reserves and an always-active dodge, without escape loot')
  do -- the third emergency from one place is a new game; elsewhere, or against a boss, it is a portal
    local kept_data,kept_x,kept_exits=bot.data,me.x,exits
    local function again(state)
      t=t+2500; me.experience=me.experience+1; bot.state=state or 'travel'; me.life=75; tick()
      t=t+40; me.life=55; tick()
    end
    again()
    assert(bot.state=='portal_heal' and bot.data.emergency.count==2,'the second emergency in a place is still a portal')
    me.x=me.x+31; again()
    assert(bot.state=='portal_heal' and bot.data.emergency.count==1,'somewhere else the count starts again')
    again(); again()
    assert(bot.state=='save_exit' and exits==kept_exits+1,
      'the third emergency in the same place is a new game: the portal leads back to what caused it')
    do
      local kept_unit=game.unit
      bot.data.boss_id=77
      game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=60} or nil end
      bot.data.emergency.count=2; again('andariel')
      assert(bot.state=='portal_heal','a boss already hurt is not left for a new game on that count')
      game.unit=function(kind,id) return kind=='monster' and id==77 and {id=77,alive=true,life_percent=100} or nil end
      bot.data.emergency.count=2; again('andariel')
      assert(bot.state=='save_exit','with the boss still whole there is nothing to lose by one')
      game.unit,bot.data.boss_id=kept_unit,nil
    end
    me.x,exits=kept_x,kept_exits
    bot.data=kept_data; bot.state='portal_heal'; me.life=55
  end
  local urgent=bot.data.portal_urgent_at
  t=t+40; tick()
  assert(bot.data.portal_urgent_at==urgent,'ongoing damage recovery must not reset the urgent deadline')
  local before_dodges=dodges
  t=urgent+4000; tick()
  assert(bot.state=='save_exit' and dodges==before_dodges,'urgent portal timeout must exit before a reactive dodge')
  -- Cross the old stall threshold after the recent-damage samples have expired.
  t=t+2500; me.experience=me.experience+1; bot.state,bot.data='travel',{}; me.life=75; tick()
  t=t+58000; tick()
  t=t+40; me.life=55; tick()
  urgent=bot.data.portal_urgent_at
  assert(urgent==t and bot.state=='portal_heal','near-stalled combat must still start urgent recovery')
  t=urgent+2100; tick()
  assert(bot.state=='portal_heal' and bot.data.portal_urgent_at==urgent,
    'the minute stall watchdog must not replace an urgent escape after damage samples expire')
  t=urgent+4000; tick()
  assert(bot.state=='save_exit','the urgent escape deadline must survive the stall watchdog')
  portal={id=90,portal=true,owner_name=me.name,x=me.x+2,y=me.y,mode=0}; scrolls=0
  local before_clicks=clicks; before_dodges=dodges
  sample(80,55)
  assert(bot.state=='portal_heal' and clicks==before_clicks+1 and dodges==before_dodges+1,
    'an owned nearby portal must be clicked before internal or global dodging, without a scroll')
  urgent=bot.data.portal_urgent_at
  me.in_town=true; me.area=1; t=urgent+4000; tick()
  assert(bot.state=='portal_heal' and not bot.data.portal_urgent_at,'town arrival must cancel the urgent deadline')
  me.in_town=false; me.area=37; portal=nil; scrolls=1
  ready=false; before_casts=casts; sample(80,55)
  assert(bot.state=='portal_heal' and casts==before_casts and not bot.data.cast_at,'busy players must not send portal casts')
  ready=true; accepted=false; t=t+40; tick()
  assert(casts==before_casts+1 and not bot.data.cast_at,'a refused portal cast must not claim a pending portal')
  t=t+40; tick(); t=t+459; tick()
  assert(casts==before_casts+1,'refused portal casts must not be retried each tick')
  accepted=true; t=t+1; tick()
  assert(casts==before_casts+2 and bot.data.cast_at==t,'a refused cast may retry after its short cooldown')
  ready=false; portal={id=90,portal=true,owner_name=me.name,x=me.x+2,y=me.y,mode=0}
  before_clicks=clicks; t=t+40; tick()
  assert(clicks==before_clicks,'busy players must not send portal entry requests')
  ready=true; portal=nil
  bot.data.cast_at=nil; scrolls=0; t=t+40; tick()
  assert(bot.state=='save_exit','losing an urgent escape resource must not switch into an unbounded walk home')
  scrolls=1
  t=t+2500; bot.state,bot.data='portal_heal',{}; states.portal_heal.enter(bot)
  me.life=80; tick()
  assert(not bot.data.portal_urgent_at,'a normal outbound restock must not start with an urgent deadline')
  t=t+40; me.life=55; tick()
  urgent=bot.data.portal_urgent_at
  assert(urgent==t and bot.data.cast_at,'damage during an existing outbound cast must arm urgency without resetting the cast')
  t=t+40; tick()
  assert(bot.data.portal_urgent_at==urgent,'existing outbound urgency must retain its first deadline')
  t=urgent+4000; tick()
  assert(bot.state=='save_exit','an existing restock that becomes dangerous must also have a finite escape deadline')
  for _,case in ipairs({{100,39,1},{24,24,2},{14,14,1}}) do
    near=case[3]; before_casts=casts; local before_exits=exits
    sample(case[1],case[2])
    assert(bot.state=='save_exit' and exits>before_exits and casts==before_casts,
      'hard burst, crowded and low-life chickens must still override portal escape')
  end
  local old_clear=fighter.ground_clear
  fighter.ground_clear=function() return false end
  near=0; before_casts=casts; sample(75,55)
  assert(bot.state=='portal_heal' and casts==before_casts+1,
    'damaging floor fire must trigger earlier portal recovery even after all nearby monsters are gone')
  sample(14,14)
  assert(bot.state=='save_exit','critical health in ownerless ground fire must still chicken')
  fighter.ground_clear=old_clear
  do
    local old_level,old_target,old_hop,old_mover=me.level,fighter.target_id,trip.hop,trip.mover
    local observed
    fighter.threat=function(_,_,_,accept) observed=accept end
    local function filter()
      me.life=100; bot.state,bot.data='travel',{}; observed=nil
      tick()
      assert(observed,'travel must supply its engagement filter')
      return observed
    end
    near=1; scrolls=1; portal=nil; ready=true
    trip.hop={from=37}; trip.mover={path={{x=me.x+10,y=me.y}},index=1}
    -- What shoots is gone around, never sought out; upon her it is still fought.
    local elite={id=601,x=me.x+7,y=me.y,ranged=true,unique=true}
    local accept=filter()
    assert(not accept(elite) and not accept({id=602,x=me.x+8,y=me.y,ranged=true}),
      'a shooter is not sought out on the way, elite or not')
    assert(accept({id=601,x=me.x+4,y=me.y,ranged=true,unique=true}),'a shooter upon her still requires immediate defense')
    fighter.target_id=601
    assert(accept({id=601,x=me.x+6,y=me.y,ranged=true,unique=true,life_percent=24}),'a retained near-dead target may still be finished')
    fighter.target_id=nil
    assert(accept({id=605,x=me.x+8,y=me.y}),'ordinary forward blockers remain eligible')
    me.level,fighter.target_id,trip.hop,trip.mover=old_level,old_target,old_hop,old_mover
  end
  charon.now,act,game.state,game.units,game.items=old_now,old_act,old_state,old_units,old_items
  bot.go,bot.tick,bot.state,bot.data=old_go,old_tick,old_bot_state,old_data
  fighter.hostiles,fighter.threat,fighter.dodge,fighter.out_of_mana=old_hostiles,old_threat,old_dodge,old_dry
  service.portal_scrolls=old_scrolls
  for name,v in pairs(old_me) do me[name]=v end
  me.can_act=old_me.can_act
end
do
  me.in_town=false; me.alive=true; me.area=2; me.life=me.max_life
  local ticks,dodges=0,0
  bot.tick=function() ticks=ticks+1 end
  fighter.dodge=function() dodges=dodges+1; return true end
  bot.state='travel'
  tick()
  assert(dodges==1 and ticks==0,'an active sidestep must own the tick before travel can issue another action')
  bot.state='save_exit'
  tick()
  assert(dodges==1 and ticks==1,'a sidestep must never delay the emergency exit')
end
do
  local old_info,old_units,old_object_info,old_map=game.level_info,game.units,game.object_info,map
  local old_threat,old_hostiles,old_trip=fighter.threat,fighter.hostiles,trip.tick
  local advances,presets=0,0
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  trip.tick=function() advances=advances+1; return 'moving' end
  game.level_info=function() return {waypoint=true} end
  game.object_info=function() return {waypoint=true} end
  local objects,clear={},true
  game.units=function(kind) return kind=='object' and objects or {} end
  map={presets=function()
    presets=presets+1; return {{type='object',class_id=119,x=100,y=100}}
  end,line_of_sight=function() return clear end}
  me.in_town=false; me.x=0; me.y=0; me.gold=360
  for _,area in ipairs({6,27,29,35}) do
    me.area=area
    local m=machine()
    assert(states.travel.tick(m)=='activate_waypoint','restart anchors must still permit deliberate detours')
    states.activate_waypoint.enter(m)
    assert(m.data.wp_spawn.x==100 and m.data.wp_spawn.y==100,'anchors retain their preset approach')
  end
  assert(presets==4)
  do -- the map places a waypoint where the game shows none
    local old_now,old_ui,old_to,t,kept,kept_presets=charon.now,game.ui,approach.to,10000,advances,presets
    charon.now=function() return t end
    game.ui=function() return false end
    approach.to=function() end
    me.area=35; me.x=100; me.y=100
    local m=machine()
    assert(states.travel.tick(m)=='activate_waypoint')
    states.activate_waypoint.enter(m)
    assert(states.activate_waypoint.tick(m)==nil,'an absent waypoint is given a moment to load')
    t=t+2500
    local after,why=states.activate_waypoint.tick(m)
    assert(after=='travel' and tostring(why):find('no waypoint',1,true) and data['act1.RouteTest.no_wp.35'],
      'standing on the preset without a waypoint ends the detour and is remembered')
    assert(states.travel.tick(machine())~='activate_waypoint','a remembered absence is not tried again in a later game')
    data['act1.RouteTest.no_wp.35']=nil
    charon.now,game.ui,approach.to=old_now,old_ui,old_to; me.x=0; me.y=0; advances,presets=kept,kept_presets
  end
  me.area=5 -- Dark Wood is optional, even with a known preset.
  assert(states.travel.tick(machine())==nil and advances==1,'unloaded optional waypoints must not divert travel')
  local wp={waypoint=true,x=26,y=0}
  objects={wp}
  assert(states.travel.tick(machine())==nil and advances==2,'far optional waypoints must not divert travel')
  wp.x=25; clear=false
  assert(states.travel.tick(machine())==nil and advances==3,'optional waypoints behind walls must not divert travel')
  clear=true
  local m=machine()
  assert(states.travel.tick(m)=='activate_waypoint','a visible waypoint she passes within twenty-five subtiles is taken')
  states.activate_waypoint.enter(m)
  assert(m.data.wp_spawn==wp and presets==4,'optional activation must use the loaded object without a preset detour')
  game.level_info,game.units,game.object_info,map=old_info,old_units,old_object_info,old_map
  fighter.threat,fighter.hostiles,trip.tick=old_threat,old_hostiles,old_trip
end


do
  local old_data,old_map,old_exploration=data,map,exploration_stub
  local old_me={};for name,value in pairs(me) do old_me[name]=value end
  local old_now,old_ui,old_items,old_units=charon.now,game.ui,game.items,game.units
  local old_info,old_object_info,old_waypoints,old_quest=game.level_info,game.object_info,game.waypoints,game.quest
  local old_threat,old_hostiles,old_trip=fighter.threat,fighter.hostiles,trip.tick
  local t,open,known,completed,vendor=10000,false,false,true,false
  local walks,explored,objects=0,0,{}
  data={['act1.RouteTest.gold_farm']=true}
  charon.now=function() return t end
  game.ui=function() return open end
  game.quest=function(id) return {completed=id==1 and completed} end
  game.waypoints=function() return known and {[3]=true,[6]=true} or {} end
  game.items=function() return vendor and {{code='jav',location='container',page=0}} or {} end
  game.units=function(kind) return kind=='object' and objects or {} end
  game.level_info=function(area) return {waypoint=area==3 or area==6,monster_level=area==3 and 2 or 1} end
  game.object_info=function() return {waypoint=true} end
  map={presets=function() return {{type='object',class_id=119,x=100,y=100}} end,
    line_of_sight=function() return true end}
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  trip.tick=function() return 'moving' end
  exploration_stub={tick=function() explored=explored+1;return 'moving' end}
  me.class,me.difficulty,me.level,me.area,me.in_town='sorceress',0,3,3,false
  me.gold,me.stash_gold,me.mana,me.max_mana,me.life,me.max_life=0,0,0,100,100,100
  local m=machine()
  assert(states.travel.tick(m)=='activate_waypoint' and m.data.wp_resume=='travel',
    'the first post-Den Normal sorceress pass must establish Cold Plains even with no nearby waypoint object')
  states.activate_waypoint.enter(m)
  assert(m.data.wp_timeout==45000 and m.data.wp_spawn.x==100,'Cold Plains recovery anchors get a preset and a bounded forty-five-second budget')
  for _,guard in ipairs{'pre-Den','difficulty','class'} do
    completed=guard~='pre-Den';me.difficulty=guard=='difficulty' and 1 or 0;me.class=guard=='class' and 'barbarian' or 'sorceress'
    objects={}
    assert(states.travel.tick(machine())==nil,'other classes, difficulties and pre-Den travel must not acquire a far recovery anchor: '..guard)
    local nearby={waypoint=true,x=12,y=0};objects={nearby}
    local optional=machine()
    assert(states.travel.tick(optional)=='activate_waypoint','ordinary opportunistic waypoint behavior must remain: '..guard)
    states.activate_waypoint.enter(optional)
    assert(optional.data.wp_timeout==30000 and optional.data.wp_spawn==nearby,'opportunistic Cold Plains activation keeps the thirty-second policy')
  end
  completed=true;me.difficulty=0;me.class='sorceress';objects={}
  vendor=true
  assert(states.travel.tick(machine())~='activate_waypoint','held vendor recovery loot must not wait for a recovery-anchor detour')
  local cash=machine();cash.data.farm_area=3;cash.data.spent={};cash.data.farm_commit={area=3,mode='gold'}
  assert(states.farm.tick(cash)=='retreat' and not cash.data.farm_commit,
    'Cold Plains farming must cash in existing vendor loot before its waypoint detour')
  vendor=false
  local function farm()
    local state=machine()
    state.data.farm_area=3;state.data.spent={};state.data.farm_commit={area=3,mode='gold'}
    return state
  end
  local prior_threat,prior_fight=fighter.threat,fighter.fight
  local fought=0
  fighter.threat=function(_,_,radius)
    assert(radius==12,'recovery-anchor travel must not inherit the farm whole-clear engagement radius')
    return true
  end
  fighter.fight=function() fought=fought+1 end
  m=farm()
  assert(states.farm.tick(m)=='activate_waypoint' and fought==0,
    'optional clear-area fights must not starve selection of the recovery waypoint')
  states.activate_waypoint.enter(m)
  assert(states.activate_waypoint.tick(m)==nil and fought==1,
    'waypoint activation must still defend against travel-range threats')
  fighter.threat,fighter.fight=prior_threat,prior_fight
  m=farm();local commitment=m.data.farm_commit
  assert(states.farm.tick(m)=='activate_waypoint' and m.data.wp_resume=='farm' and m.data.farm_commit==commitment,
    'actual Cold Plains gold farming must acquire its recovery anchor without discarding the farm commitment')
  states.activate_waypoint.enter(m)
  m.data.wp_mover={status='moving',tick=function() walks=walks+1 end}
  t=t+31000
  assert(states.activate_waypoint.tick(m)==nil and walks==1 and not m.data.no_wp,'the recovery anchor must remain active beyond the optional thirty-second limit')
  known=true;open=true
  assert(states.activate_waypoint.tick(m)=='farm' and not m.data.wp_resume and m.data.farm_commit==commitment
    and not m.data.spent[3],'panel acknowledgement must resume farming and clear only the waypoint resume field')
  open=false;states.farm.enter(m)
  assert(m.data.farm_commit.area==3 and m.data.farm_commit.mode=='gold' and m.data.farm_area==3
    and not m.data.spent[3],'farm re-entry must retain the chosen area rather than invent cleared ground')
  known=false;data['act1.RouteTest.wp.3']=nil
  m=farm();commitment=m.data.farm_commit
  assert(states.farm.tick(m)=='activate_waypoint')
  states.activate_waypoint.enter(m);t=t+45001
  assert(states.activate_waypoint.tick(m)=='farm' and m.data.no_wp==3 and not m.data.wp_resume
    and m.data.farm_commit==commitment and not m.data.spent[3],'timeout must also resume the original farm without clearing its ground or commitment')
  local before=explored
  assert(states.farm.tick(m)==nil and explored==before+1 and not m.data.wp_resume,'an unreachable anchor must not be retried during the same-game farm')
  assert(states.travel.tick(m)==nil,'same-game no_wp must also prevent repeating the attempt during travel')
  me.area=6
  assert(states.travel.tick(m)=='activate_waypoint' and m.data.wp_resume=='travel','later travel activation must replace the prior farm resume intent')
  states.activate_waypoint.enter(m);known=true;open=true
  assert(states.activate_waypoint.tick(m)=='travel' and not m.data.wp_resume,'a future travel activation must return to travel after farming resumes')
  open=false;me.area=3
  for _,case in ipairs{{known=true,expected='save_exit'},{known=false,expected='decide'},
    {known=true,area=2,expected='decide'},{known=true,difficulty=1,expected='decide'},
    {known=true,class='barbarian',expected='decide'},{known=true,pre_den=true,expected='decide'}} do
    me.area,me.difficulty,me.class=case.area or 3,case.difficulty or 0,case.class or 'sorceress'
    completed=not case.pre_den;known=case.known;data['act1.RouteTest.wp.3']=nil
    m=farm();m.data.farm_area=me.area;m.data.farm_supply=0;m.data.farm_gain_at=t-90000
    assert(states.farm.tick(m)==case.expected and m.data.spent[me.area],
      'dry refresh needs a usable Normal-sorceress recovery anchor; unknown, unreachable and other farms keep the fallback')
  end
  data,map,exploration_stub=old_data,old_map,old_exploration
  charon.now,game.ui,game.items,game.units=old_now,old_ui,old_items,old_units
  game.level_info,game.object_info,game.waypoints,game.quest=old_info,old_object_info,old_waypoints,old_quest
  fighter.threat,fighter.hostiles,trip.tick=old_threat,old_hostiles,old_trip
  for name in pairs(me) do me[name]=nil end;for name,value in pairs(old_me) do me[name]=value end
end

do
  local old_threat,old_hostiles,old_items=fighter.threat,fighter.hostiles,game.items
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  me.area=2; me.in_town=false; me.gold=120
  game.items=function()
    return {{code='mp1',location='belt'},{code='mp1',location='belt'},
      {code='mp1',location='container',page=0},{code='rvs',location='container',page=0}}
  end
  loot_checks,container_checks=0,0
  states.travel.tick(machine())
  assert(container_checks==0 and loot_checks==1,'six carried plus affordable mana reserves skip containers but retain loot')
  me.gold=60
  states.travel.tick(machine())
  assert(container_checks==1 and loot_checks==2,'below six mana reserves travel may gather containers')
  fighter.threat,fighter.hostiles,game.items=old_threat,old_hostiles,old_items
end

do
  local old_quest,old_upkeep,old_tick=game.quest,fighter.upkeep,bot.tick
  local old_synced,old_town=bot.data.quests_synced,me.in_town
  game.quest=function() return {completed=true,reward_pending=true} end
  bot.state='warm'; bot.data.quests_synced=nil
  me.alive=true; me.in_town=true; me.life=me.max_life
  fighter.upkeep=function() end
  bot.tick=function() end
  tick()
  assert(bot.state=='warm','stale quest bits must not finish a newly loaded campaign before quest synchronization')
  bot.data.quests_synced=true
  fighter.upkeep=function() error('completion must precede upkeep and healing') end
  me.alive=true; me.life=1
  for _,pending in ipairs({false,true}) do
    game.quest=function(id) return {completed=id==6 and not pending,reward_pending=id==6 and pending} end
    for _,state in ipairs({'portal_heal','town'}) do
      bot.state=state
      tick()
      assert(bot.state=='done','Andariel quest credit must end an alive campaign before town or portal healing')
    end
  end
  local dead_ticks=0
  bot.tick=function() dead_ticks=dead_ticks+1 end
  bot.state='portal_heal'; me.alive=false
  tick()
  assert(bot.state=='dead' and dead_ticks==1,'quest credit must not bypass handling a dead character')
  game.quest,fighter.upkeep,bot.tick=old_quest,old_upkeep,old_tick
  bot.data.quests_synced,me.in_town=old_synced,old_town
  me.alive=true; me.life=me.max_life
end
do
  local old_threat,old_hostiles,old_trip,old_items=fighter.threat,fighter.hostiles,trip.tick,game.items
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  game.items=function() return {} end
  local advances,detours,result=0,0,'moving'
  trip.tick=function() advances=advances+1; return 'moving' end
  exploration_stub={observe=function() end,tick=function(_,player)
    if player.area~=2 then detours=detours+1 end
    return result
  end}
  den=false; me.level=1; me.area=2; me.in_town=false; me.gold=0
  data['act1.RouteTest.gold_farm']=nil
  -- A build that levels before the Den keeps this farm active while it is exercised.
  local sorceress=require('lib.builds').sorceress
  local den_level=sorceress.den_level
  sorceress.den_level=2
  local m=machine(); states.farm.enter(m); states.farm.tick(m)
  m.data.farm_via,m.data.farm_wp_walk={[2]=3},6
  data['act1.RouteTest.wp.3']=true -- a waypoint she has; one she lacks is fetched first (below)
  me.area=1; me.in_town=true
  assert(states.farm.tick(m)==nil,'an exploration detour through town must precede a farm waypoint handoff')
  assert(detours==1 and advances==0,'farm must not overwrite an active exploration detour with a border-return trip')
  me.area=4; me.in_town=false
  states.farm.tick(m)
  assert(detours==2 and advances==0,'exploration ownership must also precede walking to a farm waypoint')
  local old_fight,fights=fighter.fight,0
  fighter.threat=function() return {} end
  fighter.fight=function() fights=fights+1 end
  states.farm.tick(m)
  assert(fights==1 and detours==2,'combat must retain priority over the temporary exploration detour')
  fighter.fight=old_fight; fighter.threat=function() return nil end
  me.area=1; me.in_town=true
  result='outside'
  assert(states.farm.tick(m)=='take_waypoint' and m.data.waypoint_to==3,
    'released exploration ownership must permit the farm waypoint handoff')
  data['act1.RouteTest.wp.3']=nil
  m.data.farm_via,m.data.farm_wp_walk=nil,nil
  states.farm.tick(m)
  assert(detours==4 and advances==1,'released exploration ownership must let farm return to its chosen area')
  exploration_stub=nil
  sorceress.den_level=den_level
  fighter.threat,fighter.hostiles,trip.tick,game.items=old_threat,old_hostiles,old_trip,old_items
end
do
  local old_threat,old_fight=fighter.threat,fighter.fight
  local fought=0
  fighter.threat=function() return {id=80} end
  fighter.fight=function() fought=fought+1 end
  me.area=2; me.in_town=false
  safe_approach=true
  states.travel.tick(machine())
  assert(fought==0,'a proven-safe nearby loot approach can precede discretionary fighting')
  safe_approach=false
  states.travel.tick(machine())
  assert(fought==1,'withdrawn loot safety must hand control to combat on the next tick')
  fighter.threat,fighter.fight=old_threat,old_fight
end

do
  local old_dodge,old_tick,old_threat,old_hostiles=fighter.dodge,bot.tick,fighter.threat,fighter.hostiles
  local advanced,dodges=0,0
  me.alive=true; me.in_town=false; me.can_act=true; me.life=100; me.max_life=100
  me.area=2; me.gold=1000; bot.state='travel'; bot.data.quests_synced=true
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  bot.tick=function() advanced=advanced+1 end
  fighter.dodge=function() dodges=dodges+1; return true end
  local before=quick_grabs
  tick()
  assert(quick_grabs==before+1 and dodges==1 and advanced==0,
    'a nearby pickup must run without stealing an emergency dodge tick')
  fighter.dodge=function() return false end
  tick()
  assert(quick_grabs==before+2 and advanced==1,
    'a nearby pickup must not prevent the campaign from continuing combat or movement')
  bot.state='save_exit'; tick()
  assert(quick_grabs==before+2,'leaving the game must not issue opportunistic pickups')
  bot.state='travel'; me.alive=false; tick()
  assert(quick_grabs==before+2,'a dead character must not issue opportunistic pickups')
  fighter.dodge,bot.tick,fighter.threat,fighter.hostiles=old_dodge,old_tick,old_threat,old_hostiles
end
do
  local explore_module=package.loaded['lib.explore']
  local old_new,old_threat,old_hostiles=explore_module.new,fighter.threat,fighter.hostiles
  local old_items=game.items
  game.items=function() -- a boss reserve, so the search itself is what is tested
    local carried={}
    for i=1,3 do carried[i]={code='hp1',location='container',page=0} end
    return carried
  end
  local made={}
  explore_module.new=function(level,reset)
    local survey={level=level,reset=reset,exhaustive=false,cleanups=0,ticks=0}
    function survey:observe() end
    function survey:tick() self.ticks=self.ticks+1; return 'done' end
    function survey:cleanup()
      if self.exhaustive then return false end
      self.exhaustive=true; self.cleanups=self.cleanups+1; return true
    end
    made[#made+1]=survey
    return survey
  end
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  me.area=8; me.in_town=false; den=false; safe_approach=false
  local m=machine(); states.clear.enter(m)
  local first=made[1]
  assert(states.clear.tick(m)==nil and #made==1 and first.cleanups==1,
    'normal Den exhaustion must first enable exhaustive cleanup without resetting observations')
  assert(states.clear.tick(m)==nil and #made==2 and made[2].reset and made[2].exhaustive,
    'only exhausted Den cleanup may reset observations, and its next sweep must remain exhaustive')
  assert(not den and not m.data.den_done,'terrain exhaustion must never claim Den quest credit')
  den=true
  assert(states.clear.tick(m)=='portal_heal' and #made==2,
    'actual quest credit must finish the Den before starting another search')

  do -- the Den's elite pack, found without the mana for it, waits until the rest is searched
    local kept={x=me.x,y=me.y,mana=me.mana,max_mana=me.max_mana,attack_mana=fighter.attack_mana,new=explore_module.new}
    den=false; made={}
    local seen={{id=70,x=40,y=0,superunique=true,name='Corpsefire'},{id=71,x=42,y=2,minion=true},{id=72,x=5,y=0}}
    local asked,status='unset','moving'
    fighter.hostiles=function() return seen end
    fighter.threat=function(_,_,_,filter) asked=filter; return nil end
    explore_module.new=function(level,reset,avoid)
      local survey={level=level,reset=reset,avoid=avoid}
      function survey:observe() end
      function survey:tick() return status end
      function survey:cleanup() return true end
      made[#made+1]=survey
      return survey
    end
    me.x,me.y,me.mana,me.max_mana,fighter.attack_mana=0,0,4,40,3
    local hold=machine(); states.clear.enter(hold)
    assert(states.clear.tick(hold)==nil and hold.data.den_pack.held,'short of the mana for it, the pack is left where it stands')
    local zones=made[1].avoid()
    assert(#zones==1 and zones[1].x==41 and zones[1].y==1 and zones[1].radius>=12,'its ground is kept out of the search')
    assert(type(asked)=='function' and not asked(seen[1]) and not asked(seen[2]) and asked(seen[3]),
      'nothing of the pack is fought meanwhile, and the rest is')
    assert(not asked{id=73,x=45,y=0},'nor what stands on its ground')
    assert(asked{id=71,x=3,y=0,minion=true},'but what is upon her is fought, whatever it is')
    assert(not made[1].accept(seen[2]) and made[1].accept(seen[3]),'and the search walks up to none of the pack')
    me.mana=38
    assert(states.clear.tick(hold)==nil and not hold.data.den_pack.held and #made[1].avoid()==0 and asked==nil,
      'with her pool full again the pack is hers')
    me.mana=20
    assert(states.clear.tick(hold)==nil and not hold.data.den_pack.held,'and stays hers while the fight has mana')
    me.mana=4
    assert(states.clear.tick(hold)==nil and hold.data.den_pack.held,'down to her last casts with it alive, she leaves it again')
    status='done'
    local clock,t0=charon.now,charon.now()
    assert(states.clear.tick(hold)==nil and hold.data.den_pack.waiting and hold.data.den_pack.held and #made==1,
      'with the rest searched she stands off while her pool fills')
    charon.now=function() return t0+121000 end
    assert(states.clear.tick(hold)==nil and hold.data.den_pack.last and not hold.data.den_pack.held and #made==2,
      'and after two minutes the pack is next, with what she has')
    status='moving'
    assert(states.clear.tick(hold)==nil and not hold.data.den_pack.held and asked==nil,'and it is not put off again')
    charon.now=clock
    local wait=machine(); me.mana=4; status='moving'; states.clear.enter(wait)
    states.clear.tick(wait); status='done'; states.clear.tick(wait)
    local before=#made
    assert(wait.data.den_pack.waiting,'standing off again in another game')
    me.mana=38; status='moving'
    assert(states.clear.tick(wait)==nil and not wait.data.den_pack.waiting and not wait.data.den_pack.held and #made==before+1,
      'her pool full while she waits, the pack is hers and the search takes its ground in again')
    local met=machine(); me.mana=38; states.clear.enter(met)
    assert(states.clear.tick(met)==nil and met.data.den_pack.held==false,'found with a full pool, it is fought at once')
    seen={}; me.x,me.y=41,1
    assert(states.clear.tick(met)==nil and met.data.den_pack.gone,'standing where it was with none of it in sight, it is gone')
    me.x,me.y,me.mana,me.max_mana,fighter.attack_mana=kept.x,kept.y,kept.mana,kept.max_mana,kept.attack_mana
    explore_module.new=kept.new
    fighter.threat=function() return nil end
    fighter.hostiles=function() return {} end
  end

  me.area=25; den=false; made={}
  m=machine(); states.countess.enter(m)
  assert(states.countess.tick(m)==nil and #made==1 and made[1].cleanups==1 and not m.data.countess_done,
    'normal cellar exhaustion must retain observations for exhaustive cleanup before abandoning the search')
  assert(states.countess.tick(m)=='save_exit' and m.data.countess_failed and not m.data.countess_done and not m.data.countess_dead,
    'exhaustive cellar exhaustion must abandon the attempt without fabricating a Countess kill')
  explore_module.new,fighter.threat,fighter.hostiles=old_new,old_threat,old_hostiles
  game.items=old_items
end
do
  local travel=package.loaded['lib.travel']
  local old_route,old_now,old_scrolls,old_items=travel.route,charon.now,service.portal_scrolls,game.items
  local old_threat,old_hostiles,old_dry,old_fight,old_dodge=fighter.threat,fighter.hostiles,fighter.out_of_mana,fighter.fight,fighter.dodge
  local old_trip,old_bot_tick=trip.tick,bot.tick
  local t,blocked,fights,advances=20000,true,0,0
  local inventory={{code='jav',location='container',page=0}}
  charon.now=function() return t end
  travel.route=function(from,to)
    if to==1 and from~=1 then return {{to=2},{to=1}} end
    return old_route(from,to)
  end
  service.portal_scrolls=function() return 0 end
  game.items=function() return inventory end
  fighter.threat=function() if blocked then return {id=80} end end
  fighter.hostiles=function() return {} end
  fighter.out_of_mana=function() return true end
  fighter.dodge=function() return false end
  fighter.fight=function() assert(fighter.melee_ok,'funded Den recovery must defend itself without mana'); fights=fights+1 end
  trip.tick=function() advances=advances+1; return 'moving' end
  bot.tick=function() end
  den=false; me.level=3; me.area=8; me.in_town=false; me.alive=true; me.can_act=true
  me.life=100; me.max_life=100; me.gold=120; bot.state='clear'; bot.data={}
  tick()
  assert(bot.state=='retreat','dry funded unfinished Den must retain its current game without a portal')
  states.retreat.enter(bot)
  tick(); states.retreat.tick(bot)
  assert(fighter.melee_ok and fights==1 and advances==0,'only blocking combat should delay the retreat path')
  me.area=2; tick(); states.retreat.tick(bot)
  assert(fighter.melee_ok and fights==2,'defensive fallback must survive crossing back into Blood Moor')
  blocked=false; states.retreat.tick(bot)
  assert(advances==1,'the walk home must resume when blockers clear')
  local begun=bot.data.retreat_started
  t=t+1000; states.retreat.enter(bot)
  assert(bot.data.retreat_started==begun,'watchdog replanning must not renew the retreat budget')
  me.area=1; me.in_town=true
  assert(states.retreat.tick(bot)=='decide' and not bot.data.retreat_den and not bot.data.town_done,
    'reaching town must clear recovery flags and permit a service visit')
  bot.data.quests_synced=true; bot.data.waypoints_synced=true; service_needed=true
  assert(states.decide.tick(bot)=='town','same-game recovery must visit the vendor')
  service_tick=function() inventory={}; me.gold=714; service_needed=false end
  assert(states.town.tick(bot)=='decide' and #inventory==0 and me.gold==714,
    'completed town services must retain their sale proceeds in the same game')
  assert(states.decide.tick(bot)=='travel' and route_goal==8,'after shopping the unfinished Den remains the goal')
  me.area=8; me.in_town=false
  assert(states.decide.tick(bot)=='clear','returning to the same Den must resume its clear')

  fighter.out_of_mana=function() return false end
  inventory={}; me.gold=0; me.life=49; bot.state='clear'; tick()
  assert(bot.state=='retreat','non-emergency low life without healing must preserve the unfinished Den')
  service.portal_scrolls=function() return 1 end
  bot.state='clear'; tick()
  assert(bot.state=='portal_heal','an available portal remains the preferred recovery path')
  service.portal_scrolls=function() return 0 end
  states.retreat.enter(bot)
  trip.tick=function() return 'failed' end
  local invalidated,old_invalidate=0,map.invalidate
  map.invalidate=function() invalidated=invalidated+1 end
  assert(states.retreat.tick(bot)==nil and invalidated==1 and chosen==1,
    'a failed walk home is planned once more: leaving the game from the Den resets its clear')
  assert(states.retreat.tick(bot)=='save_exit','a failed recovery path must exit finitely')
  map.invalidate=old_invalidate; bot.data.retreat_replanned=nil
  trip.tick=old_trip; blocked=true
  t=bot.data.retreat_started+60000
  assert(states.retreat.tick(bot)==nil,'the walk out of the Den is given longer: leaving the game there undoes the clear')
  t=bot.data.retreat_started+150000
  assert(states.retreat.tick(bot)=='save_exit','blockers cannot monopolize the retreat indefinitely')
  do -- a level gained on the walk home refills life and mana: turn back
    local kept_life,kept_mana,kept_max=me.life,me.mana,me.max_mana
    local kept_started,kept_den=bot.data.retreat_started,bot.data.retreat_den
    me.life,me.mana,me.max_mana=me.max_life,40,40
    assert(states.retreat.tick(bot)=='decide' and not bot.data.retreat_started,
      'a retreat whose reason is gone returns to the plan')
    me.life,me.mana,me.max_mana=kept_life,kept_mana,kept_max
    bot.data.retreat_started,bot.data.retreat_den=kept_started,kept_den
  end
  local dodges=0
  fighter.dodge=function() dodges=dodges+1; return true end
  bot.state='retreat'; me.life=100; me.x=me.x+6
  tick()
  assert(bot.state=='save_exit' and dodges==0,'global dodging must not bypass the recovery deadline')
  fighter.dodge=function() return false end
  bot.data.retreat_started=t
  bot.state='retreat'; me.life=10
  fighter.hostiles=function() return {{id=80}} end
  tick()
  assert(bot.state=='save_exit','emergency chicken must still override same-game Den recovery')
  bot.data.retreat_started=t
  me.life=100; me.can_act=false; bot.state='retreat'; me.x=me.x+6
  tick(); t=t+5001; tick()
  assert(bot.state=='save_exit','a frozen recovery client must retain the emergency exit')
  me.can_act=true; bot.data.retreat_started=nil; bot.data.retreat_den=nil
  do
    local old_ui,old_waypoints,old_key=game.ui,game.waypoints,act.key
    local old_known=data['act1.RouteTest.wp.6']
    local open,closed,owner_ticks=false,0,0
    game.ui=function() return open end
    game.waypoints=function() return {[6]=true} end
    act.key=function(key) assert(key==27); open=false; closed=closed+1 end
    fighter.hostiles=function() return {} end
    fighter.dodge=function() dodges=dodges+1; return true end
    bot.tick=function(self)
      owner_ticks=owner_ticks+1
      local next_state=states.activate_waypoint.tick(self)
      if next_state then self:go(next_state) end
    end
    me.area,me.life=6,100
    bot.state='activate_waypoint'
    bot.data.wp_since,bot.data.wp_timeout=t-90001,90000
    bot.data.wp_spawn={x=100,y=100}
    tick()
    assert(bot.state=='travel' and bot.data.no_wp==6 and owner_ticks==1 and dodges==0,
      'global dodging must not bypass an expired waypoint attempt')
    bot.state='activate_waypoint'; bot.data.no_wp=nil; open=true; me.x=me.x+6
    tick()
    assert(bot.state=='travel' and closed==1 and not open and owner_ticks==2 and dodges==0,
      'the expired waypoint owner must still acknowledge and close an already open panel')
    game.ui,game.waypoints,act.key=old_ui,old_waypoints,old_key
    data['act1.RouteTest.wp.6']=old_known
    bot.data.wp_since,bot.data.wp_timeout=nil,nil
  end
  do
    local old_ui,old_log,old_elapsed=game.ui,act.quest_log,bot.elapsed
    local open,closed,owner_ticks=true,0,0
    game.ui=function(name) return name=='quests' and open end
    act.quest_log=function(want) assert(want==false); open=false; closed=closed+1 end
    bot.elapsed=function() return 10001 end
    bot.tick=function(self)
      owner_ticks=owner_ticks+1
      local next_state=states.sync_quests.tick(self)
      if next_state then self:go(next_state) end
    end
    bot.state='sync_quests'; states.sync_quests.enter(bot); me.x=me.x+6
    tick()
    assert(bot.state=='sync_quests' and closed==1 and not open and owner_ticks==1 and dodges==0,
      'expired field quest sync must close its panel even when global dodges consume every tick')
    tick()
    assert(bot.state=='save_exit' and owner_ticks==2 and dodges==0,
      'expired quest sync must reset finitely after closing the panel')
    game.ui,act.quest_log,bot.elapsed=old_ui,old_log,old_elapsed
    bot.data.quest_sync=nil
  end
  service_needed,service_tick=false,nil
  travel.route,charon.now,service.portal_scrolls,game.items=old_route,old_now,old_scrolls,old_items
  fighter.threat,fighter.hostiles,fighter.out_of_mana,fighter.fight,fighter.dodge=old_threat,old_hostiles,old_dry,old_fight,old_dodge
  trip.tick,bot.tick=old_trip,old_bot_tick
end
do
  local old_items,old_waypoints,old_quest,old_units=game.items,game.waypoints,game.quest,game.units
  local old_threat,old_hostiles,old_fight=fighter.threat,fighter.hostiles,fighter.fight
  local old_map,old_mana,old_max_mana=map,me.mana,me.max_mana
  map={line_of_sight=function() return true end}
  local carried=6
  game.items=function()
    local items={}
    for i=1,carried do items[i]={code=i==1 and 'rvs' or 'mp1',location='belt'} end
    return items
  end
  game.waypoints=function() return {} end
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  den=true; me.class='sorceress'; me.difficulty=0; me.level=5; me.area=3; me.in_town=false; me.gold=0
  data['act1.RouteTest.gold_farm']=nil
  assert(states.decide.tick(machine())=='travel' and route_goal==37 and not data['act1.RouteTest.tower_prep'],
    'preparation must not divert the sorceress before reaching Black Marsh')
  me.area=6
  me.level=4
  assert(states.decide.tick(machine())=='travel' and route_goal==37 and not data['act1.RouteTest.tower_prep'],
    'reaching Black Marsh before level five must not start preparation early')
  me.level=5; me.area=7
  game.waypoints=function() return {[6]=true} end
  assert(states.decide.tick(machine())=='travel' and route_goal==25 and data['act1.RouteTest.tower_prep'],
    'reaching level five after activating Black Marsh must still start preparation')
  me.area=6; game.waypoints=function() return {} end
  assert(states.decide.tick(machine())=='travel' and route_goal==25 and chosen==25 and data['act1.RouteTest.tower_prep'],
    'level-five Normal sorceress reaching Black Marsh must prepare in the Tower despite its area level')
  carried=2
  assert(states.decide.tick(machine())=='travel' and route_goal==25,
    'two carried reserves permit Tower entry when six are genuinely unaffordable')
  for _,count in ipairs({2,4,5}) do
    carried=count; me.gold=884; data['act1.RouteTest.gold_farm']=nil
    assert(states.decide.tick(machine())=='travel' and route_goal==25 and not data['act1.RouteTest.gold_farm'],
      'two to five carried reserves must permit Tower departure without starting funded recovery')
  end
  carried=0; data['act1.RouteTest.gold_farm']=nil
  assert(states.decide.tick(machine())=='retreat' and data['act1.RouteTest.gold_farm'],
    'zero carried reserves must still restock before Tower departure')
  carried=1; me.gold=300
  assert(states.decide.tick(machine())=='retreat' and data['act1.RouteTest.gold_farm'],
    'affordable six-potion reserve must be bought before Tower departure')
  me.area=1; me.in_town=true
  local m=machine(); m.data.town_done=nil
  assert(states.decide.tick(m)=='town','Tower reserve must use existing recovery shopping')
  me.in_town=false; me.area=6; carried=0; me.gold=0
  assert(states.decide.tick(machine())=='farm','truly poor preparation must retain resource recovery instead of entering dry')
  carried=2
  assert(states.farm.tick(machine())=='decide','Tower recovery must end at two carried reserves when six are unaffordable')
  assert(states.decide.tick(machine())=='travel' and route_goal==25 and not data['act1.RouteTest.gold_farm'],
    'an earlier shortage must not change the two-reserve Tower entry rule')
  carried=6
  assert(states.decide.tick(machine())=='travel' and route_goal==25,
    'a persistent preparation phase must resume after resupply')
  local old_scrolls,old_route=service.portal_scrolls,package.loaded['lib.travel'].route
  package.loaded['lib.travel'].route=function(from,to)
    if from==21 and to==1 then return {{to=20},{to=6},{to=1}} end
    return old_route(from,to)
  end
  me.area=21; carried=5; me.gold=60
  data['act1.RouteTest.gold_farm']=true
  assert(states.decide.tick(machine())=='travel' and route_goal==25 and not data['act1.RouteTest.gold_farm'],
    'an ongoing Tower descent must not chase six reserves after consuming one potion')
  carried=1; me.gold=360; me.mana=0; me.max_mana=100
  data['act1.RouteTest.gold_farm']=true
  assert(states.decide.tick(machine())=='travel' and route_goal==25 and not data['act1.RouteTest.gold_farm'],
    'one carried reserve must commit to the Tower descent even when six are affordable')
  carried=0; me.mana=100
  data['act1.RouteTest.gold_farm']=true
  assert(states.decide.tick(machine())=='travel' and route_goal==25 and not data['act1.RouteTest.gold_farm'],
    'full mana with no potions must continue the Tower descent instead of shopping')
  for _,resupply in ipairs({{potions=1,mana=0},{potions=0,mana=50}}) do
    local retreat=machine(); retreat.data.farm_area=2; retreat.data.farm_commit={area=2,mode='gold'}
    data['act1.RouteTest.gold_farm']=true
    carried,me.mana=resupply.potions,resupply.mana
    assert(states.farm.tick(retreat)=='decide' and not retreat.data.farm_commit,
      'a potion pickup or mana regeneration must cancel an in-progress resource retreat inside the Tower')
    assert(states.decide.tick(retreat)=='travel' and route_goal==25 and not data['act1.RouteTest.gold_farm'])
  end
  carried=0
  me.area=25; me.mana=50
  assert(states.decide.tick(machine())=='countess','half mana with no reserves must still reach the Countess state')
  me.area=21; me.mana=49; service.portal_scrolls=function() return 1 end
  assert(states.decide.tick(machine())=='portal_heal','below half mana without a carried reserve must preserve descent through a portal')
  service.portal_scrolls=function() return 0 end
  assert(states.decide.tick(machine())=='save_exit','a funded Tower shortage without a portal retains finite town recovery')
  game.units=function(kind) return kind=='object' and {{x=me.x,y=me.y,portal=true,owner_name=me.name}} or {} end
  assert(states.decide.tick(machine())=='portal_heal','an existing owned portal preserves the descent without another scroll')
  game.units=function(kind) return kind=='object' and {{x=me.x,y=me.y,portal=true,class_id=59,owner_name=''}} or {} end
  assert(states.decide.tick(machine())=='portal_heal','portal availability must match the delayed-owner fallback used by portal recovery')
  game.units=old_units
  do
    local old_dry,old_dodge,old_tick,old_state,old_data=fighter.out_of_mana,fighter.dodge,bot.tick,bot.state,bot.data
    fighter.out_of_mana=function() return true end
    fighter.threat=function() return {id=90} end
    fighter.dodge=function() return false end
    bot.tick=function() end
    bot.state='travel'; bot.data={quests_synced=true}
    me.alive=true; me.can_act=true; me.life=me.max_life; me.mana=0
    service.portal_scrolls=function() return 1 end
    local old_ran_dry,old_gold=fighter.dry,me.gold
    fighter.dry=function() return true end
    me.gold=0; me.stash_gold=0
    tick()
    assert(bot.state=='travel','dry on the way with nothing to buy mana with, the walk goes on')
    me.gold=360
    tick()
    assert(bot.state=='portal_heal','actual dry combat must still recover during a committed Tower descent')
    service.portal_scrolls=function() return 0 end
    bot.state='travel'; tick()
    assert(bot.state=='travel','dry on the way without a portal is no reason to leave the game')
    service.portal_scrolls=function() return 1 end
    fighter.dry,me.gold=old_ran_dry,old_gold
    fighter.out_of_mana,fighter.dodge,bot.tick,bot.state,bot.data=old_dry,old_dodge,old_tick,old_state,old_data
    fighter.threat=function() return nil end
  end
  local restock=machine(); states.portal_heal.enter(restock)
  me.area=1; me.in_town=true; carried=2; me.gold=240
  assert(states.portal_heal.tick(restock)=='decide' and restock.data.return_logged,
    'a Tower portal return needs two carried reserves, even when six are affordable in town')
  me.area=6; me.in_town=false
  assert(states.decide.tick(machine())=='retreat','already active recovery must still buy six affordable reserves before Tower departure')
  carried=0; me.mana=100
  assert(states.decide.tick(machine())=='retreat','full mana must not bypass the Black Marsh departure reserve')
  me.area=3
  assert(states.decide.tick(machine())=='retreat','full mana must not bypass reserve recovery outside the Tower')
  service.portal_scrolls,package.loaded['lib.travel'].route=old_scrolls,old_route
  carried=6; me.gold=0
  me.area=25
  assert(states.decide.tick(machine())=='countess','preparation must use the Countess state in her cellar')
  game.quest=function(id) return {completed=id==1,completed_this_game=id==5} end
  m=machine(); states.countess.enter(m)
  data['act1.RouteTest.tower_cleared']=nil;safe_approach=true
  assert(states.countess.tick(m)==nil and m.data.countess_dead and not m.data.countess_done
    and not data['act1.RouteTest.tower_cleared'],'death alone must not record the first clear while loot work is still active')
  safe_approach=false
  assert(states.countess.tick(m)=='decide' and m.data.countess_done and m.data.countess_dead,
    'actual Countess credit must finish and loot the successful run')
  assert(data['act1.RouteTest.tower_cleared'],'only actual Countess death and completed looting must record the first clear')
  assert(states.decide.tick(m)=='save_exit','a still-unprepared build may choose another cleared Countess descent')
  me.area=1; me.in_town=true
  game.quest=function(id) return {completed=id==1 or id==5} end
  assert(states.decide.tick(machine())=='travel' and route_goal==25,
    'new game-local flags must allow another run while the persistent preparation phase survives')
  me.area=25; me.in_town=false
  local fights=0
  fighter.fight=function() fights=fights+1 end
  game.units=function(kind) return kind=='monster' and {{id=99,name='The Countess',alive=true,x=1,y=0}} or {} end
  m=machine(); states.countess.enter(m)
  assert(states.countess.tick(m)==nil and fights==1 and not m.data.countess_dead and not m.data.countess_done,
    'persistent quest completion must not replace killing the living Countess in a new game')
  local searched=0
  map.line_of_sight=function() return false end
  exploration_stub={tick=function() searched=searched+1; return 'moving' end}
  m=machine(); states.countess.enter(m)
  assert(states.countess.tick(m)==nil and fights==1 and searched==1,
    'a loaded Countess behind a wall must leave navigation active instead of repeating an empty fight')
  map.line_of_sight=function() return true end
  exploration_stub=nil
  game.units=old_units
  me.level=11
  assert(states.decide.tick(machine())=='save_exit' and data['act1.RouteTest.tower_prep_done']
    and not data['act1.RouteTest.tower_prep'],'finishing inside the Tower must exit instead of walking up five floors')
  me.area=1; me.in_town=true
  assert(states.decide.tick(machine())=='travel' and route_goal==37 and data['act1.RouteTest.tower_prep_done']
    and not data['act1.RouteTest.tower_prep'],'level eleven must resume Andariel with no rune requirement')
  game.waypoints=function() return {[6]=true} end
  m=machine()
  assert(states.decide.tick(m)=='take_waypoint' and m.data.waypoint_to==6,
    'completed preparation must use the Black Marsh waypoint toward Andariel when known')
  game.waypoints=function() return {} end
  me.level=7; me.class='barbarian'; me.area=6; me.in_town=false
  assert(states.decide.tick(machine())=='travel' and route_goal==37,'supplied other classes must keep their main route')
  me.class='sorceress'; me.level=5; me.difficulty=1
  data['act1.RouteTest.tower_prep_done']=nil
  assert(states.decide.tick(machine())=='travel' and route_goal==37 and not data['act1.RouteTest.tower_prep'],
    'the targeted preparation route applies only to Normal')
  me.difficulty=0; me.area=6
  states.decide.tick(machine())
  me.area=25
  game.quest=function(id) return {completed=id==1} end
  game.units=function() return {} end
  exploration_stub={tick=function() return 'done' end,cleanup=function() return false end}
  for attempt=1,3 do
    m=machine(); states.countess.enter(m)
    assert(states.countess.tick(m)==(attempt<3 and 'save_exit' or 'decide') and m.data.countess_failed
      and not m.data.countess_done and not m.data.countess_dead,'failed Countess sweeps must abort and reconsider the optional job on attempt three')
    assert(data['act1.RouteTest.tower_failures']==attempt,'failure budget must survive fresh game-local state')
  end
  assert(states.decide.tick(machine())=='save_exit' and data['act1.RouteTest.tower_prep_done'],
    'exhausted preparation retries must leave the Tower for the main route without failing Act 1')
  exploration_stub=nil
  for _,flag in ipairs({'tower_prep','tower_prep_done','tower_failures','tower_cleared','tower_started','gold_farm'}) do data['act1.RouteTest.'..flag]=nil end
  game.items,game.waypoints,game.quest,game.units=old_items,old_waypoints,old_quest,old_units
  fighter.threat,fighter.hostiles,fighter.fight=old_threat,old_hostiles,old_fight
  map=old_map
  me.mana,me.max_mana=old_mana,old_max_mana
end

do
  local strategy=require 'lib.strategy'
  local strategy_map=map
  map={presets=function() return {} end,line_of_sight=function() return true end}
  local leveling,routing=package.loaded['lib.leveling'],package.loaded['lib.travel']
  local old_choose,old_prior,old_route=strategy.choose,leveling.prior,routing.route
  local old_known,old_skills=fighter.known_skills,game.skills
  local old_items,old_waypoints,old_quest,old_now=game.items,game.waypoints,game.quest,charon.now
  local before_me={}; for name,value in pairs(me) do before_me[name]=value end
  local keys={'tower_prep','tower_prep_done','tower_started','tower_cleared','tower_failures','gold_farm'}
  local before_flags={}; for _,name in ipairs(keys) do before_flags[name]=data['act1.RouteTest.'..name] end
  local seen,known={},{}
  local t,wp,quest_cleared,missing_tower,missing_main=500000,true,false,false,false
  strategy.choose=function(self,context)
    local action,scores=old_choose(self,context)
    seen[#seen+1]={context=context,action=action,scores=scores}
    return action,scores
  end
  charon.now=function() return t end
  fighter.known_skills=function() return known end
  game.quest=function(id) return {completed=id==1 or id==5 and quest_cleared} end
  game.waypoints=function() return wp and {[6]=true} or {} end
  game.items=function()
    local items={}; for i=1,6 do items[i]={code='mp1',location='belt'} end; return items
  end
  leveling.prior=function(area,which,level)
    assert((area==25 or area==7) and which.element=='lightning' and level==me.level,
      'strategy must compare Countess with the next Black Marsh main-route area using the build prior')
    return 1
  end
  routing.route=function(from,to)
    if to==25 and missing_tower then return nil end
    if from==6 and to==37 then return not missing_main and {{to=7},{to=26},{to=37}} or nil end
    if from==to then return {} end
    if to==25 then
      local route={}; if from==1 then route[#route+1]={to=6} end
      for area=from>=20 and from+1 or 20,25 do route[#route+1]={to=area} end
      return route
    end
    if to==7 then return from==6 and {{to=7}} or {{to=6},{to=7}} end
    if to==37 then return {{to=6},{to=7},{to=37}} end
    return {{to=to}}
  end
  local function reset()
    for _,name in ipairs(keys) do data['act1.RouteTest.'..name]=nil end
    me.class,me.difficulty,me.level,me.area,me.in_town,me.gold='sorceress',0,5,6,false,0
    me.life,me.max_life,me.mana,me.max_mana=100,100,100,100
    known={['Charged Bolt']={base_level=3,level=3}}
    wp,quest_cleared,missing_tower,missing_main=true,false,false,false
  end
  local function prepared()
    known={['Charged Bolt']={base_level=4,level=4,usable=false},
      ['Frost Nova']={base_level=1,level=1,usable=false},['Static Field']={base_level=1,level=1,usable=false}}
  end
  reset()
  assert(states.decide.tick(machine())=='travel' and seen[#seen].action=='tower',
    'missing controls at level five must select optional Tower preparation')
  local initial=data['act1.RouteTest.tower_started']
  local context=seen[#seen].context
  assert(initial==t and context.tower_ms==0 and context.first_clear and not context.reset
    and context.tower_hops==6 and context.progress_hops==1,'first selection must start the wall clock and compare actual route hops')
  t=t+1000; me.area=1; me.in_town=true
  assert(states.decide.tick(machine())=='take_waypoint' and data['act1.RouteTest.tower_started']==initial
    and seen[#seen].context.tower_ms==1000 and seen[#seen].context.progress_hops==2,
    'town and fresh game-local state must preserve preparation time and waypoint approach cost')
  me.area=21; me.in_town=false; me.level=6; prepared()
  assert(states.decide.tick(machine())=='travel' and seen[#seen].action=='tower' and seen[#seen].context.committed,
    'a prepared level-six character must finish a committed descent')
  me.area=25; data['act1.RouteTest.tower_cleared']=true
  known={['Charged Bolt']={base_level=3,level=3}}
  local m=machine();m.data.countess_done=true;t=t+1000
  assert(states.decide.tick(m)=='save_exit' and seen[#seen].action=='tower','missing controls may justify refreshing a completed descent')
  context=seen[#seen].context
  assert(context.reset and not context.first_clear and not context.committed and context.tower_ms==2000
    and context.tower_hops==6 and data['act1.RouteTest.tower_started']==initial,
    'repeat preparation must cost a fresh Black Marsh descent and retain its original wall clock')
  wp=false;states.decide.tick(m)
  assert(seen[#seen].context.tower_hops==7,'a repeat without the Black Marsh waypoint must cost the route from town')
  wp=true;prepared();me.mana=0;me.level=10
  assert(states.decide.tick(m)=='save_exit' and seen[#seen].action=='progress' and data['act1.RouteTest.tower_prep_done']
    and not data['act1.RouteTest.tower_prep'],'prepared level ten after Countess must exit for the main route rather than repeat until eleven')
  context=seen[#seen].context
  assert(context.bolt_rank==4 and context.nova and context.static,'learned readiness must survive unusable skills and temporary zero mana')
  local calls=#seen;me.area=6;me.level=5;known={}
  assert(states.decide.tick(machine())=='travel' and chosen==7 and #seen==calls,
    'permanent optional completion must not re-enter Tower preparation')
  reset();known={['Charged Bolt']={base_level=0,level=8},['Frost Nova']={base_level=0,level=3},['Static Field']={base_level=1,level=0}}
  states.decide.tick(machine());context=seen[#seen].context
  assert(context.bolt_rank==0 and not context.nova and not context.static,'rank-zero grants and absent effective controls cannot establish learned readiness')
  reset();fighter.known_skills=saved['lib.combat'].known_skills
  game.skills=function() return {{name='Charged Bolt',base_level=1,level=8,owner=123},
    {name='Frost Nova',base_level=1,level=3,owner=123},{name='Static Field',base_level=1,level=3,owner=123}} end
  states.decide.tick(machine());context=seen[#seen].context
  assert(context.bolt_rank==0 and not context.nova and not context.static,'item-owned grants must remain excluded by innate skill collection')
  fighter.known_skills=function() return known end
  reset();me.area=21;data['act1.RouteTest.tower_failures']=3
  assert(states.decide.tick(machine())=='save_exit' and seen[#seen].action=='progress'
    and seen[#seen].context.tower_unavailable and not seen[#seen].context.committed,
    'the failure limit must release optional Tower commitment instead of failing Act 1')
  me.area=1;me.in_town=true
  assert(states.decide.tick(machine())=='take_waypoint','failure-limit completion must preserve the main-route waypoint return')
  reset();me.area=21;data['act1.RouteTest.tower_failures']=1
  m=machine();m.data.countess_failed=true
  assert(states.decide.tick(m)=='save_exit' and seen[#seen].action=='tower' and not seen[#seen].context.committed
    and not data['act1.RouteTest.tower_prep_done'],'an optional failure may retry only when the model still chooses Tower')
  assert(seen[#seen].context.reset and seen[#seen].context.tower_hops==6,
    'a failed sweep retry must include the reset and a fresh descent from the known Black Marsh waypoint')
  wp=false;states.decide.tick(m)
  assert(seen[#seen].context.reset and seen[#seen].context.tower_hops==7,
    'a failed sweep without its waypoint must include the full town departure cost')
  reset();me.area=21;me.level=10;prepared();data['act1.RouteTest.tower_cleared']=true;data['act1.RouteTest.tower_failures']=1
  m=machine();m.data.countess_failed=true
  assert(states.decide.tick(m)=='save_exit' and seen[#seen].action=='progress' and data['act1.RouteTest.tower_prep_done'],
    'a prepared build may abandon a failed optional attempt before exhausting the retry cap')
  reset();missing_tower=true
  assert(states.decide.tick(machine())=='travel' and chosen==7 and seen[#seen].context.tower_unavailable
    and seen[#seen].scores[2]==nil,'a missing optional Tower route must leave the main route available')
  reset();missing_main=true;calls=#seen
  assert(states.decide.tick(machine())=='failed' and #seen==calls and not data['act1.RouteTest.tower_prep_done'],
    'an unknown next main area must retain existing missing-route failure without inventing a strategy context')
  reset();quest_cleared=true;states.decide.tick(machine())
  assert(not seen[#seen].context.first_clear,'persistent Tower quest credit also excludes first-clear utility')
  reset();leveling.prior=function() return nil end;states.decide.tick(machine())
  assert(seen[#seen].context.tower_value==0 and seen[#seen].context.progress_value==0,'missing raw priors must be explicit zero observations')
  leveling.prior=old_prior
  for _,class in ipairs{'barbarian','sorceress'} do
    reset();me.class=class;me.difficulty=class=='sorceress' and 1 or 0;calls=#seen
    assert(states.decide.tick(machine())=='travel' and #seen==calls,'other classes and non-Normal games must retain existing routing')
  end
  strategy.choose,leveling.prior,routing.route=old_choose,old_prior,old_route
  map=strategy_map
  fighter.known_skills,game.skills=old_known,old_skills
  game.items,game.waypoints,game.quest,charon.now=old_items,old_waypoints,old_quest,old_now
  for name in pairs(me) do me[name]=nil end;for name,value in pairs(before_me) do me[name]=value end
  for _,name in ipairs(keys) do data['act1.RouteTest.'..name]=before_flags[name] end
end

do
  local old_trip,old_target,old_reason,old_map=trip.tick,trip.target,trip.reason,map
  local old_threat,old_hostiles,old_units=fighter.threat,fighter.hostiles,game.units
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  game.units=function() return {} end
  map={invalidate=function() end}
  trip.target,trip.reason=22,'movement refused'
  for attempt=1,4 do
    me.area=21; me.in_town=false
    trip.tick=function() return 'failed' end
    local m=machine(); m.data.failures=3
    assert(states.travel.tick(m)==(attempt<4 and 'save_exit' or 'failed'),
      'repeated failures at the same destination must exhaust the persistent game-reset budget')
    me.area=6; trip.tick=function() return 'arrived' end
    assert(states.travel.tick(machine())=='decide' and data['act1.RouteTest.travel_resets']==attempt,
      'reaching an earlier waypoint after rejoining must not erase the failing destination budget')
  end
  me.area=22
  assert(states.travel.tick(machine())=='decide' and not data['act1.RouteTest.travel_resets']
    and not data['act1.RouteTest.travel_reset_target'],'reaching the failed destination clears its recovery budget')
  trip.tick,trip.target,trip.reason,map=old_trip,old_target,old_reason,old_map
  fighter.threat,fighter.hostiles,game.units=old_threat,old_hostiles,old_units
end
do
  local old_items,old_units,old_quest,old_map=game.items,game.units,game.quest,map
  local old_scrolls,old_antidote=service.portal_scrolls,service.prepare_antidote
  local old_hostiles,old_fight=fighter.hostiles,fighter.fight
  local inventory,close,scrolls,fights,complete={},false,1,0,false
  game.items=function() return inventory end
  game.units=function(kind) return kind=='monster' and {
    {id=90,name='The Countess',class_id=156,alive=true,x=10,y=0}} or {} end
  game.quest=function(id) return {completed=complete and id==6} end
  map={line_of_sight=function() return true end}
  service.portal_scrolls=function() return scrolls end
  service.prepare_antidote=function() end
  fighter.hostiles=function() return close and {{id=91}} or {} end
  fighter.fight=function() fights=fights+1 end
  me.in_town=false; me.level=7; me.x=0; me.y=0; me.gold=180; me.stash_gold=0
  for _,entry in ipairs({{'countess',25},{'andariel',37}}) do
    local state=states[entry[1]]
    me.area=entry[2]
    local m=machine()
    assert(state.tick(m)=='portal_heal' and m.data.boss_supply_attempted==me.area,
      'an empty healing reserve must be restocked from a quiet boss approach')
    local before=fights
    assert(state.tick(m)==nil and fights==before+1,
      'a failed healing purchase must not loop through the same pre-boss visit')
    for _,item in ipairs({{code='hp1',location='container',page=0},{code='rvs',location='belt',grid_x=0}}) do
      inventory={item,item,item}
      local fresh=machine()
      fresh.data.boss_supply_attempted=me.area
      assert(state.tick(fresh)==nil and not fresh.data.boss_supply_attempted,
        'a boss reserve of healing or rejuvenation must suppress the visit and clear its attempt latch')
    end
    inventory={}; me.gold=0; me.stash_gold=180
    assert(state.tick(m)==nil,'once the fight is on, the reserve is no reason to leave it')
    assert(state.tick(machine())=='portal_heal','stash gold must fund healing restock')
    me.stash_gold=0; inventory={{code='jav',location='container',page=0}}
    assert(state.tick(machine())=='portal_heal','saleable loot must fund healing before approaching the boss')
    inventory={}
    assert(state.tick(machine())==nil,'an unfunded boss approach must not take an empty shopping trip')
    me.gold=120
    assert(state.tick(machine())==nil,'cash for two mana potions alone cannot fund healing as well')
    inventory={{code='mp1',location='container',page=0},{code='mp1',location='container',page=0}}
    me.gold=60
    assert(state.tick(machine())=='portal_heal','carried mana leaves the full healing budget available')
    inventory={}
    me.gold=180; close=true
    assert(state.tick(machine())==nil,'close threats must be handled before pre-boss shopping')
    close=false; scrolls=0
    assert(state.tick(machine())==nil,'pre-boss shopping must not require walking back through the dungeon')
    scrolls=1
  end
  complete=true
  assert(states.andariel.tick(machine())=='done','quest completion must bypass supply preparation')
  game.items,game.units,game.quest,map=old_items,old_units,old_quest,old_map
  service.portal_scrolls,service.prepare_antidote=old_scrolls,old_antidote
  fighter.hostiles,fighter.fight=old_hostiles,old_fight
end
do
  local old_items,old_info,old_quest,old_units=game.items,game.level_info,game.quest,game.units
  local old_act,old_scrolls,old_trip=act,service.portal_scrolls,trip.tick
  local old_hostiles,old_threat,old_lanes=fighter.hostiles,fighter.threat,fighter.missile_lanes
  local inventory,close,ready,scrolls,anchor={},false,true,1,false
  local advances=0
  act={ready=function() return ready end}
  service.portal_scrolls=function() return scrolls end
  trip.tick=function() advances=advances+1; return 'moving' end
  fighter.hostiles=function(_,_,radius) return close and radius==12 and {{id=90}} or {} end
  fighter.threat=function() return nil end
  fighter.missile_lanes={}
  game.items=function() return inventory end
  game.units=function() return {} end
  game.level_info=function() return {waypoint=anchor} end
  game.quest=function(id) return {completed=id==1 and den} end
  me.area=4; me.in_town=false; me.gold=180; me.stash_gold=0; den=true
  local m=machine()
  assert(states.travel.tick(m)=='portal_heal' and advances==0,
    'quiet travel with empty healing reserves and a funded minimum mana-plus-healing budget must restock')
  inventory={{code='hp1',location='belt'}}
  assert(states.travel.tick(m)==nil and advances==1,
    'a failed shopping visit leaving one healing potion must not loop in the same area')
  me.area=5
  assert(states.travel.tick(m)=='portal_heal','a different area permits its own quiet restock attempt')
  me.area=4
  assert(states.travel.tick(m)==nil,'returning to an earlier area must retain its failed-purchase latch')
  inventory={{code='hp1',location='belt'},{code='rvs',location='container',page=0}}
  assert(states.travel.tick(m)==nil and not m.data.travel_supply_attempted,
    'two carried HP/RV across belt and inventory must clear failed-purchase latches without shopping')
  inventory={{code='hp1',location='container',page=0},{code='hp1',location='container',page=4},
    {code='rvs',location='ground'}}
  assert(states.travel.tick(m)=='portal_heal','stash and ground potions must not inflate the one-potion carried reserve')
  me.gold=179
  assert(states.travel.tick(machine())==nil,'shopping must reserve the existing two-mana deficit before buying healing')
  inventory={{code='rvs',location='belt'}}; me.gold=120
  assert(states.travel.tick(machine())==nil,
    'a rejuvenation cannot reduce the actual mana purchases that town services makes before buying healing')
  me.gold=180
  assert(states.travel.tick(machine())=='portal_heal','a rejuvenation reserve still permits restocking when all purchases are funded')
  me.gold=0; me.stash_gold=60
  inventory={{code='mp1',location='belt'},{code='mp1',location='container',page=0}}
  assert(states.travel.tick(machine())=='portal_heal','carried mana and stash gold must fund two healing potions')
  me.stash_gold=0; inventory={{code='jav',location='container',page=0}}
  assert(states.travel.tick(machine())=='portal_heal','sellable inventory loot may fund a quiet healing restock')
  inventory={}
  assert(states.travel.tick(machine())==nil,'unfunded travel must continue instead of taking an empty shopping trip')
  me.gold=180; close=true
  assert(states.travel.tick(machine())==nil,'a hostile within twelve subtiles must block discretionary shopping')
  close=false; fighter.missile_lanes={{}}
  assert(states.travel.tick(machine())==nil,'observed missile lanes must block discretionary shopping')
  fighter.missile_lanes={}; ready=false
  assert(states.travel.tick(machine())==nil,'an uninterruptible action must finish before discretionary shopping')
  ready=true; scrolls=0
  assert(states.travel.tick(machine())==nil,'quiet restock must have a portal scroll')
  scrolls=1; me.in_town=true; me.area=1
  assert(states.travel.tick(machine())==nil,'travel inside town must not request a portal')
  me.in_town=false; me.area=8; den=false
  assert(states.travel.tick(machine())==nil,'an unfinished Den must preserve its early recovery economy')
  me.area=2
  assert(states.travel.tick(machine())==nil,'the quiet Blood Moor approach to an unfinished Den must also keep its opening economy')
  me.area=8
  den=true
  assert(states.travel.tick(machine())=='portal_heal','a completed Den may use quiet travel restocking')
  me.area=6; anchor=true
  assert(states.travel.tick(machine())=='activate_waypoint','a restart anchor must take priority over discretionary restocking')
  game.items,game.level_info,game.quest,game.units=old_items,old_info,old_quest,old_units
  act,service.portal_scrolls,trip.tick=old_act,old_scrolls,old_trip
  fighter.hostiles,fighter.threat,fighter.missile_lanes=old_hostiles,old_threat,old_lanes
end
do
  local old_threat,old_dodge,old_heading=fighter.threat,fighter.dodge,trip.dodge_heading
  local old_tick,old_now,old_map=bot.tick,charon.now,map
  local selected,received,queries=nil,nil,0
  local t=1000000
  charon.now=function() return t end
  map={line_of_sight=function() return true end}
  fighter.threat=function(_,_,_,accept)
    return selected and (not accept or accept(selected)) and selected or nil
  end
  trip.dodge_heading=function(_,player) assert(player==me); queries=queries+1; return nil end
  fighter.dodge=function(_,player,target,passage,retain_engagement)
    received={target=target,passage=passage,retain_engagement=retain_engagement}
    if passage then passage(player) end
    return true
  end
  bot.tick=function() error('a consumed dodge must retain tick ownership') end
  me.area,me.level,me.in_town,me.alive,me.can_act=4,9,false,true,true
  me.life,me.max_life,me.gold,me.stash_gold=100,100,1000,0
  bot.state,bot.data='travel',{}
  fighter.target_id=999 -- no longer accepted for travel
  tick()
  assert(not received.target and type(received.passage)=='function' and queries==1 and not received.retain_engagement,
    'targetless travel must explicitly suppress stale combat retention even when the heading is unavailable')
  selected={id=80,x=me.x+3,y=me.y,life_percent=100}
  t=t+40; tick()
  assert(received.target==selected and not received.passage and queries==1,
    'an accepted fresh travel threat must take precedence over passage direction')
  do
    local old_unit,old_usable,old_sight,old_build=game.unit,fighter.usable,fighter.in_sight,fighter.build
    local old_ignored,old_cache,old_priority=fighter.ignored,fighter.sight,require('lib.value').priority
    local old_plan,old_retargeted=fighter.plan,fighter.retargeted
    local value=require('lib.value')
    local fresh,visible,lookups,searches
    fighter.usable,fighter.in_sight=saved['lib.combat'].usable,saved['lib.combat'].in_sight
    fighter.build={element='lightning'}
    fighter.ignored={}
    value.priority=function(unit,player,element)
      assert(player==me and element=='lightning')
      return unit.score or 1
    end
    game.unit=function(kind,id) assert(kind=='monster' and id==999); lookups=lookups+1; return fresh end
    map.line_of_sight=function() return visible end
    fighter.threat=function(_,player,radius,accept)
      assert(player==me and radius==12)
      searches=searches+1
      return selected and accept(selected) and selected or nil
    end
    local plan={missile=true}
    fighter.plan,fighter.retargeted=plan,1234
    for _,case in ipairs({
      {name='accepted held target',held=true},
      {name='missing',missing=true},
      {name='dead',field='alive',value=false},
      {name='unattackable',field='attackable',value=false},
      {name='friendly',field='owner',value=1},
      {name='outside travel sight',distance=12.01,elite=true},
      {name='ignored',ignored=true},
      {name='freshly obscured',visible=false},
      {name='freshly visible',cached=false,held=true},
      {name='travel filter rejected',distance=6},
      {name='immune at four',distance=4,score=-1},
      {name='close immune',distance=3.99,score=-1,held=true},
      {name='engagement boundary',distance=12,elite=true,held=true},
      {name='no held or fresh threat',missing=true,no_candidate=true},
      {name='no target id',no_id=true},
    }) do
      fresh={id=999,type='monster',alive=true,attackable=true,owner=-1,
        x=me.x+(case.distance or 3),y=me.y,unique=case.elite,score=case.score}
      if case.field then fresh[case.field]=case.value end
      if case.missing then fresh=nil end
      visible=case.visible~=false
      fighter.target_id=not case.no_id and 999 or nil
      fighter.sight={[999]={at=t+40,seen=case.cached~=false}}
      fighter.ignored[999]=case.ignored and t+10000 or nil
      selected=not case.no_candidate and {id=80,x=me.x+3,y=me.y} or nil
      lookups,searches=0,0
      local before_queries=queries
      t=t+40; tick()
      assert(received.target==(case.held and fresh or selected),
        'global travel avoidance must validate its held fight target: '..case.name)
      assert(searches==(case.held and 0 or 1) and lookups==(case.no_id and 0 or 1),
        'accepted held targets must bypass fresh selection without retaining a snapshot: '..case.name)
      assert((received.passage~=nil)==(case.no_candidate==true) and not received.retain_engagement,
        'only targetless travel may request passage; extended retention stays disabled: '..case.name)
      assert(fighter.target_id==(not case.no_id and 999 or nil) and fighter.retargeted==1234 and fighter.plan==plan,
        'global preference must not mutate fight ownership: '..case.name)
      if case.no_candidate then queries=before_queries end
    end
    fighter.plan,fighter.retargeted=old_plan,old_retargeted
    game.unit,fighter.usable,fighter.in_sight,fighter.build=old_unit,old_usable,old_sight,old_build
    fighter.ignored,fighter.sight,value.priority=old_ignored,old_cache,old_priority
    fighter.threat=function(_,_,_,accept) return selected and (not accept or accept(selected)) and selected or nil end
    map.line_of_sight=function() return true end
  end
  local asked_before=1
  for _,state in ipairs({'activate_waypoint','take_waypoint','clear','farm','countess','andariel',
    'portal_heal','retreat','collect','decide'}) do
    bot.state=state; bot.data.visit_done=state=='portal_heal'; t=t+40; tick()
    -- The search of a level has somewhere to go as well (its own next point); the rest do not.
    if state=='farm' then asked_before=asked_before+1 end -- its own heading is asked once
    assert(not received.target and (received.passage~=nil)==(state=='farm') and queries==asked_before,
      'other state owners must not borrow the travel mover for global dodge direction')
    assert(received.retain_engagement==(state=='clear' or state=='farm' or state=='countess' or state=='andariel'),
      'expanded combat retention must be disabled during recovery and other noncombat ownership')
  end
  fighter.target_id=nil
  fighter.threat,fighter.dodge,trip.dodge_heading=old_threat,old_dodge,old_heading
  bot.tick,charon.now,map=old_tick,old_now,old_map
end
do
  local old_players,old_items,old_info,old_units,old_ui=game.players,game.items,game.level_info,game.units,game.ui
  local old_act,old_scrolls,old_trip,old_target,old_hop=act,service.portal_scrolls,trip.tick,trip.target,trip.hop
  local old_threat,old_hostiles,old_lanes,old_dodge=fighter.threat,fighter.hostiles,fighter.missile_lanes,fighter.dodge
  local old_observe=fighter.observe_missiles
  local old_bot_tick,old_bot_data,old_bot_state=bot.tick,bot.data,bot.state
  local configured,revision,supported,ack,accepted=8,0,true,true,true
  local wrap,fresh_missile=false,false
  local writes,moved,events=0,0,{}
  local healing,mana,scrolls,close,ready=2,6,1,false,true
  game.players=function() return {supported=supported,configured=configured,revision=revision} end
  act={ready=function() return ready end,players=function(count)
    assert(count>=1 and count<=8,'PX commands must stay in the standard range')
    writes=writes+1; events[#events+1]='P'..count
    if ack then configured=count; revision=wrap and (revision+1)%4294967296 or revision+1 end
    return accepted
  end}
  game.items=function()
    local items={}
    for i=1,healing do items[#items+1]={code='hp1',location=i==1 and 'belt' or 'container',page=0} end
    for i=1,mana do items[#items+1]={code='mp1',location='belt'} end
    return items
  end
  game.level_info=function() return {monster_level=7} end
  game.units=function() return {} end
  game.ui=function() return false end
  service.portal_scrolls=function() return scrolls end
  fighter.threat=function() return nil end
  fighter.hostiles=function(_,_,radius) return close and radius==12 and {{id=91}} or {} end
  fighter.missile_lanes={}
  fighter.observe_missiles=function() if fresh_missile then fighter.missile_lanes={{}} end end
  trip.tick=function() moved=moved+1; events[#events+1]='move'; return 'moving' end
  me.class='sorceress'; me.difficulty=0; me.level=2; me.area=2; me.in_town=false
  me.max_life=100; me.life=85; me.max_mana=100; me.mana=75; me.gold=0; me.stash_gold=0
  den=false; trip.target=8; trip.hop=nil
  local m=machine()
  states.warm.enter(m)
  assert(configured==1 and writes==1,'every new game must reconcile P1 before warming or moving')
  events={}
  states.travel.tick(m)
  assert(configured==8 and events[1]=='P8' and events[2]=='move',
    'a prepared Normal bolt sorceress must raise before the Den hop can move or warp')
  configured=1; revision=4294967295; wrap=true
  states.travel.tick(machine())
  assert(configured==8 and revision==0,'a wrapped native acknowledgement revision must remain valid')
  wrap=false
  local before=writes
  me.life=20; me.mana=0; mana=0; healing=0
  states.travel.tick(m)
  assert(writes==before and configured==8,'falling reserves must not flip the decision during the same hop')
  me.life=100; me.mana=100; mana=6; healing=2; den=true; me.level=6
  me.area=6; trip.target=20; data['act1.RouteTest.tower_prep']=true
  configured=1
  states.travel.tick(m)
  assert(configured==8,'prepared Tower entry before level eleven must use P8')
  do
    local old_known,old_skills,old_right=fighter.known_skills,game.skills,me.right_skill
    local old_cap=data['act1.RouteTest.tower_px_cap']
    fighter.known_skills=saved['lib.combat'].known_skills
    me.right_skill=42 -- selecting Static cannot substitute for a learned point
    for _,case in ipairs({
      {skills={},count=3},
      {skills={{name='Static Field',owner=123,base_level=1,level=1}},count=3},
      {skills={{name='Static Field',owner=-1,base_level=0,level=3}},count=3},
      {skills={{name='Static Field',owner=-1,base_level=1,level=0}},count=3},
      {skills={{name='Static Field',owner=4294967295,base_level=1,level=1,usable=false}},count=8},
      {skills={{name='Static Field',owner=-1,base_level=1,level=1}},cap=3,count=3},
      {skills={},cap=1,count=1},
      {skills={{name='Static Field',owner=-1,base_level=1,level=1}},cap=1,count=1},
      {skills={},mana=5,count=1},
    }) do
      game.skills=function() return case.skills end
      data['act1.RouteTest.tower_px_cap']=case.cap
      mana,configured=case.mana or 6,1
      states.travel.tick(machine())
      assert(configured==case.count,'Tower PX must require learned positive-level Static for P8 while retaining readiness and persisted caps')
    end
    game.skills=function() return {} end
    data['act1.RouteTest.tower_px_cap']=nil
    me.area,trip.target,me.level,den,mana,configured=2,8,2,false,6,1
    states.travel.tick(machine())
    assert(configured==8,'missing Static must not change eligible Den P8 hops')
    me.area,trip.target,me.level,den=6,20,6,true
    fighter.known_skills,game.skills,me.right_skill=old_known,old_skills,old_right
    data['act1.RouteTest.tower_px_cap']=old_cap
  end
  for _,hop in ipairs({{24,25},{36,37},{3,4}}) do
    me.area=hop[1]; trip.target=hop[2]; configured=8
    states.travel.tick(machine())
    assert(configured==1,'Countess, Andariel, and ordinary route hops must reconcile P1 before moving')
  end
  local function blocked(change,restore,why)
    me.area=6; trip.target=20; configured=8
    change(); states.travel.tick(machine()); restore()
    assert(configured==1,why)
  end
  blocked(function() me.life=84 end,function() me.life=100 end,'low life must prevent a P8 raise')
  blocked(function() me.mana=74 end,function() me.mana=100 end,'low mana must prevent a P8 raise')
  blocked(function() healing=1 end,function() healing=2 end,'one healing reserve must prevent a P8 raise')
  blocked(function() mana=5 end,function() mana=6 end,'five mana reserves must prevent a P8 raise')
  blocked(function() scrolls=0 end,function() scrolls=1 end,'no portal scroll must prevent a P8 raise')
  blocked(function() close=true end,function() close=false end,'hostiles within twelve must prevent a P8 raise')
  blocked(function() fighter.missile_lanes={{}} end,function() fighter.missile_lanes={} end,'missile lanes must prevent a P8 raise')
  blocked(function() fresh_missile=true; fighter.missile_lanes={} end,
    function() fresh_missile=false; fighter.missile_lanes={} end,
    'fresh missile observation must prevent a P8 raise even when the previous lane snapshot was empty')
  blocked(function() ready=false end,function() ready=true end,'an uninterruptible action must prevent a P8 raise')
  blocked(function() me.difficulty=1 end,function() me.difficulty=0 end,'other difficulties must keep P1')
  blocked(function() me.class='barbarian' end,function() me.class='sorceress' end,'other classes must keep P1')
  blocked(function() me.level=11 end,function() me.level=6 end,'completed Tower leveling must keep P1')
  me.area=2; trip.target=8; me.level=1; den=false; configured=8
  states.travel.tick(machine()); assert(configured==1,'the level-one opening must keep P1')
  me.level=2; den=true; configured=8
  states.travel.tick(machine()); assert(configured==1,'a completed Den must not start another P8 sweep')
  den=false; me.area=6; trip.target=20; me.level=6
  supported=false; configured=8; before=writes
  m=machine(); states.travel.tick(m); states.travel.tick(m)
  assert(writes==before and m.data.px_unsupported,'joined or unsupported games must leave PX inactive without a command')
  supported=true; configured=1; ack=false
  local walked=moved
  assert(not pcall(states.travel.tick,machine()) and moved==walked,
    'an unacknowledged request must fail finitely before any movement')
  ack=true; accepted=false
  assert(not pcall(states.travel.tick,machine()) and moved==walked,'a rejected native request must stop before movement')
  do
    local old_cap=data['act1.RouteTest.tower_px_cap']
    local old_ready=game.ready
    data['act1.RouteTest.tower_px_cap']=nil
    accepted,ack,configured=true,true,1
    me.alive,me.can_act,me.life,me.mana=true,true,100,100
    me.class,me.difficulty,me.level='sorceress',0,6
    healing,mana,scrolls,ready=2,6,1,true
    den=true
    game.ready=function() return true end
    bot.tick=function() end
    fighter.dodge=function() return false end
    fighter.hostiles=function(_,_,radius) return radius<=8 and {{id=91}} or {} end
    local function new_game()
      me.life=100
      states.warm.enter(machine())
    end
    local function tower_hop()
      me.life,me.area=100,6; trip.target,trip.hop=20,nil
      states.travel.tick(machine())
    end
    local function chicken(area)
      me.area,me.life,me.x=area,10,me.x+6
      bot.state,bot.data='clear',{quests_synced=true}
      tick()
      assert(bot.state=='save_exit','the PX backoff must preserve the emergency exit')
    end
    new_game(); tower_hop()
    chicken(4)
    assert(not data['act1.RouteTest.tower_px_cap'],'a field chicken must not penalize earlier Tower exposure')
    new_game(); tower_hop()
    chicken(8)
    assert(not data['act1.RouteTest.tower_px_cap'],'a Den chicken must not lower the Tower cap')
    new_game(); configured=8
    chicken(20)
    assert(not data['act1.RouteTest.tower_px_cap'],'configured P8 alone must not invent Tower exposure in a fresh game')
    new_game(); ack=false
    assert(not pcall(tower_hop),'an unacknowledged Tower command must fail')
    ack=true; chicken(20)
    assert(not data['act1.RouteTest.tower_px_cap'],'an unacknowledged command must not create Tower exposure')
    new_game(); accepted=false
    assert(not pcall(tower_hop),'a rejected Tower command must fail')
    accepted=true; chicken(20)
    assert(not data['act1.RouteTest.tower_px_cap'],'a rejected command must not create Tower exposure')
    new_game(); supported=false; configured=8
    tower_hop(); supported=true; chicken(20)
    assert(not data['act1.RouteTest.tower_px_cap'],'unsupported player commands must not create Tower exposure')
    new_game(); me.mana=0
    tower_hop(); chicken(20)
    assert(not data['act1.RouteTest.tower_px_cap'],'a P1-only Tower chicken must not lower the cap')
    new_game(); me.mana=100; configured=8; before=writes
    tower_hop()
    assert(writes==before,'an already configured eligible P8 hop needs no duplicate command')
    states.collect.enter(machine())
    assert(configured==1,'recovery must still lower to P1 after high-count exposure')
    chicken(20)
    assert(data['act1.RouteTest.tower_px_cap']==3,'P8 Tower exposure must lower the next-game cap to P3 even after lowering to P1')
    new_game(); tower_hop()
    assert(configured==3,'the persisted Tower cap must determine the next eligible XP hop')
    me.area=24; trip.target=25; states.travel.tick(machine())
    assert(configured==1,'the Countess approach must remain P1 with a P3 XP cap')
    chicken(25)
    assert(data['act1.RouteTest.tower_px_cap']==1,'P3 Tower exposure must lower the next-game cap to P1')
    new_game(); tower_hop()
    assert(configured==1,'a P1 Tower cap must not escalate automatically')
    me.area=2; trip.target=8; den=false
    states.travel.tick(machine())
    assert(configured==8,'Tower backoff must not change the Den P8 policy')
    states.collect.enter(machine())
    assert(configured==1,'recovery remains P1 after a capped Tower attempt')
    data['act1.RouteTest.tower_px_cap']=old_cap
    game.ready=old_ready
    fighter.hostiles=function(_,_,radius) return close and radius==12 and {{id=91}} or {} end
    bot.tick,bot.data,bot.state=old_bot_tick,old_bot_data,old_bot_state
    me.life,me.mana=100,100
  end
  accepted=true; game.players=nil
  assert(not pcall(states.warm.enter,machine()),'an old DLL without player-count APIs must fail clearly')
  game.players=function() return {supported=true,configured=8,revision=revision} end
  act.players=nil
  assert(not pcall(states.warm.enter,machine()),'a missing native command API must fail clearly')
  game.players=old_players; act=old_act
  -- Exercise recovery entry ordering with an acknowledgement-capable API.
  game.players=function() return {supported=true,configured=configured,revision=revision} end
  act={players=function(count) configured=count; revision=revision+1; return true end,ready=function() return true end}
  me.area=21; me.in_town=false; me.level=6; den=true
  for _,name in ipairs({'portal_heal','retreat','take_waypoint','activate_waypoint','corpse','save_exit','farm','collect','dead'}) do
    configured=8
    local recovery=machine()
    recovery.data.px_from,recovery.data.px_to=6,20
    states[name].enter(recovery)
    assert(configured==1 and not recovery.data.px_from and not recovery.data.px_to,
      'recovery and non-XP movement entries must lower to P1 and invalidate the old hop decision')
  end
  me.area=6; trip.target=20; trip.hop=nil
  m=machine(); states.travel.tick(m)
  assert(configured==8,'a stocked Tower approach should select P8')
  states.collect.enter(m)
  assert(configured==1,'a recovery detour must first lower the setting')
  states.travel.tick(m)
  assert(configured==8,'a resumed hop must re-evaluate current readiness after its old decision was invalidated')
  -- Global dodge can cross a border before the FSM tick: its count must already be reconciled.
  configured=1; me.area=6; trip.target=20; trip.hop=nil; me.alive=true; me.can_act=true
  bot.state='travel'; bot.data={}; bot.tick=function() end
  fighter.dodge=function() assert(configured==8,'global movement must follow player-count acknowledgement'); return true end
  -- Clear the prior state's explorer so it cannot demand navigation mocks here.
  states.corpse.enter(machine()); configured=1
  tick()
  game.players,game.items,game.level_info,game.units,game.ui=old_players,old_items,old_info,old_units,old_ui
  act,service.portal_scrolls,trip.tick,trip.target,trip.hop=old_act,old_scrolls,old_trip,old_target,old_hop
  fighter.threat,fighter.hostiles,fighter.missile_lanes,fighter.dodge=old_threat,old_hostiles,old_lanes,old_dodge
  fighter.observe_missiles=old_observe
  bot.tick,bot.data,bot.state=old_bot_tick,old_bot_data,old_bot_state
  data['act1.RouteTest.tower_prep']=nil
end
do
  local old_units,old_ui,old_info,old_map,old_act=game.units,game.ui,game.object_info,map,act
  local old_to,old_tick,old_status=approach.to,approach.tick,approach.status
  local wp={waypoint=true,x=16,y=0}
  local walks,clicks,clear=0,0,true
  game.units=function(kind) return kind=='object' and {wp} or {} end
  game.ui=function() return false end
  game.object_info=function() return {waypoint=true} end
  map={line_of_sight=function() return clear end,
    presets=function() return {{type='object',class_id=119,x=wp.x,y=wp.y}} end}
  act={ready=function() return true end,interact=function(unit) assert(unit==wp); clicks=clicks+1 end}
  approach.to=function() walks=walks+1 end
  approach.tick=function() end
  approach.status='idle'
  me.area,me.x,me.y=1,0,0
  states.sync_waypoints.tick(machine())
  assert(walks==1 and clicks==0,'a waypoint sixteen subtiles away must be approached without queuing an interaction')
  wp.x=4; clear=false
  states.sync_waypoints.tick(machine())
  assert(walks==2 and clicks==0,'a nearby waypoint behind an obstacle must still be approached')
  clear=true
  states.sync_waypoints.tick(machine())
  assert(walks==2 and clicks==1,'a visible waypoint in interaction range can be clicked')
  game.units,game.ui,game.object_info,map,act=old_units,old_ui,old_info,old_map,old_act
  approach.to,approach.tick,approach.status=old_to,old_tick,old_status
end
do
  local old_data,old_quest,old_items,old_units,old_waypoints,old_info=data,game.quest,game.items,game.units,game.waypoints,game.level_info
  local routing,leveling=package.loaded['lib.travel'],package.loaded['lib.leveling']
  local old_route,old_hops,old_choose=routing.route,leveling.hops_from,leveling.choose
  local carried,reachable=0,true
  data={}
  game.quest=function(id) return {completed=id==1} end
  game.items=function()
    local result={}
    for i=1,carried do result[i]={code=i==2 and 'rvs' or 'mp1',location='belt'} end
    return result
  end
  game.units=function() return {} end
  game.waypoints=function() return {} end
  game.level_info=function(id) return {monster_level=({[2]=1,[3]=2,[4]=4})[id] or 30} end
  routing.route=function(from,to)
    if from==to then return {} end
    if to==37 then return {{to=2},{to=3},{to=4},{to=37}} end
    return {{to=to}}
  end
  leveling.hops_from=function() return reachable and {[2]=1,[3]=2,[4]=3} or {[2]=1} end
  me.class,me.difficulty,me.area,me.in_town,me.gold,me.stash_gold='sorceress',0,1,true,0,0
  no_farm=false
  for _,level in ipairs({3,4,5}) do
    me.level=level
    for count=0,1 do
      carried=count; data['act1.RouteTest.gold_farm']=nil
      assert(states.decide.tick(machine())=='farm' and data['act1.RouteTest.gold_farm'],
        'poor early sorceresses must still recover with fewer than two carried reserves')
    end
    carried=2
    assert(states.decide.tick(machine())=='travel' and not data['act1.RouteTest.gold_farm'],
      'two carried mana/rejuvenation reserves must end poor early recovery and resume forward travel')
    data['act1.RouteTest.gold_farm']=true
    me.gold=240
    local m=machine(); m.data.town_done=nil
    assert(states.decide.tick(m)=='town' and data['act1.RouteTest.gold_farm'],
      'six affordable doses must still require buying the carried reserve')
    me.gold=0
  end
  for _,change in ipairs({{class='barbarian',level=3,difficulty=0},
    {class='sorceress',level=6,difficulty=0},{class='sorceress',level=3,difficulty=1}}) do
    me.class,me.level,me.difficulty=change.class,change.level,change.difficulty
    data['act1.RouteTest.gold_farm']=true
    assert(states.decide.tick(machine())=='farm' and data['act1.RouteTest.gold_farm'],
      'other classes, later levels and other difficulties must retain the six-dose recovery target')
  end
  me.class,me.level,me.difficulty='sorceress',3,0
  carried=0; data['act1.RouteTest.gold_farm']=true
  local m=machine(); m.data.farm_commit={area=2,mode='gold'}
  states.farm.enter(m)
  assert(#candidates==1 and candidates[1]==3 and m.data.farm_area==3,
    'post-Den recovery must replace a retained Moor sweep with reachable unspent Cold Plains')
  leveling.choose=function(player,build,options)
    if #options.candidates==1 and options.candidates[1]==3 then return nil end
    return old_choose(player,build,options)
  end
  m=machine(); states.farm.enter(m)
  assert(m.data.farm_area==2,'Cold Plains rejected by the area evaluator must retain the Moor fallback')
  leveling.choose=old_choose
  m=machine(); m.data.spent={[3]=true}
  states.farm.enter(m)
  assert(m.data.farm_area==2,'spent Cold Plains must retain the Moor recovery fallback')
  reachable=false
  m=machine(); states.farm.enter(m)
  assert(m.data.farm_area==2,'unreachable Cold Plains must retain the Moor recovery fallback')
  reachable=true
  game.level_info=function(id) return {monster_level=id==3 and 4 or 1} end
  m=machine(); states.farm.enter(m)
  assert(m.data.farm_area==2,'dry recovery must not prefer Cold Plains beyond safe staff range')
  data,game.quest,game.items,game.units,game.waypoints,game.level_info=old_data,old_quest,old_items,old_units,old_waypoints,old_info
  routing.route,leveling.hops_from,leveling.choose=old_route,old_hops,old_choose
end
do
  local old_data,old_items,old_quest,old_units,old_waypoints=data,game.items,game.quest,game.units,game.waypoints
  local old_me={}; for k,v in pairs(me) do old_me[k]=v end
  local potions,completed={'mp1','hp1'},true
  data={}
  game.quest=function(id) return {completed=id==1 and completed} end
  game.units=function() return {} end
  game.waypoints=function() return {} end
  game.items=function()
    local result={}
    for _,code in ipairs(potions) do result[#result+1]={code=code,location='belt'} end
    return result
  end
  local function ready()
    me.class,me.difficulty,me.level,me.area,me.in_town='sorceress',0,4,5,false
    me.life,me.max_life,me.mana,me.max_mana,me.gold,me.stash_gold=85,100,75,100,7,0
    completed=true; potions={'mp1','hp1'}; data={}
  end
  for _,level in ipairs({3,4,5}) do
    for _,area in ipairs({3,4,10,5}) do
      ready(); me.level,me.area=level,area
      assert(states.decide.tick(machine())=='travel' and not data['act1.RouteTest.gold_farm'],
        'healthy early forward travel must not start a gold detour for one carried mana potion')
      data['act1.RouteTest.gold_farm']=true
      local resumed=machine(); resumed.data.farm_commit={area=3,mode='gold'}
      assert(states.decide.tick(resumed)=='travel' and not data['act1.RouteTest.gold_farm']
        and not resumed.data.farm_commit,
        'healthy early forward travel must release an interrupted gold request and its committed destination')
    end
  end
  for _,pair in ipairs({{'mp1','rvs'},{'hp1','rvs'},{'rvs','rvl'}}) do
    ready(); potions=pair; data['act1.RouteTest.gold_farm']=true
    assert(states.decide.tick(machine())=='travel' and not data['act1.RouteTest.gold_farm'],
      'two distinct carried consumables may cover mana and healing with rejuvenations')
  end
  local exclusions={
    function() me.life=84 end, function() me.mana=74 end,
    function() me.max_life=0 end, function() me.max_mana=0 end,
    function() me.level=2 end, function() me.level=6 end,
    function() me.class='barbarian' end, function() me.difficulty=1 end,
    function() me.in_town=true; me.area=1 end,
    function() completed=false; me.area=8 end,
    function() completed=false end, function() data['act1.RouteTest.tower_prep']=true end,
    function() potions={} end, function() potions={'hp1'} end,
    function() potions={'mp1'} end, function() potions={'rvs'} end,
    function() game.items=function() return {{code='mp1',location='stash'},
      {code='hp1',location='container',page=1}} end end,
  }
  for _,area in ipairs({1,2,6,8,9,20,21,25}) do
    local excluded=area
    exclusions[#exclusions+1]=function() me.area=excluded end
  end
  local carried_items=game.items
  for index,exclude in ipairs(exclusions) do
    ready(); game.items=carried_items; data['act1.RouteTest.gold_farm']=true; exclude()
    states.decide.tick(machine())
    assert(data['act1.RouteTest.gold_farm'],'outside early forward readiness the existing recovery request must remain: '..index)
  end
  ready(); game.items=function() return {{code='mp1',location='container',page=0},{code='hp1',location='belt'}} end
  data['act1.RouteTest.gold_farm']=true
  local m=machine(); m.data.farm_area=3; m.data.farm_commit={area=3,mode='gold'}
  assert(states.farm.tick(m)=='decide' and not m.data.farm_commit and not data['act1.RouteTest.gold_farm'],
    'resources becoming ready during a gold farm must release its commitment immediately')
  assert(states.decide.tick(m)=='travel','released recovery must resume the ordinary forward decision')

  local old_ready,old_portals,old_dry,old_threat,old_dodge=game.ready,service.portal_scrolls,fighter.out_of_mana,fighter.threat,fighter.dodge
  local old_state,old_bot_data=bot.state,bot.data
  game.ready=function() return true end
  service.portal_scrolls=function() return 1 end
  fighter.out_of_mana=function() return true end
  fighter.threat=function() return {id=99,x=1,y=0} end
  fighter.dodge=function() return false end
  me.alive,me.can_act=true,true
  bot.state,bot.data='farm',machine().data; bot.data.farm_area=3
  me.area=3 -- farming where we stand, not on the way there
  tick()
  assert(bot.state=='portal_heal','actual dry combat must still recover even when macro forward readiness passes')
  game.ready,service.portal_scrolls,fighter.out_of_mana,fighter.threat,fighter.dodge=old_ready,old_portals,old_dry,old_threat,old_dodge
  bot.state,bot.data=old_state,old_bot_data
  for k in pairs(me) do me[k]=nil end; for k,v in pairs(old_me) do me[k]=v end
  data,game.items,game.quest,game.units,game.waypoints=old_data,old_items,old_quest,old_units,old_waypoints
end
do
  local old_now,old_ui,old_units=charon.now,game.ui,game.units
  local old_map,old_object_info=map,game.object_info
  local old_threat,old_hostiles=fighter.threat,fighter.hostiles
  local t,walks=31000,0
  charon.now=function() return t end
  game.ui=function() return false end
  game.units=function() return {} end
  game.object_info=function() return {waypoint=true} end
  map={presets=function() return {{type='object',class_id=119,x=100,y=100}} end}
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  me.in_town=false
  for _,area in ipairs({6,27,35}) do
    me.area=area
    local m=machine()
    t=0; states.activate_waypoint.enter(m); t=31000
    m.data.wp_mover={status='moving',tick=function() walks=walks+1 end}
    local before=walks
    assert(states.activate_waypoint.tick(m)==nil and walks==before+1 and not m.data.no_wp,
      'mandatory waypoint anchors must keep working after thirty-one seconds')
    me.area=area+1
    assert(states.activate_waypoint.tick(m)==nil and not m.data.no_wp,
      'a brief neighboring-area crossing must not shorten an anchor attempt to thirty seconds')
    me.area=area
    t=90001
    fighter.threat=function() error('expired waypoint attempts must not let combat renew their budget') end
    assert(states.activate_waypoint.tick(m)=='travel' and m.data.no_wp==area,
      'mandatory waypoint attempts must still stop after ninety seconds')
    fighter.threat=function() return nil end
    t=31000
  end
  me.area=29
  local m=machine(); m.data.wp_since=0; m.data.wp_spawn={x=100,y=100}
  assert(states.activate_waypoint.tick(m)=='travel' and m.data.no_wp==29,
    'optional waypoints must retain their thirty-second bound')
  charon.now,game.ui,game.units=old_now,old_ui,old_units
  map,game.object_info=old_map,old_object_info
  fighter.threat,fighter.hostiles=old_threat,old_hostiles
end
do
  local old_units,old_items,old_quest,old_map=game.units,game.items,game.quest,map
  local old_threat,old_hostiles,old_fight=fighter.threat,fighter.hostiles,fighter.fight
  local saved_approach={}
  for key,value in pairs(approach) do saved_approach[key]=value end
  local calls,to_status,tick_status,plan_status
  game.units=function() return {} end
  game.items=function() return {{code='hp1',location='belt',grid_x=0}} end
  game.quest=function() return {completed=false,reward_pending=false} end
  map={invalidate=function() calls.invalidate=calls.invalidate+1 end,
    presets=function() return {{type='monster',class_id=156,x=80,y=90}} end}
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  me.area,me.in_town,me.x,me.y,me.life,me.max_life=37,false,0,0,100,100
  me.gold,me.stash_gold=0,0
  local function fixture()
    calls={to=0,tick=0,plan=0,invalidate=0,fights=0}
    to_status,tick_status,plan_status='moving','moving','moving'
    approach.x,approach.y,approach.level,approach.status=80,90,nil,'moving'
    approach.replans,approach.tried_doors=3,{[7]=true}
    approach.to=function(self,x,y)
      calls.to=calls.to+1; self.x,self.y,self.level,self.status=x,y,nil,to_status
      self.replans,self.tried_doors=0,nil
      return self.status
    end
    approach.tick=function(self) calls.tick=calls.tick+1; self.status=tick_status; return self.status end
    approach.plan=function(self) calls.plan=calls.plan+1; self.status=plan_status; return self.status end
    local m=machine(); m.data.lair={x=80,y=90}
    return m
  end
  local m=fixture()
  approach.x=nil; to_status='failed'
  assert(states.andariel.tick(m)==nil and calls.to==1 and calls.plan==1 and calls.invalidate==1
    and approach.status=='moving' and m.data.lair_refreshed,
    'an initially failed lair route must get exactly one same-goal cache refresh')
  assert(states.andariel.tick(m)==nil and calls.to==1 and calls.plan==1 and calls.tick==1,
    'a recovered current-goal route must continue with ordinary ticks')
  -- A legitimate combat interruption must not consume another planning allowance.
  fighter.threat=function() return {id=1} end
  fighter.fight=function() calls.fights=calls.fights+1 end
  assert(states.andariel.tick(m)==nil and calls.fights==1 and calls.tick==1 and m.data.lair_refreshed,
    'combat must keep priority without renewing or consuming the lair refresh bound')
  fighter.threat=function() return nil end
  assert(states.andariel.tick(m)==nil and calls.tick==2 and calls.invalidate==1,
    'the same lair route must resume after combat')
  -- Portal work can replace the shared mover's goal, but not the per-game allowance.
  approach.x,approach.y=10,20; to_status='failed'
  assert(states.andariel.tick(m)=='save_exit' and calls.to==2 and calls.invalidate==1 and calls.plan==1,
    'returning from portal work must replace a stale goal without resetting the used refresh bound')
  m=fixture(); approach.x=10; approach.y=20
  assert(states.andariel.tick(m)==nil and calls.to==1 and calls.tick==0 and calls.invalidate==0,
    'a stale waypoint or town route must be replaced before its old tick can issue movement')
  m=fixture()
  assert(states.andariel.tick(m)==nil and calls.tick==1 and calls.to==0 and calls.plan==0,
    'a valid current lair route must not be restarted on each tick')
  m=fixture(); tick_status='failed'
  local tried=approach.tried_doors
  assert(states.andariel.tick(m)==nil and calls.to==0 and calls.plan==1 and calls.invalidate==1
    and approach.replans==3 and approach.tried_doors==tried,
    'a failed current-goal refresh must retain mover retry and tried-door ownership')
  m=fixture(); approach.status='idle'
  assert(states.andariel.tick(m)==nil and calls.plan==1 and calls.to==0 and calls.invalidate==0,
    'a stopped same-goal mover must resume planning without resetting its retries')
  m=fixture(); tick_status,plan_status='failed','failed'
  assert(states.andariel.tick(m)=='save_exit' and calls.invalidate==1 and calls.plan==1,
    'a persistent lair failure must exit after its single cache refresh')
  assert(states.andariel.tick(m)=='save_exit' and calls.invalidate==1 and calls.plan==1 and calls.to==0,
    'repeated failed ticks must not restart budgets or repeat cache invalidation')
  m=fixture(); tick_status,plan_status='arrived','arrived'
  assert(states.andariel.tick(m)=='save_exit' and calls.invalidate==1 and calls.plan==1,
    'arrival without a visible boss or quest credit must terminate finitely instead of replanning forever')

  m=fixture(); m.data.lair=nil
  assert(states.andariel.tick(m)==nil and m.data.andariel_goal.source=='spawn'
    and m.data.andariel_goal.x==80 and m.data.andariel_goal.y==90,
    'a boss never observed alive must retain the original spawn fallback')
  local boss={id=112,class_id=156,alive=true,x=15.9,y=20.1}
  game.units=function() return {boss} end
  map.line_of_sight=function() return false end
  m=fixture(); m.data.lair_refreshed=true
  approach.status='arrived'
  assert(states.andariel.tick(m)==nil and calls.to==1 and approach.x==15 and approach.y==20
    and m.data.andariel_goal.source=='live' and m.data.andariel_last_seen.id==112,
    'an obscured live boss must replace a stale arrived spawn goal with her current position')
  approach.replans,approach.tried_doors=3,{[7]=true}
  local tried=approach.tried_doors
  boss.x,boss.y=16.01,20.01
  assert(states.andariel.tick(m)==nil and calls.to==1 and calls.tick==1
    and approach.replans==3 and approach.tried_doors==tried and m.data.lair_refreshed,
    'subcell movement across an integer boundary must not reset current-goal retries or map refresh ownership')
  boss.x,boss.y=17.4,20.9
  assert(states.andariel.tick(m)==nil and calls.to==1 and approach.replans==3,
    'movement must be measured from the original observation, not the rounded route cell')
  game.units=function() return {} end
  assert(states.andariel.tick(m)==nil and calls.to==1 and approach.replans==3
    and approach.tried_doors==tried and m.data.andariel_goal.source=='last seen',
    'unloading after ignored movement must not re-quantize the remembered goal or renew retries')
  game.units=function() return {boss} end
  assert(states.andariel.tick(m)==nil and calls.to==1 and approach.replans==3
    and m.data.andariel_goal.source=='live',
    'reloading the same boss must preserve the existing route and retries')
  boss.x=19
  assert(states.andariel.tick(m)==nil and calls.to==2 and approach.x==19 and calls.invalidate==0,
    'meaningful boss movement must update the live approach without renewing the map refresh')

  local old_scrolls=service.portal_scrolls
  service.portal_scrolls=function() return 1 end
  game.items=function() return {} end
  me.gold=180
  boss.x,boss.y=25.5,20.5
  m=fixture(); m.data.lair_refreshed=true
  assert(states.andariel.tick(m)=='portal_heal' and m.data.andariel_last_seen.x==25.5
    and m.data.andariel_last_seen.y==20.5 and calls.to==0,
    'live boss coordinates must be remembered before resupply can leave the area')
  me.area,me.in_town=1,true
  assert(states.andariel.tick(m)=='decide' and m.data.lair_refreshed,
    'town must preserve the existing refresh allowance and boss observations')
  me.area,me.in_town,me.gold=37,false,0
  game.units=function() return {} end
  game.items=function() return {{code='hp1',location='belt',grid_x=0}} end
  boss.x=100 -- saved observations must not retain a mutable unit table
  approach.x,approach.y=10,20
  assert(states.andariel.tick(m)==nil and calls.to==1 and approach.x==25 and approach.y==20
    and m.data.andariel_goal.source=='last seen' and m.data.lair_refreshed,
    'an unloaded boss after portal recovery must be approached at her last live position, not her spawn')
  local old_print,diagnostics=print,0
  print=function(line)
    if tostring(line):find('andariel: terminal approach',1,true) then
      diagnostics=diagnostics+1
      assert(line:find('boss=nil',1,true) and line:find('source=spawn',1,true)
        and line:find('endpoint=(24.00,20.00)',1,true)
        and line:find('refresh_used=true',1,true),'terminal diagnostics must identify the selected and actual route endpoints')
    end
  end
  approach.x,approach.y=10,20
  approach.path={{x=24,y=20}}
  to_status,tick_status='arrived','arrived'
  assert(states.andariel.tick(m)==nil and not m.data.andariel_last_seen and not m.data.andariel_goal,
    'a boss gone from where she was last seen is looked for in her hall before the game is given up')
  assert(states.andariel.tick(m)=='save_exit' and calls.invalidate==0 and calls.plan==0,
    'another town interruption must not grant a second refresh after a remembered-goal arrival')
  assert(states.andariel.tick(m)=='save_exit' and diagnostics==1 and calls.invalidate==0,
    'terminal diagnostics and retry allowances must stay bounded across repeated terminal ticks')
  print=old_print
  service.portal_scrolls=old_scrolls
  game.units,game.items,game.quest,map=old_units,old_items,old_quest,old_map
  fighter.threat,fighter.hostiles,fighter.fight=old_threat,old_hostiles,old_fight
  for key in pairs(approach) do approach[key]=nil end
  for key,value in pairs(saved_approach) do approach[key]=value end
end
do
  -- Level gate: level up nearby instead of walking into an area whose monsters
  -- out-level the build by more than its gap.
  gated_build.enter_gap = 1
  local level_info, route, items = game.level_info, package.loaded['lib.travel'].route, game.items
  game.items=function()
    local carried={}
    for i=1,6 do carried[i]={code='mp1',location='belt'} end
    carried[7]={code='hp1',location='container',page=0}
    return carried
  end
  game.level_info=function(id) return {monster_level=({[33]=11,[34]=11,[37]=12})[id] or 5,name='area'..id} end
  package.loaded['lib.travel'].route=function(from,to)
    if from==to then return {} end
    if to==37 then return {{to=34},{to=37}} end
    return {{to=to}}
  end
  den=true; me.class='sorceress'; me.difficulty=0; me.in_town=false; me.area=33; me.level=9
  me.gold=1000; me.life=me.max_life
  data['act1.RouteTest.gold_farm']=nil; data['act1.RouteTest.tower_prep_done']=true
  data['act1.RouteTest.tower_prep']=nil; data['act1.RouteTest.corpse_area']=nil
  local m=machine()
  local state,why=states.decide.tick(m)
  assert(state=='farm' and tostring(why):find('low for',1,true),
    'two levels below the next area the build must level up first, got '..tostring(state)..' '..tostring(why))
  no_farm=nil
  states.farm.enter(m)
  me.area=m.data.farm_area or me.area
  assert(states.farm.tick(m)~='decide','leveling must continue below the gate even where the next hop from the farm is easy')
  do -- a stop to level is seen through from wherever the way to its area leads
    local kept_area,kept_town,kept_info=me.area,me.in_town,game.level_info
    game.level_info=function(id) return {monster_level=5,name='area'..id} end -- from here nothing ahead is gated
    me.area,me.in_town=3,false
    local state,reason=states.decide.tick(m)
    assert(state=='farm' and tostring(reason):find('levelling in',1,true),
      'arriving by waypoint on the way to the levelling area must not send her back to the gate, got '..tostring(state))
    me.level=10
    assert(states.decide.tick(m)~='farm','once the level is reached the plan goes on')
    me.level=9; me.area,me.in_town,game.level_info=kept_area,kept_town,kept_info
  end
  me.level=10; chosen=nil
  assert(states.farm.tick(m)=='decide','reaching the gated level ends the leveling')
  me.area=33
  assert(states.decide.tick(machine())=='travel' and chosen==34,'one level below, the build walks on')
  do -- where to level is the optimizer's choice among every area of the act
    local lib=package.loaded['lib.leveling']
    local old,offered=lib.choose,nil
    lib.choose=function(player,build,options) offered=options; return 21,'the Tower' end
    local function listed()
      local set={}
      for _,level in ipairs(offered.candidates) do set[level]=true end
      return set
    end
    me.level=9
    local stop=machine()
    states.decide.tick(stop); states.farm.enter(stop)
    local set=listed()
    assert(stop.data.farm_area==21 and set[21] and set[18] and set[33] and set[2] and not set[1] and not set[8] and not set[38],
      'a stop to level weighs every area of the act, the Tower and the crypts with the rest')
    assert(offered.floor==nil,'and none is left out for lying below her')
    data['act1.RouteTest.farm_left']='21:9'; states.farm.enter(stop)
    set=listed()
    assert(not set[21] and set[22],'one that ended in a chicken at this level is not offered again')
    me.level=8; states.decide.tick(stop); states.farm.enter(stop)
    assert(listed()[21],'at another level it is')
    data['act1.RouteTest.farm_left']=nil
    data['act1.RouteTest.gold_farm']=true; me.level=9; states.farm.enter(stop)
    assert(not listed()[21],'a sweep for gold keeps to the main route')
    data['act1.RouteTest.gold_farm']=nil
    lib.choose=old
  end
  gated_build.enter_gap=nil; me.level=9; chosen=nil
  assert(states.decide.tick(machine())=='travel' and chosen==34,'a build without a gap is never gated')
  do -- the gate is asked from the waypoint nearest the goal, wherever she stands
    local kept_route=package.loaded['lib.travel'].route
    package.loaded['lib.travel'].route=function(from,to)
      if from==to then return {} end
      local order,path={1,6,7,35,36,37},{}
      local started=false
      for _,level in ipairs(order) do
        if started then path[#path+1]={to=level} end
        if level==from then started=true end
        if level==to then break end
      end
      return path
    end
    game.level_info=function(id) return {monster_level=({[36]=12,[37]=12})[id] or 5,name='area'..id} end
    gated_build.enter_gap=1
    data['act1.RouteTest.wp.35']=true
    me.area=6; me.level=10; me.in_town=false; chosen=nil
    local state,why=states.decide.tick(machine())
    assert(state=='farm' and tostring(why):find('area36',1,true),
      'sent back to level up, she must not jump ahead to the gated waypoint again, got '..tostring(state)..' '..tostring(why))
    me.level=11
    state=states.decide.tick(machine())
    assert(state~='farm','one level on, the plan goes on')
    data['act1.RouteTest.wp.35']=nil; gated_build.enter_gap=nil
    package.loaded['lib.travel'].route=kept_route
    game.level_info=function(id) return {monster_level=({[33]=11,[34]=11,[37]=12})[id] or 5,name='area'..id} end
  end
  -- A build that does not prepare in the Tower never begins to.
  gated_build.tower_prep=false
  data['act1.RouteTest.tower_prep_done']=nil; me.area=6; me.level=6; chosen=nil
  assert(states.decide.tick(machine())=='travel' and not data['act1.RouteTest.tower_prep']
    and not data['act1.RouteTest.tower_prep_done'],'without Tower preparation the plan is the main route')
  -- One descent only: after the Countess has been killed, preparation is over.
  gated_build.tower_prep='once'
  data['act1.RouteTest.tower_cleared']=true; chosen=nil
  assert(states.decide.tick(machine())=='travel' and data['act1.RouteTest.tower_prep_done']
    and not data['act1.RouteTest.tower_prep'],'a build that prepares once does not go back to the Tower')
  data['act1.RouteTest.tower_cleared']=nil
  gated_build.tower_prep=true; data['act1.RouteTest.tower_prep_done']=true
  game.level_info,package.loaded['lib.travel'].route,game.items=level_info,route,items
end
do -- every way in to Andariel is a new approach
  local old={class=me.class,level=me.level,area=me.area,in_town=me.in_town,gold=me.gold,stash_gold=me.stash_gold}
  me.class,me.level,me.area,me.in_town,me.gold,me.stash_gold='sorceress',9,1,true,5000,0
  assert(states.decide.tick(machine())~='hire','the sorceress does not hire, however much gold she has')
  local lured=machine(); lured.data.lure,lured.data.lured,lured.data.andariel_trail={x=1,y=1},true,{{x=0,y=0}}
  states.andariel.enter(lured)
  assert(not lured.data.lure and not lured.data.lured and not lured.data.andariel_trail,
    'every way in to Andariel is a new approach with its own walk back')
  for key,value in pairs(old) do me[key]=value end
end
do -- the opening: a portal by the Den, the Cold Plains waypoint and level 2, then back through the portal
  local old={class=me.class,level=me.level,area=me.area,in_town=me.in_town,x=me.x,y=me.y}
  local kept={items=game.items,units=game.units,waypoints=game.waypoints,quest=game.quest,object_info=game.object_info,
    route=package.loaded['lib.travel'].route,map=map,ready=act.ready,use_item=act.use_item,interact=act.interact,
    threat=fighter.threat,close=town.close,to=approach.to,tick=approach.tick,stop=approach.stop,status=approach.status}
  assert(gated_build.den_marker,'the sorceress opens with the portal by the Den')
  local scroll={code='tsc',location='container',page=0,id=5}
  local carried,objects,known={scroll},{},{}
  local used,clicked
  game.items=function() return carried end
  game.units=function(kind) return kind=='object' and objects or {} end
  game.waypoints=function() return known end
  game.object_info=function() return {waypoint=true} end
  map={presets=function() return {{type='object',class_id=119,x=7,y=7}} end,invalidate=function() end,
    line_of_sight=function() return true end}
  package.loaded['lib.travel'].route=function(from,to)
    if from==to then return {} end
    if from==2 and to==8 then return {{to=8,x=50,y=0}} end
    if to==37 then return {{to=3},{to=37}} end
    return {{to=to}}
  end
  act.ready=function() return true end
  act.use_item=function(item) used=item; return true end
  act.interact=function(unit) clicked=unit end
  town.close=function() end
  fighter.threat=function() return nil end
  approach.status='idle'
  approach.to=function(self,x,y) self.target={x=x,y=y}; self.status='moving' end
  approach.tick=function() end
  approach.stop=function(self) self.status='idle' end
  for name in pairs(data) do if name:find('act1.RouteTest.',1,true) then data[name]=nil end end
  den=false; me.class,me.level,me.area,me.in_town,me.x,me.y='sorceress',1,1,true,0,0

  local m=machine(); chosen=nil
  local state=states.decide.tick(m)
  assert(state=='travel' and chosen==2,'a new character with her scroll heads into the Moor first, got '..tostring(state))
  me.in_town,me.area=false,2
  assert(states.decide.tick(m)=='den_marker','in the Moor the portal by the Den comes first')
  states.den_marker.enter(m)
  assert(states.den_marker.tick(m)==nil and approach.target.x==50 and not used,'she walks to the Den entrance before reading the scroll')
  do -- on the way out, what would take many casts is left unless it is upon her
    local values=package.loaded['lib.value']
    local kept_life,kept_neural,asked=values.life,fighter.neural
    values.life=function(unit) return unit.tough and 45 or 11 end
    fighter.neural={version='fixture',cast_power=function() return 9 end}
    fighter.threat=function(_,_,_,filter) asked=filter; return nil end
    states.den_marker.tick(m)
    assert(asked and not asked{id=1,x=20,y=0,tough=true},'a zombie at eight players is not gone after')
    assert(asked{id=2,x=3,y=0,tough=true},'unless it is upon her')
    values.life,fighter.neural=kept_life,kept_neural
    fighter.threat=function() return nil end
  end
  me.x=45
  assert(states.den_marker.tick(m)==nil and used==scroll,'at the entrance the scroll is read')
  used=nil
  assert(states.den_marker.tick(m)==nil and not used,'one reading is given time to open the portal')
  carried={}
  objects={{id=9,portal=true,owner_name='RouteTest',class_id=59,x=46,y=0,destination=1}}
  assert(states.den_marker.tick(m)=='decide' and m.data.den_marker and m.data.den_marker.x==46,'the open portal is the marker')

  do -- with the portal standing, the Blood Moor first, until she can pay for her potions
    local kept_gold,kept_stash,kept_items=me.gold,me.stash_gold,game.items
    local stub=package.loaded['lib.loot']
    local kept_value,kept_now=stub.sell_value,charon.now
    local t=5e11 -- under the clocks of the cases that follow
    charon.now=function() return t end
    stub.sell_value=function() return 200 end
    game.items=function() return {} end
    me.gold,me.stash_gold=10,0
    chosen=nil
    assert(states.decide.tick(m)=='travel' and chosen==3,'as the build stands she goes straight on to the Cold Plains')
    gated_build.moor_first=true
    assert(states.decide.tick(m)=='opening_moor','with the Moor first and nothing to her name she searches it')
    exploration_stub={tick=function() return 'moving' end}
    states.opening_moor.enter(m)
    assert(states.opening_moor.tick(m)==nil and not m.data.moor_done,'and stays while she is poor')
    me.in_town,me.area=true,1; chosen=nil
    assert(states.decide.tick(m)=='travel' and chosen==2,'home to restock in the middle of it, she goes back out')
    me.in_town,me.area=false,2
    game.items=function() return {{code='jav',location='container',page=0},{code='jav',location='container',page=0}} end
    t=t+3000
    assert(states.opening_moor.tick(m)=='decide' and m.data.moor_done,'what she carries counts: with six doses within her means she goes on')
    m.data.moor_done=nil; game.items=function() return {} end; t=t+3000
    exploration_stub={tick=function() return 'done' end}
    states.opening_moor.enter(m)
    assert(states.opening_moor.tick(m)=='decide' and m.data.moor_done,'and with the Moor searched she goes on as she is')
    exploration_stub=nil
    me.gold,me.stash_gold,game.items,stub.sell_value,charon.now=kept_gold,kept_stash,kept_items,kept_value,kept_now
    gated_build.moor_first=false
  end
  chosen=nil
  assert(states.decide.tick(m)=='travel' and chosen==3,'with the portal standing she goes on to the Cold Plains')
  local sight
  fighter.threat=function(_,_,range) sight=range end
  states.travel.tick(m)
  assert(sight==24,'before her first spell the way is cleared, not run, got '..tostring(sight))
  me.area=3
  assert(states.decide.tick(m)=='activate_waypoint' and m.data.wp_resume=='decide' and m.data.wp_spawn.x==7,
    'in the Cold Plains the waypoint is next')
  states.activate_waypoint.enter(m)
  assert(m.data.wp_timeout==150000 and not m.data.wp_budget,'the walk across the Cold Plains has its own time')
  known={[3]=true}
  assert(states.decide.tick(m)=='take_waypoint' and m.data.waypoint_to==1,
    'with the waypoint she goes to town for the portal, whatever her level')
  me.level=2
  sight=nil; states.travel.tick(m)
  assert(sight==12,'with the spell she travels as usual, got '..tostring(sight))
  me.in_town,me.area,me.x,me.y=true,1,0,0
  assert(states.decide.tick(m)=='den_return','in town the portal to the Den is next')
  states.den_return.enter(m)
  objects={{id=10,portal=true,owner_name='RouteTest',class_id=59,x=20,y=0,destination=2}}
  assert(states.den_return.tick(m)==nil and approach.target.x==20 and not clicked,'she walks to her portal')
  me.x=19
  assert(states.den_return.tick(m)==nil and clicked==objects[1],'and steps through')
  me.in_town,me.area=false,2
  assert(states.den_return.tick(m)=='decide' and not m.data.den_marker and m.data.opening_off,'through the portal the opening is over')
  chosen=nil
  assert(states.decide.tick(m)=='travel' and chosen==8,'and the Den is next')

  me.area=8; den=true
  assert(states.clear.tick(m)=='save_exit','with the Cold Plains waypoint the cleared Den ends the game: no walk home')
  do -- and so it does when its last monster fell as she turned for home
    local home=machine(); home.data.retreat_started=charon.now()
    assert(states.retreat.tick(home)=='save_exit','a walk home out of the cleared Den is a new game instead')
  end
  known={}
  service.portal_scrolls=function() return 0 end
  chosen=nil
  assert(states.clear.tick(m)=='travel' and chosen==1,'without it she walks to Akara as before')
  known={[3]=true}

  -- The new game starts beside Akara, who has the reward.
  me.in_town,me.area=true,1
  game.quest=function(id) return {completed=false,reward_pending=id==1} end
  local fresh=machine()
  assert(states.decide.tick(fresh)=='den_reward','a pending Den reward is collected first')
  local talks=reward_talks
  assert(states.den_reward.tick(fresh)=='decide' and reward_talks==talks+1 and fresh.data.den_reward_tried
    and not fresh.data.quests_synced,'Akara is spoken to once and the quest record read again')
  fresh.data.quests_synced=true
  assert(states.decide.tick(fresh)~='den_reward','and not asked twice')
  game.quest=kept.quest

  -- Without the scroll, or with the waypoint already known, the Den is simply next.
  den=false; me.in_town,me.area,me.level=false,2,1
  carried={}; known={}; chosen=nil
  assert(states.decide.tick(machine())=='travel' and chosen==8,'no scroll: straight to the Den')
  carried={scroll}; known={[3]=true}; chosen=nil
  assert(states.decide.tick(machine())=='travel' and chosen==8,'waypoint known and no portal standing: straight to the Den')
  -- A portal that will not open ends the plan, not the game.
  known={}; objects={}
  local stuck=machine()
  local t,kept_now=10000,charon.now
  charon.now=function() return t end
  states.den_marker.enter(stuck); me.x=45
  local result
  for _=1,8 do t=t+4100; result=states.den_marker.tick(stuck); if result then break end end
  assert(result=='decide' and stuck.data.opening_off and not stuck.data.den_marker,'three readings without a portal drop the opening')
  charon.now=kept_now

  game.items,game.units,game.waypoints,game.quest,game.object_info=kept.items,kept.units,kept.waypoints,kept.quest,kept.object_info
  package.loaded['lib.travel'].route,map=kept.route,kept.map
  act.ready,act.use_item,act.interact=kept.ready,kept.use_item,kept.interact
  fighter.threat,town.close=kept.threat,kept.close
  approach.to,approach.tick,approach.stop,approach.status=kept.to,kept.tick,kept.stop,kept.status
  for name,value in pairs(old) do me[name]=value end
  den=true
end
do -- a pack that is not worth the fight is gone around, and fought from range only when there is no way round
  local old={area=me.area,in_town=me.in_town,level=me.level,x=me.x,y=me.y}
  local kept={hostiles=fighter.hostiles,lethality=fighter.pack_lethality,unit=game.unit,threat=fighter.threat,
    replan=trip.replan,through=trip.mover.through,detour=trip.mover.detour}
  assert(travel_options and type(travel_options.avoid)=='function','travel is given the ground to keep off')
  local elite={id=501,x=20,y=0,unique=true,ranged=true,name='Soul Flame',alive=true}
  local seen={elite}
  local accept
  fighter.hostiles=function() return seen end
  fighter.pack_lethality=function() return 0.5 end
  fighter.threat=function(_,_,_,filter) accept=filter end
  game.unit=function(kind,id) return kind=='monster' and id==501 and elite or nil end
  local replans=0
  trip.replan=function() replans=replans+1 end
  trip.mover.through,trip.mover.detour=nil,true
  den=true; me.area,me.in_town,me.level,me.x,me.y=10,false,4,0,0
  local t,kept_now=1e12,charon.now -- later than any clock an earlier case left behind
  charon.now=function() return t end
  local m=machine()
  states.travel.tick(m)
  local zones=travel_options.avoid()
  assert(replans==1 and #zones==1 and zones[1].x==20 and zones[1].radius==18,
    'an elite whose pack would take a third of her life is planned around, a shooter from farther')
  assert(accept and not accept(elite),'and is not sought out while a way round exists')
  trip.mover.through,trip.mover.detour=true,nil
  states.travel.tick(m)
  assert(accept(elite)==true,'with no way round it is fought from range, not walked into')
  elite.alive=false
  assert(#travel_options.avoid()==0,'a dead pack is forgotten')
  elite.alive=true; elite.id=502; elite.ranged=false
  fighter.pack_lethality=function() return 0.1 end
  game.unit=function() return nil end
  t=t+500; states.travel.tick(m)
  assert(replans==1 and #travel_options.avoid()==0,'an elite she can stand beside is no reason for a detour')
  fighter.pack_lethality=function() return 0.5 end
  t=t+500; states.travel.tick(m)
  assert(replans==1 and travel_options.avoid()[1].radius==12,'one that must close in is given less room')
  t=t+3000; states.travel.tick(m)
  assert(replans==2,'and the route is planned afresh for it, though not twice within moments')
  me.area=11
  assert(#travel_options.avoid()==0,'what was kept off in another level is forgotten')
  charon.now=kept_now
  fighter.hostiles,fighter.pack_lethality,game.unit,fighter.threat=kept.hostiles,kept.lethality,kept.unit,kept.threat
  trip.replan,trip.mover.through,trip.mover.detour=kept.replan,kept.through,kept.detour
  for name,value in pairs(old) do me[name]=value end
end
do -- what does not pay is not fought, unless it is upon her or must die for a quest
  local old={area=me.area,in_town=me.in_town,level=me.level,x=me.x,y=me.y}
  local kept={info=game.monster_info,level_info=game.level_info,threat=fighter.threat,hostiles=fighter.hostiles,
    lethality=fighter.pack_lethality,through=trip.mover.through,replan=trip.replan,unit=game.unit}
  -- Two kinds with the same life and experience; one shrugs off four fifths of her lightning.
  game.monster_info=function(id)
    if id~=9001 and id~=9002 then return nil end
    return {min_life=20,max_life=20,experience=40,level=14,
      resist={physical=0,magic=0,fire=0,cold=0,poison=0,lightning=id==9002 and 80 or 0}}
  end
  game.level_info=function(id)
    return {monster_level=(id==2 or id==10) and 14 or 60,monsters=(id==2 or id==10) and {9001} or {},density=1,name='area'..id}
  end
  game.unit=function() return nil end
  local plain={id=701,class_id=9001,x=20,y=0,name='Plain',alive=true}
  local shrugging={id=702,class_id=9002,x=20,y=0,name='Shrugging',alive=true}
  local elite={id=703,class_id=9001,x=20,y=0,unique=true,enchants={17},name='Charged',alive=true} -- Lightning Enchanted
  local accept='unset'
  fighter.threat=function(_,_,_,filter) accept=filter end
  fighter.hostiles=function() return {} end
  fighter.pack_lethality=function() return 0 end
  trip.replan=function() end
  trip.mover.through=nil
  den=false; me.area,me.in_town,me.level,me.x,me.y=2,false,14,0,0
  local t,kept_now=2e12,charon.now
  charon.now=function() return t end
  -- Levelling (here the opening's walk to the Den): the filter is applied.
  local m=machine(); m.data.marker_spot={x=100,y=0}; m.data.marker_mover=approach; m.data.marker_casts,m.data.marker_fails=0,0
  states.den_marker.tick(m)
  assert(type(accept)=='function','levelling chooses whom to fight')
  assert(accept(plain) and not accept(shrugging) and not accept(elite),
    'what pays a quarter of the usual is left alone: a resistant kind, and a unique enchanted against her element')
  shrugging.x=4
  assert(accept(shrugging),'unless it is upon her')
  shrugging.x=20
  -- On the way: an elite that does not pay is not sought out, and is planned around.
  den=true; me.area=10
  fighter.hostiles=function() return {elite} end
  local travelling=machine()
  states.travel.tick(travelling)
  assert(not accept(elite),'on the way it is not sought out either')
  assert(#travel_options.avoid()==1,'and its ground is kept off')
  -- Nothing that stands where a shooter reaches is fought by choice either.
  den=true; me.area=2
  local shooter={id=704,class_id=9001,x=20,y=0,ranged=true,name='Archer',alive=true}
  fighter.hostiles=function() return {shooter} end
  t=t+500; states.travel.tick(machine())
  fighter.hostiles=function() return {} end
  den=false
  local beside=machine(); beside.data.marker_spot={x=100,y=0}; beside.data.marker_mover=approach
  beside.data.marker_casts,beside.data.marker_fails=0,0
  states.den_marker.tick(beside)
  assert(not accept(shooter),'what shoots is not fought by choice')
  assert(not accept({id=705,class_id=9001,x=30,y=0,name='Plain',alive=true}),
    'nor what stands within its reach, whatever it would pay')
  assert(accept({id=706,class_id=9001,x=60,y=0,name='Plain',alive=true}),'out of its reach the same monster is fought')
  assert(accept({id=707,class_id=9001,x=3,y=0,ranged=true,name='Archer',alive=true}),'and a shooter within a tile of her is')
  assert(#travel_options.avoid()==0,'and the ground a shooter reaches is not planned around: she may walk through it')
  do -- within a shooter's reach she does not stand and fight: levelling, she steps out of it first
    local kept_move,kept_can,kept_fight,kept_threat=act.move,act.ready,fighter.fight,fighter.threat
    local moved,fought
    act.ready=function() return true end
    act.move=function(x,y) moved={x=x,y=y}; return true end
    fighter.fight=function() fought=true; return 'fighting' end
    me.x=10; t=t+1000
    states.den_marker.tick(beside)
    assert(not moved and not fought,'with nothing to fight she goes on with what she was doing there')
    fighter.threat=function() return true end
    t=t+1000
    states.den_marker.tick(beside)
    assert(moved and moved.x<10 and not fought,'she steps away from it before a fight')
    moved=nil; t=t+4000
    states.den_marker.tick(beside)
    assert(not moved and fought,'after three seconds without getting out she deals with what is there')
    -- On her way she keeps to her route across it, and turns on what is upon her only after six seconds.
    den=true; fought=nil
    local crossing=machine()
    t=t+3000; states.travel.tick(crossing)
    assert(not fought and not moved,'on her way across a shooter\'s ground she does not stop to fight')
    for _=1,4 do t=t+1500; states.travel.tick(crossing) end
    assert(not fought,'nor a few seconds on')
    t=t+1500; states.travel.tick(crossing)
    assert(fought,'held there longer than a crossing takes, she fights')
    den=false
    me.x=0; act.move,act.ready,fighter.fight,fighter.threat=kept_move,kept_can,kept_fight,kept_threat
  end
  -- Where everything must die, everything is fought.
  den=false; me.area=8; accept='unset'
  local clearing=machine()
  exploration_stub={tick=function() return 'moving' end}
  states.clear.enter(clearing); states.clear.tick(clearing)
  exploration_stub=nil
  assert(accept==nil,'the Den is cleared whatever its monsters pay')
  charon.now=kept_now
  game.monster_info,game.level_info,fighter.threat,fighter.hostiles=kept.info,kept.level_info,kept.threat,kept.hostiles
  fighter.pack_lethality,trip.mover.through,trip.replan,game.unit=kept.lethality,kept.through,kept.replan,kept.unit
  for name,value in pairs(old) do me[name]=value end
  den=true
end
do -- gear: what is better goes on before anything is sold, one request at a time
  local old={area=me.area,in_town=me.in_town,cursor=me.cursor_item}
  local kept={items=game.items,pick=act.pick_item,equip=act.equip,place=act.place_item,ready=act.ready,now=charon.now,
    hostiles=fighter.hostiles}
  local t=6e11
  charon.now=function() return t end
  act.ready=function() return true end
  fighter.hostiles=function() return {} end
  local staff={id=901,name='Short Staff',location='container',page=0,width=1,height=3}
  local wand={id=902,name='Wand',location='equipped',body_location=4,width=1,height=2}
  local list={staff,wand}
  game.items=function() return list end
  local picked,equipped,placed
  act.pick_item=function(item) picked=item end
  act.equip=function(slot) equipped=slot; return true,5 end
  act.place_item=function(item,page,x,y) placed={item=item,page=page} end
  me.in_town,me.area,me.cursor_item=true,1,false
  local m=machine(); m.data.town_done=nil
  service_needed=true
  gear_offer={item=staff,slot=4,removes={wand},gain='damage'}
  local state,why=states.decide.tick(m)
  assert(state=='gear' and m.data.gear_change==gear_offer,'a better item is put on before the visit that would sell it, got '..tostring(state))
  assert(package.loaded['lib.loot'].wear(staff) and not package.loaded['lib.loot'].wear(wand),'and it is not for sale meanwhile')
  states.gear.enter(m)
  t=t+400; assert(states.gear.tick(m)==nil and picked==staff,'first it is lifted')
  staff.location='cursor'
  t=t+400; assert(states.gear.tick(m)==nil and not equipped,'seen on the cursor')
  t=t+400; assert(states.gear.tick(m)==nil and equipped==4,'then it goes into its slot')
  staff.location,staff.body_location='equipped',4; wand.location,wand.body_location='cursor',nil
  t=t+400; assert(states.gear.tick(m)==nil and placed and placed.item==wand and placed.page=='inventory','what came off goes into the pack')
  wand.location,wand.page='container',0
  t=t+400
  state=states.gear.tick(m)
  assert(state=='decide' and not m.data.gear_change,'and the change is done')
  assert(m.data.gear_off and m.data.gear_off[902],'what came off is remembered, and not put back on')
  -- An item left on the cursor is put away before anything else.
  do
    local stray={id=905,name='Short Bow',location='cursor',width=2,height=3}
    list={staff,wand,stray}; me.cursor_item=true; placed=nil; gear_offer=nil
    local again=machine()
    assert(states.decide.tick(again)=='gear' and not again.data.gear_change,'with something on the cursor that comes first')
    states.gear.enter(again)
    t=t+400; assert(states.gear.tick(again)==nil and placed and placed.item==stray,'it goes into the pack')
    stray.location,stray.page='container',0; me.cursor_item=false
    t=t+400; assert(states.gear.tick(again)=='decide','and she goes on')
    list={staff,wand}
  end
  -- One that does not go through is not tried again in this game.
  gear_offer={item=wand,slot=5,removes={},gain='life'}
  list={staff,wand,{id=903,location='container',page=0}} -- the pack has changed: asked again
  assert(states.decide.tick(m)=='gear','another change is taken up')
  states.gear.enter(m)
  local late=machine(); late.data=m.data; late.elapsed=function() return 9000 end
  assert(states.gear.tick(late)=='decide' and m.data.gear_failed[902],'after eight seconds it is given up')
  list={staff,wand} -- changed again
  gear_offer={item=wand,slot=5,removes={},gain='life'}
  assert(states.decide.tick(m)~='gear','and not offered again')
  gear_offer=nil; service_needed=false
  do -- finds worth reading are read in town, as many as she can pay for after her restock
    local lib=package.loaded['lib.gear']
    local kept_scrolls,kept_identify,kept_gold,kept_stash=service.scrolls,act.identify,me.gold,me.stash_gold
    local ring={id=911,name='Ring',code='rin',location='container',page=0,identified=false,quality=4}
    local cap={id=912,name='Cap',code='cap',location='container',page=0,identified=false,quality=4}
    local scroll={id=913,code='isc',location='container',page=0}
    list={ring,cap,scroll}
    gear_candidate=function(item) return item.code~='isc' end
    local read
    act.identify=function(item,with) read={item=item,with=with} end
    me.gold,me.stash_gold=0,0
    t=t+4000
    local wear=package.loaded['lib.loot'].wear
    assert(not wear(ring),'broke, an unread find is sold as it is')
    me.gold=5000
    t=t+4000
    assert(wear(ring) and wear(cap),'with the gold to read them they are kept from the sale')
    service.scrolls=function() return 1 end
    m=machine()
    assert(states.decide.tick(m)=='identify','with a scroll in the pack they are read after the visit')
    states.identify.enter(m)
    t=t+500; assert(states.identify.tick(m)==nil and read and read.item==ring and read.with==scroll,'a scroll is read on the first')
    ring.identified=true
    t=t+4000; read=nil
    assert(states.identify.tick(m)==nil and read.item==cap,'then on the next')
    t=t+6000
    assert(states.identify.tick(m)==nil and m.data.read_failed[912],'one that will not be read is given up')
    cap.identified=true -- (the bot's own record keeps it off the list; this machine is the test's)
    assert(states.identify.tick(m)=='decide','and with nothing more to read she goes on')
    gear_candidate=nil
    service.scrolls,act.identify,me.gold,me.stash_gold=kept_scrolls,kept_identify,kept_gold,kept_stash
    -- Gear she carries and will not wear is for sale whatever it fetches; what she is about to wear is not.
    local surplus=package.loaded['lib.loot'].surplus
    local old_staff={id=921,name='Short Staff',location='container',page=0,gear=true}
    local gem={id=922,name='Chipped Emerald',location='container',page=0}
    assert(surplus(old_staff) and not surplus(gem),'a staff that came off is surplus; a gem is not gear')
    gear_offer={item=old_staff,slot=4,removes={},gain='damage'}
    list={old_staff,gem}; t=t+4000
    states.decide.tick(machine())
    assert(not surplus(old_staff),'and nothing she is about to put on is')
    gear_offer=nil
  end
  game.items,act.pick_item,act.equip,act.place_item,act.ready,charon.now=kept.items,kept.pick,kept.equip,kept.place,kept.ready,kept.now
  fighter.hostiles=kept.hostiles
  me.area,me.in_town,me.cursor_item=old.area,old.in_town,old.cursor
end
do -- following the pay: no level gates; the optimizer says where she is, and a boss is always fought
  local old={area=me.area,in_town=me.in_town,level=me.level}
  local lib=package.loaded['lib.leveling']
  local kept_choose,kept_quest,kept_info=lib.choose,game.quest,game.level_info
  game.level_info=function(id) return {monster_level=({[37]=12})[id] or 5,name='area'..id} end
  gated_build.follow_pay=true
  den=true; me.area,me.in_town,me.level=6,false,5
  for name in pairs(data) do if name:find('act1.RouteTest.',1,true) then data[name]=nil end end
  data['act1.RouteTest.tower_prep_done']=true
  local scores={left=40,fresh=40,beyond=10}
  local asked
  lib.choose=function(player,build,options)
    asked=options
    if options.candidates[1] and options.candidates[1]>=41 then return 41,'the next act','ranking',scores.beyond end
    if options.spent then return 21,'the Tower','ranking',scores.left end
    return 21,'the Tower','ranking',scores.fresh
  end
  local m=machine()
  local state=states.decide.tick(m)
  assert(state=='farm' and m.data.objective and m.data.objective.area==21,
    'after the Den the planner asks one place what she is doing, got '..tostring(state))
  local first=m.data.objective
  states.farm.enter(m)
  assert(m.data.farm_area==21 and m.data.objective==first,'the farm state carries the objective out; it does not choose again')
  do -- by its waypoint or on foot: asked where she stands when she sets out, not when the objective was made
    local kept_hops,kept_via,kept_area,kept_town=lib.hops_from,first.via,me.area,me.in_town
    first.via=6
    lib.hops_from=function(from) return from==6 and {[21]=2} or {[21]=1} end -- a level away on foot, three by the waypoint
    me.in_town,me.area=false,20
    states.farm.enter(m)
    assert(not m.data.farm_via and m.data.objective==first,'beside it she walks')
    me.in_town,me.area=true,1
    states.farm.enter(m)
    assert(m.data.farm_via and m.data.farm_via[21]==6 and m.data.objective==first,
      'sent to town, she goes back to the same objective by its waypoint')
    lib.hops_from,first.via,me.area,me.in_town=kept_hops,kept_via,kept_area,kept_town
    states.farm.enter(m)
  end
  me.area=3; states.decide.tick(m)
  assert(m.data.objective==first,'the objective does not change with where she stands')
  me.area=6
  me.level=30
  assert(states.farm.tick(m)~='decide','no level ends it: the next area is chosen when this one is cleared')
  me.level=5
  m.data.spent[21]=true; scores.left=20
  assert(states.decide.tick(m)=='save_exit' and not m.data.objective,
    'with its area cleared the objective is worked out again, and a new game taken when that pays much more')
  scores.left=40; m.data.spent={}
  assert(states.decide.tick(m)=='farm' and m.data.objective.area==21,'otherwise the next best area is the objective')
  me.level=6; states.decide.tick(m)
  assert(m.data.objective.level==6,'a level gained is a reason to ask again')
  do -- her means are part of the answer: poor, she is sent where the staff will do
    local kept_items,kept_gold,kept_stash=game.items,me.gold,me.stash_gold
    game.items=function() return {} end
    game.level_info=function(id) return {monster_level=({[2]=1,[3]=2,[4]=3,[21]=7,[37]=12})[id] or 5,name='area'..id} end
    local function offered()
      m.data.objective=nil; states.decide.tick(m)
      local set={}
      for _,level in ipairs(asked.candidates) do set[level]=true end
      return set
    end
    lib.choose=function(player,build,options)
      if options.candidates[1] and options.candidates[1]>=41 then return 41,'the next act','ranking',scores.beyond end
      asked=options
      return options.candidates[1],'somewhere','ranking',40
    end
    me.gold,me.stash_gold=60,0
    local set=offered()
    assert(me.level==6 and m.data.poor and set[2] and set[3] and set[4] and not set[5] and not set[21],
      'with one mana dose within her means the choice is among monsters of level 3 at most: the Cold Plains and the crypts')
    me.level=3; set=offered()
    assert(set[2] and set[4] and not set[5],'and that floor holds below it')
    me.level=10; set=offered()
    assert(set[5] and not set[21],'above it the cap is five levels under her: five at level 10')
    me.level=12; set=offered()
    assert(set[21] and not set[37],'and seven at level 12')
    me.level=6
    me.gold=240; set=offered()
    assert(m.data.poor and not set[21],'a few potions bought do not send her straight back to the Tower')
    me.gold=360; set=offered()
    assert(not m.data.poor and set[21] and set[5],'with six doses within her means every area is open again')
    me.gold=180; set=offered()
    assert(not m.data.poor and set[21],'and she is not poor again until she is down to one')
    do -- what she carries that sells is part of her means: once it would pay, it is sold
      local stub,routing=package.loaded['lib.loot'],package.loaded['lib.travel']
      local kept_value,kept_scrolls,kept_route,kept_now=stub.sell_value,service.portal_scrolls,routing.route,charon.now
      local t=3e12
      charon.now=function() return t end
      local worth=100
      stub.sell_value=function() return worth end
      game.items=function() return {{code='jav',location='container',page=0}} end
      me.gold=60; m.data.objective=nil
      assert(states.decide.tick(m)=='farm' and m.data.poor,'a little to sell leaves her poor, and where the staff will do')
      worth=300; t=t+3000
      service.portal_scrolls=function() return 1 end
      assert(states.farm.tick(m)=='decide','carrying what would pay her way, the farm state asks the planner')
      local step=states.decide.tick(m)
      assert(step=='portal_heal' and m.data.portal_town_only and m.data.portal_passage,
        'and she takes a portal to town to sell it')
      m.data.portal_town_only,m.data.portal_passage=nil,nil
      assert(states.decide.tick(m)=='farm','once tried, the same load does not send her again')
      m.data.cash_left=nil
      service.portal_scrolls=function() return 0 end
      assert(states.decide.tick(m)=='farm','without a scroll the same load is short by the scroll that comes first')
      worth=400; t=t+3000 -- the scroll that must be bought first is counted against it
      routing.route=function() return {6,2,1} end
      assert(states.decide.tick(m)=='save_exit','without a scroll and far from town a new game is the way to the shop')
      m.data.cash_left=nil
      routing.route=function() return {1} end
      assert(states.decide.tick(m)=='retreat','and next door to town she walks')
      me.gold=360; m.data.cash_left=nil; m.data.objective=nil
      assert(states.decide.tick(m)=='farm' and not m.data.poor,'with the gold in hand there is nothing to go and sell for')
      stub.sell_value,service.portal_scrolls,routing.route,charon.now=kept_value,kept_scrolls,kept_route,kept_now
    end
    game.items,me.gold,me.stash_gold=kept_items,kept_gold,kept_stash
    game.level_info=function(id) return {monster_level=({[37]=12})[id] or 5,name='area'..id} end
    lib.choose=function(player,build,options)
      asked=options
      if options.candidates[1] and options.candidates[1]>=41 then return 41,'the next act','ranking',scores.beyond end
      if options.spent then return 21,'the Tower','ranking',scores.left end
      return 21,'the Tower','ranking',scores.fresh
    end
    m.data.objective,m.data.poor=nil,nil
  end
  do -- a new game is weighed with its cost: from town, and one hop more for leaving and loading
    local calls={}
    local kept=lib.choose
    lib.choose=function(player,build,options) calls[#calls+1]=options; return kept(player,build,options) end
    game.level_info=function(id) return {monster_level=({[37]=12})[id] or 5,waypoint=id==6,name='area'..id} end
    m.data.objective=nil; states.decide.tick(m)
    local here,anew
    for _,options in ipairs(calls) do
      if options.spent then here=options elseif options.hops and options.hops[18]==5 then anew=options end
    end
    assert(here and here.hops[18]==3,'from where she is, an area is the walk there or a jump and the walk on, whichever is less')
    assert(anew and anew.hops[2]==3 and not anew.spent,'a new game has nothing cleared and is charged one hop more')
    game.level_info=function(id) return {monster_level=({[37]=12})[id] or 5,name='area'..id} end
    lib.choose=kept; m.data.objective=nil
  end
  -- Progression: once the next act pays more than a fresh game of this one, the boss is the objective.
  scores.beyond=50; me.level=7
  assert(states.decide.tick(m)=='farm' and m.data.objective.area==37 and m.data.objective.progress
    and data['act1.RouteTest.progress'],'an area of the next act that pays more makes Andariel the objective')
  scores.beyond=10; me.level=8; states.decide.tick(m)
  assert(m.data.objective.area==37,'and that is not gone back on')
  -- Two chickens on the way to the boss put it off until the next level.
  bot.progress_failed(me); states.decide.tick(m)
  assert(m.data.objective.area==37,'one chicken on the way is not a reason')
  bot.progress_failed(me); states.decide.tick(m)
  assert(m.data.objective.area==21 and not m.data.objective.progress,'two are: she levels where it pays')
  me.level=9; states.decide.tick(m)
  assert(m.data.objective.area==37,'and at the next level the boss is the objective again')
  data['act1.RouteTest.progress_fails.8']=nil
  states.farm.enter(m)
  assert(m.data.farm_area==37,'she is taken to the level of the boss')
  me.area=37
  game.quest=function(id) return {completed=id==1} end
  assert(states.farm.tick(m)=='decide','on the level of a boss the choice of area is over')
  assert(states.decide.tick(m)=='andariel','and she is fought')
  data['act1.RouteTest.progress']=nil
  gated_build.follow_pay=false
  lib.choose,game.quest,game.level_info=kept_choose,kept_quest,kept_info
  for name,value in pairs(old) do me[name]=value end
end
do -- chests are opened for their gold, so only while she cannot pay for a full restock
  local old={area=me.area,in_town=me.in_town,gold=me.gold,stash_gold=me.stash_gold}
  local kept_threat,kept_hostiles=fighter.threat,fighter.hostiles
  fighter.threat=function() return nil end
  fighter.hostiles=function() return {} end
  den=false; me.area,me.in_town=8,false
  exploration_stub={tick=function() return 'moving' end}
  local m=machine(); states.clear.enter(m)
  me.gold,me.stash_gold=100,200; container_checks=0
  states.clear.tick(m)
  assert(container_checks==1,'short of the gold for a restock she looks into what is beside her')
  me.gold,me.stash_gold=100,900; container_checks=0
  states.clear.tick(m)
  assert(container_checks==0,'with the gold for a full restock, counting the stash, chests are passed by')
  exploration_stub=nil
  fighter.threat,fighter.hostiles=kept_threat,kept_hostiles
  for name,value in pairs(old) do me[name]=value end
  den=true
end
do -- the players count while levelling follows her gold: one more player for each restock she could pay for
  local old={area=me.area,in_town=me.in_town,level=me.level,life=me.life,gold=me.gold,stash_gold=me.stash_gold}
  local kept_players,kept_set,kept_ready,kept_hostiles,kept_threat=game.players,act.players,act.ready,fighter.hostiles,fighter.threat
  local kept_info=game.level_info
  local count,revision=1,1
  game.players=function() return {supported=true,configured=count,revision=revision} end
  act.players=function(n) count=n; revision=revision+1; return true end
  act.ready=function() return true end
  game.level_info=function(id) return {monster_level=({[4]=3,[6]=7})[id] or 5,name='area'..id} end
  local near={}
  fighter.hostiles=function() return near end
  fighter.threat=function() return nil end
  for name in pairs(data) do if name:find('act1.RouteTest.',1,true) then data[name]=nil end end
  data['act1.RouteTest.tower_prep_done']=true
  gated_build.players_by_gold=true
  den=true; me.area,me.in_town,me.level,me.life=5,false,6,me.max_life
  local m=machine()
  local restock=10*60+4*30+100 -- ten mana potions, four healing, a scroll
  local function with(gold) me.gold,me.stash_gold=0,gold; bot.keep_players(m,me); return count end
  near={{id=1}}
  assert(with(restock*3)==1,'the count is not changed in the middle of a fight')
  near={}
  assert(with(restock*3)==4,'three restocks in gold are four players')
  assert(with(restock-1)==1,'short of one restock she is alone, and opens chests')
  assert(with(restock)==2,'one restock is two players')
  assert(with(restock*40)==8,'eight at most')
  bot.px_setback(me,'an emergency portal'); bot.keep_players(m,me)
  assert(count==7,'each emergency takes one player off where she is fighting, at once')
  bot.px_setback(me,'a chicken')
  assert(with(restock*40)==6 and with(restock*3)==2,'and the next another, whatever her gold says')
  assert(with(restock-1)==1,'never below one')
  me.area=6
  assert(with(restock*40)==6,'and in any area whose monsters are at least as high')
  me.area=4
  assert(with(restock*40)==8,'among lower monsters the count follows her gold')
  me.area=5; me.level=7
  assert(with(restock*40)==8,'and a level later it does so everywhere again')
  -- Kept on the way and through every state; only a boss is met at one player.
  states.take_waypoint.enter(m); states.portal_heal.enter(m,'farm')
  assert(count==8,'no state puts the count back to one on entering')
  me.area=36; assert(with(restock*40)==1,'the level that leads to Andariel is entered at one player')
  me.area=37; assert(with(restock*40)==1,'and so is hers')
  me.area=5; den=false; assert(with(restock*40)==1,'before the Den she is alone')
  den=true; data['act1.RouteTest.progress']=true
  assert(with(restock*40)==1,'in progression, on the way to the boss, she is at one player everywhere')
  data['act1.RouteTest.progress']=nil
  den=true; me.in_town=true; count=3; bot.keep_players(m,me)
  assert(count==3,'in town nothing is changed')
  me.in_town=false
  gated_build.players_by_gold=false
  game.players,act.players,act.ready,fighter.hostiles,fighter.threat,game.level_info=kept_players,kept_set,kept_ready,kept_hostiles,kept_threat,kept_info
  for name,value in pairs(old) do me[name]=value end
end
gated_build.enter_gap, gated_build.tower_prep, gated_build.gold_farm = saved_enter_gap, saved_tower_prep, saved_gold_farm
gated_build.follow_pay = saved_follow_pay
gated_build.players_by_gold = saved_by_gold
act.ready = saved_act_ready
for name,module in pairs(saved) do package.loaded[name]=module end
print('campaign routing regressions passed')
