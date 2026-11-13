-- The real candidate enumerator and authored model, with deterministic game snapshots.
package.path='scripts/?.lua;'..package.path
local saved_policy=policy
dofile('tests/policy_reference.lua')
local combat=require 'lib.combat'
local value=require 'lib.value'
local saved={cast_damage=value.cast_damage,life=value.life,resist=value.resist,monster=value.monster}
value.cast_damage=function(skill,target) return (skill.damage or 0)*(1-(target.resist or 0)/100) end
value.life=function() return 100 end
value.resist=function(target,element) return element=='cold' and target.cold_resist or target.resist or 0 end
value.monster=function() return {cold_effect=-50} end
local function scenario(options)
  options=options or {}
  -- `hit` is every monster's damage per blow; without it they are level-one weaklings.
  value.monster=function() return {cold_effect=options.cold_effect or -50,min_life=100,max_life=100,experience=100,level=1,
    attack_min=options.hit,attack_max=options.hit,attack2_min=0,attack2_max=0} end
  local t=10000
  local me={id=100,x=0,y=0,area=2,life=options.life or 100,max_life=100,mana=options.mana or 100,
    max_mana=100,alive=true,stamina=100,max_stamina=100,mode=1,left_skill=0,right_skill=0}
  local enemies=options.enemies or {{x=5,y=0}}
  for i,u in ipairs(enemies) do
    u.id=i; u.type='monster'; u.alive=true; u.owner=-1; u.attackable=true
    u.class_id=1; u.name='test'; u.life_percent=100
  end
  local skills=options.skills or {{name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20}}
  local calls={casts=0,moves=0,searches=0}
  charon={now=function() return t end,frame=function() return t end}
  game={me=function() return me end,units=function(kind) return kind=='monster' and enemies or {} end,
    unit=function(_,id) return enemies[id] end,skills=function() return skills end,
    state=function(_,id) return type(options.chilled)=='table' and options.chilled[id] or options.chilled==true end,
    difficulty_info=function() return {static_field_min=0} end,
    items=function() return options.items or {} end,
    attack_damage=function() return {min=12,max=12} end}
  act={ready=function() return true end,select_skill=function(_,id)
    calls.skill=id; return not options.selection_refused end,
    cast_at=function(hand,target,standstill)
      calls.casts=calls.casts+1; calls.hand,calls.standstill=hand,standstill
      return not options.cast_refused
    end,
    cast=function() calls.casts=calls.casts+1; return not options.cast_refused end,
    move=function() calls.moves=calls.moves+1; return not options.move_refused end}
  map={line_of_sight=function() return not options.blocked end,
    hazard_clear=function() return not options.fire end,
    safe_spot=function(o)
      calls.searches=calls.searches+1
      if options.spot then return options.spot end
      return {found=false,path={},danger_here=options.danger or 0,danger_there=0,
        missile_here=options.incoming or 0,open_sectors=8,hazard_here=0,hazard_there=0}
    end}
  local c=combat.new(options.build or {range=7,element='lightning'})
  c.melee_ok=options.melee
  c.target_id=1
  c.target_progress=t
  c:observe_missiles(me)
  return c,me,enemies,calls,function(delta) t=t+delta end
end
local function selected(c,me,target,global)
  local actions,rows=c.neural:candidates(c,me,target,global)
  local i=c.neural:rank(actions,rows)
  return actions[i],actions,rows
end
local function feature(c,row,name) return c.neural.scorer:get(row,name) end
local function names(actions)
  local result={}
  for _,a in ipairs(actions) do result[a.name]=true end
  return result
end
do
  for _,life in ipairs{100,45} do
    for _,melee in ipairs{false,true} do
      for _,incoming in ipairs{0,2} do
        local c,me,enemies,calls=scenario{life=life,incoming=incoming,
          skills=melee and {} or nil,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},danger=7.6}
        c:fight(me,12)
        assert(calls.casts==1 and calls.moves==0,
          'a trapped ready fighter must attack instead of waiting under the same melee or missile exposure')
        assert(calls.hand==(melee and 'left' or 'right') and calls.standstill==not melee,
          'trapped pressure must preserve native melee versus stand-still bolt requests')
      end
    end
  end
  for _,refused in ipairs{false,true} do
    local c,me,enemies,calls,advance=scenario{move_refused=refused,hit=12,
      enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},
      spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7.6,danger_there=0,
        missile_here=0,open_sectors=2,hazard_there=0}}
    c:fight(me,12)
    assert(calls.moves==1 and calls.casts==0,'a usable escape from a pack that hurts must still beat attacking')
    if refused then
      assert(c.last_action==0 and not c.escape_progress and not c.plan and c.escape_retry_until==10400,
        'native refusal must not claim movement progress or an accepted action')
      advance(40); c:fight(me,12)
      assert(calls.casts==1 and calls.moves==1 and c.last_action==10040 and not c.escape_progress
        and not c.plan and c.escape_retry_until==10400,
        'the refusal cooldown must allow a real attack without inventing escape progress or retrying movement')
    end
  end
  for _,guard in ipairs{'busy','paced','fire','mana','immune'} do
    local enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}}
    if guard=='immune' then for _,unit in ipairs(enemies) do unit.resist=100 end end
    local c,me,targets,calls=scenario{enemies=enemies,danger=7.6,mana=guard=='mana' and 0 or 100,fire=guard=='fire'}
    if guard=='busy' then act.ready=function() return false end end
    if guard=='paced' then c.last_action=10000 end
    if guard=='fire' then c.ground_hazards={{x=0,y=0,size=1}} end
    c:fight(me,12)
    assert(calls.casts==0 and calls.moves==0,'trapped attack preference must preserve hard constraints: '..guard)
    if guard=='mana' then assert(c.resting,'an unaffordable bolt must still request mana recovery') end
    if guard=='immune' then assert(c.target_id==nil and c.ignored[1]>10000,'spell immunity must still release the target') end
  end
  local builds=require 'lib.builds'
  for _,class in ipairs{'barbarian','amazon','necromancer','paladin','druid','assassin'} do
    local c,me,enemies,calls=scenario{build=builds.for_class(class),skills={},enemies={{x=2,y=0}}}
    assert(c.neural,'all combat constructors must install the neural policy')
    c:fight(me,12)
    assert(calls.casts==1 and calls.hand=='left' and calls.standstill==false and calls.moves==0,
      'non-spell builds retain a normal basic melee attack: '..class)
  end
  for _,case in ipairs{{}, {mana=0}, {unusable=true}, {owner=123}, {level=0}, {blocked=true}, {distance=4.01}} do
    local c,me,enemies,calls=scenario{build=builds.barbarian,mana=case.mana,blocked=case.blocked,
      skills={{name='Bash',id=126,level=case.level or 1,mana=2,usable=not case.unusable,owner=case.owner or -1,damage=4}},
      enemies={{x=case.distance or 2,y=0}}}
    local actions=c.neural:candidates(c,me,enemies[1],false)
    local permitted=not (case.mana or case.unusable or case.owner or case.level or case.blocked or case.distance)
    assert(not not names(actions).Bash==permitted,'Bash must respect innate ownership, level, usability, mana, contact and sight')
    c:fight(me,12)
    if permitted then
      assert(calls.casts==1 and calls.skill==126 and calls.hand=='left' and not calls.standstill,
        'the neural policy must rank and execute usable Bash as a native melee unit action')
    elseif not case.blocked and not case.distance then
      assert(calls.casts==1 and calls.skill~=126 and calls.hand=='left','unavailable Bash must preserve basic melee fallback')
    else
      assert(calls.casts==0,'out-of-contact or obscured melee cannot start an attack')
    end
  end
end

do
  local weights=require 'lib.combat_weights'
  for slot,old_name in pairs({[4]='danger_after',[12]='resource_shortage'}) do
    local expected=weights.features[slot]
    weights.features[slot]=old_name
    local ok,message=pcall(require('lib.neural_combat').new)
    weights.features[slot]=expected
    assert(not ok and tostring(message):find('feature',1,true),'old same-shaped weights must fail clearly')
  end
end

do
  local function retreat_case(options)
    options=options or {}
    options.enemies=options.enemies or {{x=4,y=0}}
    options.hit=80 -- one blow from this target is most of our life: kiting it is worthwhile
    options.skills={{name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
      {name='Frost Nova',id=44,level=1,mana=9,usable=not options.nova_unusable,damage=2}}
    options.spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=2,danger_there=0,
      missile_here=options.incoming or 0,missile_there=0,open_sectors=6,hazard_there=0}
    return scenario(options)
  end
  local c,me,enemies,calls,advance=retreat_case()
  local actions,rows=c.neural:candidates(c,me,enemies[1],false)
  local eligible,eligible_rows={},{}
  for i,a in ipairs(actions) do
    if not a.kite_only then eligible[#eligible+1],eligible_rows[#eligible_rows+1]=a,rows[i] end
  end
  local baseline=policy.rank(c.neural.model,eligible_rows)
  assert(eligible[baseline].name=='escape','the chill setup fixture must first intend to kite')
  for _,row in ipairs(rows) do
    assert(feature(c,row,'target_chill')==1 and feature(c,row,'kite_intent')==0,
      'baseline exposes a reachable unchilled target without kite intent')
  end
  assert(actions[c.neural:rank(actions,rows)].name=='Frost Nova','an unchilled target must be chilled before an intended kite')
  assert(feature(c,rows[1],'kite_intent')==1,'only a baseline escape should enable the second scoring pass')
  c.neural:fight(c,me,enemies[1])
  assert(calls.casts==1 and calls.moves==0 and calls.skill==44,'runtime execution must use the two-pass chill setup decision')
  local kite,during_cooldown=selected(c,me,enemies[1])
  assert(kite.name=='escape' and not names(during_cooldown)['Frost Nova'],
    'an accepted Nova owns its real cooldown and lets the intended kite proceed')
  advance(1200)
  game.state=function(_,id,state) return id==enemies[1].id and state==11 end
  local action,_,chilled_rows=selected(c,me,enemies[1])
  local _,chilled_actions=selected(c,me,enemies[1])
  assert(action.name~='Frost Nova' and not names(chilled_actions)['Frost Nova']
    and feature(c,chilled_rows[1],'target_chill')==0,'a chilled primary must not receive another chill setup')
  for _,case in ipairs{
    {mana=8}, {nova_unusable=true}, {enemies={{x=4,y=0,cold_resist=100}},invalid_primary=true},
    {cold_effect=0,invalid_primary=true}, {enemies={{x=7.1,y=0}},invalid_primary=true},
    {incoming=2},
  } do
    local fighter,player,targets=retreat_case(case)
    local decision,_,features=selected(fighter,player,targets[1])
    assert(decision.name=='escape',
      'unavailable, ineffective, unreachable or missile-inhibited chill must preserve escape, got '..decision.name)
    if case.invalid_primary then
      assert(feature(fighter,features[1],'target_chill')==0,
        'immune, unchillable or distant targets must not receive the setup feature')
    end
  end
  do -- A target behind a wall neither threatens us nor qualifies for a chill setup.
    local fighter,player,targets=retreat_case{blocked=true}
    local decision,_,features=selected(fighter,player,targets[1])
    assert(decision.name=='wait' and feature(fighter,features[1],'target_chill')==0
      and feature(fighter,features[1],'lethality_after')==0,'an unseen monster must not trigger kiting or a setup')
  end
  -- Chilling the primary must not discard a useful defensive AoE for other hostiles.
  c,me,enemies=retreat_case{chilled={[1]=true},life=50,
    enemies={{x=3,y=0},{x=2,y=1},{x=3,y=1}}}
  local eligible=c.neural:candidates(c,me,enemies[1],false)
  assert(names(eligible)['Frost Nova'],'unchilled nearby enemies must retain defensive Nova after the primary is chilled')
end

do
  -- Live range gap: no cast yet, no useful approach cell, only a harmless retreat.
  local c,me,enemies=scenario{enemies={{x=7.2,y=0}},skills={
    {name='Fire Bolt',id=36,level=1,mana=2,usable=true,damage=4}},
    spot={found=true,x=-2,y=0,path={{x=-2,y=0}},danger_here=.2,danger_there=.05,
      missile_here=0,open_sectors=8,hazard_there=0}}
  assert(selected(c,me,enemies[1]).name=='wait',
    'a safe range gap must not bounce away simply because waiting had a penalty')
end
do
  local c,me,enemies=scenario{skills={
    {name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
    {name='Fire Bolt',id=36,level=1,mana=2,usable=true,damage=10}}}
  assert(selected(c,me,enemies[1]).name=='Charged Bolt','best damage per cast should win')
end
do
  local c,me,enemies,calls=scenario{skills={
    {name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
    {name='Frost Nova',id=44,level=1,mana=9,usable=true,damage=2}},enemies={{x=5,y=0,unique=true}}}
  local action,actions=selected(c,me,enemies[1])
  assert(names(actions)['Frost Nova'] and action.name=='Charged Bolt',
    'an elite that cannot hurt us is shot; control is offered but not worth the cast')
end
do
  local c,me,enemies=scenario{mana=3,skills={
    {name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
    {name='Fire Bolt',id=36,level=1,mana=2,usable=true,damage=1}}}
  local _,actions=selected(c,me,enemies[1])
  assert(not names(actions)['Fire Bolt'],'mana shortage must not promote the weak backup')
  assert(c.neural:fight(c,me,enemies[1])=='resting')
end
do
  local c,me,enemies=scenario{enemies={{x=2,y=0,resist=100}}}
  c.neural:fight(c,me,enemies[1])
  assert(not c.resting and c.target_id==nil and c.ignored[1]>10000,'immunity must release the target')
end
for _,life in ipairs{100,30} do
  local c,me,enemies=scenario{life=life,hit=12,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},
    spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
      missile_here=0,open_sectors=2,hazard_there=0}}
  assert(selected(c,me,enemies[1]).name=='escape','a pack that hurts must favor a safe native route')
end
for _,life in ipairs{100,70} do
  local c,me,enemies=scenario{life=life,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},
    spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
      missile_here=0,open_sectors=2,hazard_there=0}}
  assert(selected(c,me,enemies[1]).name=='Charged Bolt','weak monsters in our face are shot, not fled from')
end
do
  local c,me,enemies=scenario{life=50,hit=10,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},
    skills={{name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
      {name='Frost Nova',id=44,level=1,mana=9,usable=true,damage=2}},
    spot={found=false,path={},danger_here=7,danger_there=7,
      missile_here=0,open_sectors=2,hazard_there=0}}
  local action,actions=selected(c,me,enemies[1])
  assert(names(actions)['Frost Nova'] and action.attack,
    'a trapped caster keeps attacking the surrounding pack instead of waiting for more hits')
end
do
  local c,me,enemies=scenario{skills={},enemies={{x=2,y=0}},
    spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=3,danger_there=0,
      missile_here=0,open_sectors=6,hazard_there=0}}
  assert(selected(c,me,enemies[1]).name=='melee','healthy untrained melee should fight isolated ordinary enemies')
end
do
  local c,me,enemies,calls=scenario{fire=true}
  c.ground_hazards={{x=0,y=0,size=2}}
  c.neural:fight(c,me,enemies[1])
  assert(calls.casts==0,'known ground fire prohibits starting attacks')
end
do
  local c,me,enemies=scenario{blocked=true}
  local _,actions=selected(c,me,enemies[1])
  assert(not names(actions)['Charged Bolt'],'a unit attack needs confirmed line of sight')
  enemies[1].x=10
  local _,far=selected(c,me,enemies[1])
  assert(not names(far)['Charged Bolt'],'bolts cannot be started outside usable range')
end
do
  local c,me,enemies=scenario{}
  local _,actions=selected(c,me,enemies[1],true)
  local available=names(actions)
  assert(not available['Charged Bolt'] and not available.approach and not available['Static Field'],
    'travel arbitration cannot start offensive attacks or off-route approaches')
end
do
  local c,me,enemies,calls=scenario{move_refused=true,hit=12,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},
    spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
      missile_here=0,open_sectors=2,hazard_there=0}}
  c.neural:fight(c,me,enemies[1])
  assert(calls.moves==1 and c.last_action==0 and c.plan==nil and c.escape_retry_until>10000,
    'refused movement must retain shared refusal semantics')
end
do
  local c,me,enemies,calls=scenario{cast_refused=true,skills={
    {name='Frost Nova',id=44,level=1,mana=9,usable=true,damage=2}},enemies={{x=5,y=0,unique=true}}}
  c.neural:fight(c,me,enemies[1])
  assert(calls.casts==1 and c.neural.nova_at<0 and c.last_action==0,'refused casts do not consume cooldown')
end
do
  local c,me,enemies=scenario{selection_refused=true}
  assert(not pcall(c.neural.fight,c.neural,c,me,enemies[1]),'skill-selection refusal must surface')
end
do
  local c,me,enemies,calls,advance=scenario{}
  selected(c,me,enemies[1]); selected(c,me,enemies[1])
  assert(calls.searches==2,'short native candidate searches are cached within a tick')
  advance(120); selected(c,me,enemies[1])
  assert(calls.searches==4,'native search budget renews once after 120 ms')
end
do
  local c,me,enemies=scenario{mana=0,melee=true,enemies={{x=6,y=0}},
    spot={found=true,x=4,y=0,path={{x=4,y=0}},danger_here=0,danger_there=0,
      missile_here=0,open_sectors=8,hazard_there=0}}
  local seen_range
  local original=map.safe_spot
  map.safe_spot=function(options)
    if options.target then seen_range=options.range end
    return original(options)
  end
  assert(selected(c,me,enemies[1]).name=='approach' and seen_range==2.25,
    'mana-starved melee fallback must approach into usable melee range')
end
do
  -- Live Gargantuan Beast contact: the adjacent ground corner was repeatedly refused.
  local c,me,enemies,calls=scenario{mana=0,melee=true,move_refused=true,
    enemies={{x=7599.5,y=5101.5}},spot={found=true,x=7600,y=5102,
      path={{x=7599,y=5103},{x=7600,y=5102}},danger_here=1,danger_there=1,
      missile_here=0,open_sectors=8,hazard_there=0}}
  me.x,me.y=7599.5,5104.5
  game.attack_damage=function() return {min=3,max=3} end
  c.neural:fight(c,me,enemies[1])
  assert(calls.casts==1 and calls.hand=='left' and calls.standstill==false and calls.moves==0,
    'contact melee must beat closing a tiny deficit through a refused ground corner')
end
do
  local c,me,enemies=scenario{}
  c.resting=true
  c.neural:choose(c,me,enemies[1],true)
  assert(c.resting,'global arbitration must preserve mana-resting state')
end
do
  local c,me,enemies=scenario{enemies={{x=12,y=0},{x=3,y=0},{x=4,y=0}},skills={
    {name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
    {name='Static Field',id=42,level=1,mana=10,usable=true,damage=0}}}
  local _,actions,rows=selected(c,me,enemies[1])
  for i,a in ipairs(actions) do
    if a.name=='Static Field' then
      assert(feature(c,rows[i],'tempo')>0 and feature(c,rows[i],'finish')==0,
        'aggregate AoE damage cannot imply finishing an out-of-range target')
    end
  end
end
do
  local c,me,enemies=scenario{hit=12,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},
    spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
      missile_here=0,open_sectors=2,hazard_there=0}}
  c.neural.wait_at,c.neural.wait_target=5000,1
  c.neural:fight(c,me,enemies[1])
  assert(c.neural.wait_at==nil,'accepted tactical movement resets nonproductive waiting')
end
do
  local c,me,enemies,calls=scenario{mana=0,melee=true,enemies={{x=2,y=0}}}
  assert(c.neural:fight(c,me,enemies[1])=='fighting' and calls.casts==1 and not c.resting,
    'permitted melee fallback must not request a mana-resting town trip')
  assert(not c:out_of_mana(),'accepted fallback melee must keep campaign recovery inactive')
end
do
  local c,me,enemies,calls,advance=scenario{skills={
    {name='Frost Nova',id=44,level=1,mana=9,usable=true,damage=2}},enemies={{x=5,y=0,unique=true}}}
  c.missile_lanes={{x=20,y=0,to_x=22,to_y=0}}
  assert(c:dodge(me,enemies[1]) and c.dodge_cast and calls.casts==1)
  act.ready=function() return false end
  advance(40)
  assert(c:dodge(me,enemies[1]),'global defensive cast owns the tick through its busy animation')
  act.ready=function() return true end
  advance(80)
  assert(not c:dodge(me,enemies[1]) and not c.dodge_cast,'ready travel resumes after defensive cast')
end
do
  local c,me,enemies,calls=scenario{}
  selected(c,me,enemies[1],true)
  selected(c,me,enemies[1],false)
  selected(c,me,enemies[1],true)
  selected(c,me,enemies[1],false)
  assert(calls.searches==2,'global and fight arbitration must share the native escape query')
end
do
  local c,me,enemies,_,advance=scenario{enemies={{x=5,y=0}},
    spot={found=true,x=3,y=0,path={{x=1,y=0},{x=2,y=0},{x=3,y=0}},danger_here=0,danger_there=0,
      missile_here=0,open_sectors=8,hazard_there=0}}
  assert(selected(c,me,enemies[1]).name=='Charged Bolt','working bolts should keep their casting window')
  local _,_,working_rows=selected(c,me,enemies[1])
  assert(feature(c,working_rows[1],'kite_intent')==0,'a productive attack must not arm kite intent')
  c.misses=3
  local range
  local original=map.safe_spot
  map.safe_spot=function(options) if options.target then range=options.range end; return original(options) end
  advance(120) -- the next native search
  assert(selected(c,me,enemies[1]).name=='approach' and range==2.5,
    'repeated misses must discount the volley here and close to where it connects')
end
do
  -- Out of range, a spot beside what would take a third of our life is
  -- traded for one where the farthest bolts reach.
  local ranges
  local function spots(options)
    if options.target then
      ranges[#ranges+1]=options.range
      local x=15-options.range
      return {found=true,x=x,y=0,path={{x=x,y=0}},danger_here=0,danger_there=0,
        missile_here=0,missile_there=0,open_sectors=8,hazard_there=0}
    end
    return {found=false,path={},danger_here=0,missile_here=0,open_sectors=8}
  end
  local c,me,enemies=scenario{hit=20,enemies={{x=15,y=0}}}
  ranges={}; map.safe_spot=spots
  local a=selected(c,me,enemies[1])
  assert(ranges[1]==2.5 and ranges[2]==9.5 and a.name=='approach' and a.spot.x==5.5,
    'a target beyond the bolts whose side is dangerous is approached to casting range, got '..a.name)
  c,me,enemies=scenario{hit=1,enemies={{x=15,y=0}}}
  ranges={}; map.safe_spot=spots
  a=selected(c,me,enemies[1])
  assert(#ranges==1 and a.name=='approach' and a.spot.x==12.5,
    'a harmless one is walked up to as before, got '..a.name)
end
do
  local c,me,enemies,calls=scenario{enemies={{x=8,y=0}},
    spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=1,danger_there=0,
      missile_here=0,missile_there=0,open_sectors=8,hazard_there=0}}
  c.missile_lanes={{x=20,y=0,to_x=22,to_y=0}}
  local plan={neural=true,why='neural approach'}
  c.plan=plan
  assert(not c:dodge(me,enemies[1]) and calls.moves==0 and c.plan==plan,
    'distant harmless flight must not trigger global retreats or cancel combat approaches')
end
do
  -- Engaged, the step out of a lane is the fight's to take or leave: the
  -- missile check takes the tick only when the fight itself would step away.
  local spot={found=true,x=-1,y=0,path={{x=-1,y=0}},danger_here=7,danger_there=6.9,
    missile_here=1,missile_there=0,open_sectors=1,hazard_there=0}
  local skills={{name='Charged Bolt',id=38,level=6,mana=5,usable=true,damage=400}}
  local c,me,enemies,calls=scenario{hit=12,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},skills=skills,spot=spot}
  c.missile_lanes={{x=6,y=0,to_x=0,to_y=0}}
  local free=c.neural:dodge(c,me,enemies[1])
  c,me,enemies,calls=scenario{hit=12,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},skills=skills,spot=spot}
  c.missile_lanes={{x=6,y=0,to_x=0,to_y=0}}
  c.target_id=enemies[1].id
  assert(free and c.neural:dodge(c,me,enemies[1]) and calls.moves==1,
    'with no fight in hand, and in one that would itself step away, the lane is stepped out of')
  -- A fight that would rather cast: the ranking is the policy's; here it is told to prefer the bolt.
  c,me,enemies,calls=scenario{hit=12,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},skills=skills,spot=spot}
  c.missile_lanes={{x=6,y=0,to_x=0,to_y=0}}
  c.target_id=enemies[1].id
  local policy_rank=c.neural.rank
  c.neural.rank=function(self,actions,rows)
    for index,action in ipairs(actions) do
      if action.name=='Charged Bolt' then return index,{} end
    end
    return policy_rank(self,actions,rows)
  end
  assert(not c.neural:dodge(c,me,enemies[1]) and calls.moves==0 and calls.casts==0,
    'engaged, the missile check leaves the tick to a fight that would rather cast')
end
do
  local c,me,enemies=scenario{enemies={{x=10.2,y=0}},skills={
    {name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
    {name='Fire Bolt',id=36,level=1,mana=2,usable=true,damage=10}}}
  local range
  map.safe_spot=function(options)
    if options.target then
      range=options.range
      return {found=true,x=1,y=0,path={{x=1,y=0}},danger_here=.1,danger_there=.1,
        missile_here=0,open_sectors=8,hazard_there=0}
    end
    return {found=false,path={},danger_here=.1,missile_here=0,open_sectors=8}
  end
  assert(selected(c,me,enemies[1]).name=='approach' and range==2.5,
    'beyond casting range the main volley should close to where it connects')
end
do
  local c,me,enemies,calls=scenario{enemies={{x=5,y=0}},skills={
    {name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=20},
    {name='Frost Nova',id=44,level=1,mana=9,usable=true,damage=2}}}
  c.missile_lanes={{x=20,y=0,to_x=22,to_y=0}}
  local plan={neural=true,why='neural approach'}
  c.plan=plan
  assert(not c:dodge(me,enemies[1]) and calls.casts==0 and calls.moves==0 and c.plan==plan,
    'a harmless distant missile must not turn single-target kite setup into an unsolicited Nova')
  assert(selected(c,me,enemies[1]).name=='Charged Bolt',
    'single-target setup must not displace productive combat before the network intends to kite')
end
do
  -- One monster that hurts: Static Field and a few volleys end it in four casts.
  local spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
    missile_here=0,open_sectors=2,hazard_there=0}
  local skills={{name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=5},
    {name='Static Field',id=42,level=1,mana=9,usable=true,damage=0}}
  local potions={{code='hp1',location='belt',grid_x=0},{code='hp1',location='belt',grid_x=1}}
  local c,me,enemies=scenario{hit=15,enemies={{x=3,y=0,boss=true}},skills=skills,items=potions,spot=spot}
  local a,_,rows=selected(c,me,enemies[1])
  assert(a.attack and c.neural.stay>0.5,
    'a fight that ends in a few casts with healing in hand is finished where we stand, got '..a.name)
  c:fight(me,12)
  assert(c:winning(0.4) and c.mana_need and c.mana_need>=36,
    'the projection of the fight in hand is published for the survival rules and the potions')
  c,me,enemies=scenario{hit=15,life=50,enemies={{x=3,y=0,boss=true}},skills=skills,spot=spot}
  a,_,rows=selected(c,me,enemies[1])
  assert(c.neural.stay<0.25,
    'the same monster is kited when the fight would take the life we have left, got '..a.name)
  c,me,enemies=scenario{hit=15,items=potions,enemies={{x=3,y=0}},skills=skills,spot=spot}
  a=selected(c,me,enemies[1])
  assert(c.neural.stay==0 and a.name=='escape',
    'the same monster without the standing of a boss is kited as packs are, got '..a.name)
  c,me,enemies=scenario{hit=15,mana=20,enemies={{x=3,y=0,boss=true}},skills=skills,items=potions,spot=spot}
  a,_,rows=selected(c,me,enemies[1])
  assert(c.neural.stay==0,'a fight the mana in hand cannot finish is not decided where we stand')
end
do
  -- The tables call these three weak; the life we are losing says otherwise.
  local spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
    missile_here=0,open_sectors=2,hazard_there=0}
  local c,me,enemies=scenario{hit=2,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},spot=spot}
  assert(selected(c,me,enemies[1]).name=='Charged Bolt','three weak monsters are shot where they stand')
  c.losses={{at=10000,loss=60}}
  local a=selected(c,me,enemies[1])
  assert(a.name=='escape' and c.neural.harder>2,
    'monsters that take life faster than their table damage are treated as that much stronger, got '..a.name)
  me.life=90; c:observe_life(me); me.life=70; c:observe_life(me); me.life=100; c:observe_life(me)
  assert(math.abs(c:intake()-40)<1e-9,'only drops in life count toward the measured intake')
end
do
  -- A boss: too much life to finish where we stand, too fast to shed. With a
  -- way out it is struck once, the step away is taken, and it is struck again.
  local old_life=value.life
  value.life=function() return 1000 end
  local spot={found=true,x=-6,y=0,path={{x=-6,y=0}},danger_here=7,danger_there=0,
    missile_here=0,open_sectors=2,hazard_there=0}
  local skills={{name='Charged Bolt',id=38,level=4,mana=10,usable=true,damage=5},
    {name='Static Field',id=42,level=1,mana=9,usable=true,damage=0}}
  local c,me,enemies,calls,advance=scenario{hit=20,enemies={{x=4,y=0,boss=true}},skills=skills,spot=spot}
  local a=selected(c,me,enemies[1])
  assert(c.neural.stay==0 and a.name=='Static Field','a boss in reach is struck with what takes most of it, got '..a.name)
  c:fight(me,12)
  assert(calls.casts==1 and c.neural.struck,'the strike is cast')
  advance(600)
  a=selected(c,me,enemies[1])
  assert(a.name=='Static Field','with most of our life in hand the boss is struck again at once, got '..a.name)
  -- Below the floor the step away is owed before the next cast.
  c,me,enemies,calls,advance=scenario{hit=20,life=55,enemies={{x=4,y=0,boss=true}},skills=skills,spot=spot}
  a=selected(c,me,enemies[1])
  assert(a.name=='Static Field','hurt, she still strikes once, got '..a.name)
  c:fight(me,12)
  assert(calls.casts==1 and c.neural.struck,'the strike is cast and the step away is owed')
  advance(600)
  a=selected(c,me,enemies[1])
  assert(a.name=='escape','after a strike the next cast in danger counts in full: leave first, got '..a.name)
  advance(2600)
  a=selected(c,me,enemies[1])
  assert(a.name=='Static Field','once the step away is paid for, the next strike is due, got '..a.name)
  -- Without a way out there is no strike to speak of: the exposure counts in full.
  c,me,enemies=scenario{hit=20,enemies={{x=4,y=0,boss=true}},skills=skills}
  local _,_,rows=selected(c,me,enemies[1])
  assert(not c.neural.strike and math.abs(feature(c,rows[1],'lethality_after')-(20*5/7*4/100)/(1+20*5/7*4/100))<1e-6,
    'a trapped fighter is exposed for the whole window, got '..tostring(c.neural.strike)..' '..feature(c,rows[1],'lethality_after'))
  -- Short of mana, a volley from where a quarter of it connects waits: the
  -- mana buys ten times more as Static Field once the boss has come.
  c,me,enemies=scenario{hit=20,mana=16,enemies={{x=9.5,y=0}},skills=skills}
  a,_,rows=selected(c,me,enemies[1])
  assert(a.name=='wait' and feature(c,rows[1],'thrift')<0.1,'scarce mana is kept for its best use, got '..a.name)
  c,me,enemies=scenario{hit=20,mana=100,enemies={{x=9.5,y=0}},skills=skills}
  assert(selected(c,me,enemies[1]).name=='Charged Bolt','a full pool spends freely')
  value.life=old_life
end
do
  -- A chilled monster walks at half speed: what it reaches within the window halves.
  local c,me,enemies=scenario{hit=20,enemies={{x=6,y=0}},chilled=true}
  local _,_,rows=selected(c,me,enemies[1])
  assert(feature(c,rows[#rows],'lethality_after')==0,'a chilled melee monster six subtiles off does not reach us in the window')
  c,me,enemies=scenario{hit=20,enemies={{x=6,y=0}},cold_effect=0}
  _,_,rows=selected(c,me,enemies[1])
  assert(feature(c,rows[#rows],'lethality_after')>0.2,'the same monster unchilled does')
end
do
  -- What a class took beyond its table is remembered for the session.
  local kept={}
  store={get=function(k) return kept[k] end,set=function(k,v) kept[k]=v end}
  assert(value.harder(10)==3 and value.harder(11)==3 and not value.learn(10,2) and value.learn(10,3.5)
    and value.harder(10)==3.5,'what every session measured is known from the start, and can still be corrected upward')
  kept['value.harder.10']=nil
  assert(value.harder(901)==1 and value.learn(901,2.5) and value.harder(901)==2.5 and kept['value.harder.901']==2.5,
    'a learned factor is kept in the store')
  assert(not value.learn(901,2) and value.harder(901)==2.5 and value.learn(901,9) and value.harder(901)==4,
    'learning only raises the factor, up to its cap')
  local c,me,enemies=scenario{hit=4,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}}}
  c.losses={{at=10000,loss=60}}
  selected(c,me,enemies[1])
  assert(value.harder(1)==1,'one moment of the model is not yet compared with two seconds of losses')
  local t=10000
  charon.now=function() return t end
  charon.frame=charon.now
  for _=1,3 do
    t=t+400; c.losses={{at=t,loss=60}}
    selected(c,me,enemies[1])
  end
  assert(value.harder(1)>2,'losses far off the table are remembered against the class that took most')
  -- Not while missiles fly: what a fireball takes is no blow of whoever stands nearest.
  kept['value.harder.1']=nil
  c,me,enemies=scenario{hit=4,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}}}
  t=40000
  charon.now=function() return t end
  charon.frame=charon.now
  c.missile_lanes={{x=20,y=0,to_x=22,to_y=0}}
  for _=1,4 do
    t=t+400; c.losses={{at=t,loss=60}}
    selected(c,me,enemies[1])
  end
  assert(value.harder(1)==1,'losses taken under missiles are not laid at the door of the nearest class')
  -- And the other way: a class that lands a fraction of what its table promises is rated down to it.
  assert(value.lands(1)==1,'until she has stood in its reach a class is taken at its table')
  c,me,enemies=scenario{hit=10,enemies={{x=2,y=0}}}
  t=60000
  charon.now=function() return t end
  charon.frame=charon.now
  local before=c.neural:candidates(c,me,enemies[1])
  for _=1,40 do t=t+400; selected(c,me,enemies[1]) end -- sixteen seconds beside it, and not a scratch
  assert(value.lands(1)<1 and value.lands(1)>=0.2,'sixteen seconds in its reach unhurt: it does not land what the table says, got '..value.lands(1))
  local rated=value.lands(1)
  assert(math.abs(value.dps({class_id=1})-value.table_dps({class_id=1})*rated)<1e-9,'and that is what it is taken to do from now on')
  c.lost_total=0
  for _=1,40 do t=t+400; c.lost_total=c.lost_total+4*rated*10*0.4; selected(c,me,enemies[1]) end -- now it lands four times that
  assert(value.lands(1)>rated,'when it starts landing more, the rating follows it back up')
  kept['value.lands.1']=nil
  -- A blow taken beside a monster she has since stepped back from is what its table says, not many times it.
  kept['value.harder.1']=nil
  c,me,enemies=scenario{hit=10,enemies={{x=2,y=0}}}
  t=20000
  charon.now=function() return t end
  charon.frame=charon.now
  for _=1,4 do t=t+400; selected(c,me,enemies[1]) end
  enemies[1].x=8.5 -- at the edge of its reach the tables give it a fourteenth of its blow
  t=t+200; c.losses={{at=t,loss=22}}
  selected(c,me,enemies[1])
  assert(value.harder(1)==1,'the model is asked about the seconds the losses are from, not about where she stands now')
  store=nil
end
do
  -- Dry: the attack is unaffordable and nothing restores mana. It lasts until half the pool is back.
  local c,me,enemies,_,advance=scenario{mana=3}
  assert(not c:dry(me),'before any fight the attack cost is unknown: not dry')
  c:fight(me,12)
  assert(c.attack_mana==10 and c:dry(me),'an unaffordable attack with no potions is dry')
  me.mana=20; assert(c:dry(me),'a trickle of mana does not end it')
  me.mana=50; assert(not c:dry(me),'half a pool does')
  me.mana=3; assert(c:dry(me))
  game.items=function() return {{code='mp1',location='belt',grid_x=0}} end
  advance(40)
  assert(not c:dry(me),'a mana potion in hand is not dry')
end
do
  -- Out of mana where staff hits are allowed: what dies in a few blows and
  -- hurts little is clubbed; what does not is left alone as before.
  local spot={found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
    missile_here=0,open_sectors=2,hazard_there=0}
  local pack={{x=2,y=0},{x=2,y=1},{x=3,y=0}}
  local old_life=value.life
  value.life=function() return 4 end
  local c,me,enemies=scenario{melee=true,mana=0,hit=3,life=40,enemies=pack,spot=spot}
  local a=selected(c,me,enemies[1])
  assert(a.name=='melee' and c.neural.stay>0.3,'three weak monsters are clubbed rather than waited out, got '..a.name)
  local potions={{code='hp1',location='belt',grid_x=0},{code='hp1',location='belt',grid_x=1}}
  local with_potions,potion_me,potion_enemies=scenario{melee=true,mana=0,hit=3,life=40,enemies=pack,spot=spot,items=potions}
  charon.frame=function() return 10001 end -- a new frame: what she carries is counted afresh
  selected(with_potions,potion_me,potion_enemies[1])
  assert(with_potions.neural.stay>c.neural.stay,'healing in hand counts toward what staff work may cost, '
    ..tostring(with_potions.neural.stay)..' against '..tostring(c.neural.stay))
  value.life=function() return 100 end
  c,me,enemies=scenario{melee=true,mana=0,hit=3,life=40,enemies=pack,spot=spot}
  a=selected(c,me,enemies[1])
  assert(a.name=='escape' and c.neural.stay==0,'monsters that take many blows are not clubbed, got '..a.name)
  value.life=function() return 4 end
  c,me,enemies=scenario{melee=true,mana=0,hit=12,life=40,enemies=pack,spot=spot}
  a=selected(c,me,enemies[1])
  assert(a.name=='escape','monsters that hit hard are not clubbed either, got '..a.name)
  -- Staff work she can afford only with the healing in her belt begins with a potion.
  local hurt=scenario{melee=true,mana=0,hit=6,life=20,enemies=pack,spot=spot}
  assert(hurt.heal_first==nil,'nothing to drink for: no mark')
  local found
  for _,hit in ipairs{4,5,6,7,8,9,10} do
    local bare,bare_me,bare_enemies=scenario{melee=true,mana=0,hit=hit,life=20,enemies=pack,spot=spot}
    charon.frame=function() return 20000+hit end -- a frame of its own: what she carries is counted afresh
    selected(bare,bare_me,bare_enemies[1])
    local stocked,stocked_me,stocked_enemies=scenario{melee=true,mana=0,hit=hit,life=20,enemies=pack,spot=spot,items=potions}
    charon.frame=function() return 30000+hit end
    selected(stocked,stocked_me,stocked_enemies[1])
    assert(not bare.heal_first,'without potions there is nothing to mark')
    found=found or stocked.heal_first~=nil
  end
  assert(found,'where her life alone will not pay for the blows and her potions will, the policy asks for one first')
  c,me,enemies=scenario{mana=0,hit=3,life=40,enemies=pack,spot=spot}
  a=selected(c,me,enemies[1])
  assert(a.name~='melee' and c.neural.stay==0,'without the leave of the campaign the staff stays down')
  -- Mana is rationed when there is none to drink: the staff takes the weak and lone, the pool the rest.
  local bolt={{name='Charged Bolt',id=38,level=3,mana=4,usable=true,damage=6}}
  local function choice(options)
    local cc,cme,cenemies=scenario(options)
    charon.frame=function() return 50000+(options.frame or 0) end
    return selected(cc,cme,cenemies[1]).name
  end
  value.life=function() return 4 end
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,frame=1,mana=10}=='melee',
    'a lone weak monster beside her is clubbed, with a full pool and nothing to drink')
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,items={{code='mp1',location='belt',grid_x=0}},frame=2,mana=10}=='melee',
    'and with a potion or two in her belt as well: they are for the packs')
  local plenty={}
  for slot=0,3 do plenty[#plenty+1]={code='mp1',location='belt',grid_x=slot} end
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,items=plenty,frame=7}=='Charged Bolt',
    'with plenty to drink there is nothing to ration')
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,frame=13}=='Charged Bolt',
    'nor with most of her pool: it fills by itself')
  do
    local cc,cme,cenemies=scenario{melee=true,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,mana=30}
    charon.frame=function() return 50014 end
    cc.save_mana=true
    assert(selected(cc,cme,cenemies[1]).name=='melee','unless the campaign is saving it for something ahead')
  end
  assert(choice{melee=true,hit=1,life=40,enemies={{x=3.5,y=0}},skills=bolt,frame=8,mana=10}=='melee',
    'the game walks a unit attack in: a swing is asked for from a little beyond contact')
  -- A fanned volley waits for the step closer while most of it would miss.
  value.life=function() return 60 end
  local near={found=true,x=6,y=0,path={{x=6,y=0}},danger_here=0,danger_there=0,missile_there=0,hazard_there=0}
  assert(choice{hit=1,life=40,enemies={{x=9,y=0}},skills=bolt,spot=near,frame=15}=='approach',
    'from the edge of her range, with a safe way closer, she closes before she casts')
  assert(choice{hit=1,life=40,enemies={{x=3,y=0}},skills=bolt,spot=near,frame=16}=='Charged Bolt',
    'and casts from where the volley lands')
  value.life=function() return 4 end
  assert(choice{hit=1,life=40,enemies={{x=9,y=0}},skills=bolt,spot=near,frame=17}=='Charged Bolt',
    'what the bolts that do arrive will kill is shot where it stands')
  -- An elite and its company are for the pool, full or empty.
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0,minion=true}},skills=bolt,frame=9}=='Charged Bolt',
    'a minion is not clubbed')
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0},{x=6,y=0,superunique=true}},skills=bolt,frame=10}=='Charged Bolt',
    'nor is what stands beside an elite')
  assert(choice{melee=true,mana=0,hit=1,life=40,enemies={{x=2,y=0,minion=true}},skills=bolt,frame=11}~='melee',
    'and dry she stands off from them rather than club them')
  assert(choice{melee=true,mana=0,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,frame=12}=='melee',
    'while a common monster is clubbed dry as before')
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0},{x=2,y=1},{x=3,y=0}},skills=bolt,frame=3}=='Charged Bolt',
    'a pack is worth the cast')
  assert(choice{hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,frame=4}=='Charged Bolt',
    'and where the campaign has not allowed the staff she casts')
  value.life=function() return 100 end
  assert(choice{melee=true,hit=1,life=40,enemies={{x=2,y=0}},skills=bolt,frame=5}=='Charged Bolt',
    'what would take many blows is for the pool')
  value.life=function() return 4 end
  assert(choice{melee=true,hit=12,life=40,enemies={{x=2,y=0}},skills=bolt,frame=6}~='melee',
    'and so is what hits hard')
  -- No safe route ends beside a monster: staff work goes straight at it.
  c,me,enemies=scenario{melee=true,mana=0,hit=3,life=40,enemies={{x=6,y=0}}}
  a=selected(c,me,enemies[1])
  assert(a.name=='approach' and math.abs(a.spot.x-3.75)<1e-9 and a.spot.y==0,
    'a weak monster out of reach is walked up to, got '..a.name)
  value.life=old_life
end
for name,fn in pairs(saved) do value[name]=fn end
policy=saved_policy
print('neural combat checks passed')
