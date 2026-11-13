-- Deterministic decisions at the game boundary; movement physics stay native.
local combat = require('lib.combat')
local value = require('lib.value')
local saved_priority = value.priority
value.priority = function(unit) return unit.priority or 1 end
-- Fixture monsters are weaklings unless a case gives them `dps` (life per second in reach).
local saved_dps = value.dps
value.dps = function(unit) return unit.dps or saved_dps(unit) end
local function scenario(ranged, enemies)
  local me = {id=1, x=0,y=0,life=100,max_life=100,mana=20,max_mana=100,
    stamina=20,max_stamina=20,mode=1,level=10,left_skill=0,right_skill=0,alive=true,area=2}
  local calls = {moves={},casts={},selected=0}
  charon = {now=function() return 10000 end, frame=function() return 1 end}
  for i,u in ipairs(enemies) do
    u.id=i; u.type='monster'; u.type_id=1; u.alive=true; u.attackable=true; u.owner=-1
    u.life_percent=100; u.name='target'; u.class_id=1
  end
  game = {me=function() return me end, units=function(kind) return kind=='monster' and enemies or {} end,
    unit=function(_,id) return enemies[id] end, skills=function()
      return ranged and {{name='Charged Bolt',id=38,level=4,mana=10,usable=true,owner=-1}} or {}
    end,
    attack_damage=function() return {min=12,max=12} end,
    skill_damage=function() return {physical_min=0,physical_max=0,elemental_min=20,elemental_max=20} end,
    skill_info=function() return {element=2} end,
    monster_info=function() return {min_life=100,max_life=100,cold_effect=-50,resist={physical=0,lightning=0,cold=0}} end,
    state=function() return false end, items=function() return {} end}
  act = {ready=function() return true end,
    move=function(x,y) calls.moves[#calls.moves+1]={x=x,y=y} end,
    select_skill=function(_,id) calls.skill=id; calls.selected=calls.selected+1; return true end,
    cast=function() calls.casts[#calls.casts+1]='self' end,
    cast_at=function(_,_,stand) calls.casts[#calls.casts+1]=stand and 'ranged' or 'melee' end}
  map = {line_of_sight=function() return true end, line_clear=function() return true end,
    safe_spot=function(options)
      if options.target then
        local dx,dy=options.target.x-me.x,options.target.y-me.y
        local distance=math.sqrt(dx*dx+dy*dy)
        if distance>options.range then
          local x,y=options.target.x-dx/distance*options.range,options.target.y-dy/distance*options.range
          return {found=true,x=x,y=y,path={{x=x,y=y}},danger_here=0,danger_there=0,missile_here=0,open_sectors=8}
        end
      end
      return {found=false,path={},danger_here=0,missile_here=0,open_sectors=8}
    end}
  local build={range=ranged and 7 or 2.8,element='lightning'}
  return combat.new(build),me,calls
end

do
  local c,me=scenario(true,{{x=4,y=0},{x=2,y=0}})
  act.ready=function() return false end
  c:fight(me,20,nil,1)
  assert(c.target_id==1,'a boss already in close range keeps focus; crowding is handled by escape decisions')
end
do
  local c,me=scenario(true,{{x=4,y=0},{x=2,y=0},{x=7,y=0}})
  act.ready=function() return false end
  c.target_id,c.retargeted=3,0
  c:fight(me,20,nil,1)
  assert(c.target_id==1,'a distant retained target must not divert a newly close boss focus to nearby trash')
end

for _,case in ipairs({
  {name='zero mana',expected=3},
  {name='weak retained Fallen',weak=true,expected=3},
  {name='healthy boundary',life=90,expected=3},
  {name='hurt',life=40,expected=3},
  {name='cooldown',recent=true,expected=1},
  {name='explicit focus',focus=3,expected=2},
  {name='invalid explicit focus',focus=99,expected=2},
  {name='non-Fallen blocker',blocker='Zombie',expected=2},
  {name='unique blocker',flag='unique',expected=2},
  {name='champion blocker',flag='champion',expected=2},
  {name='superunique blocker',flag='superunique',expected=2},
  {name='boss blocker',flag='boss',expected=2},
  {name='minion blocker',flag='minion',expected=2},
  {name='dead Shaman',dead=true,expected=2},
  {name='ignored Shaman',ignored=true,expected=2},
  {name='unattackable Shaman',unattackable=true,expected=2},
  {name='obscured Shaman',obscured=true,expected=2},
  {name='failed sight query',sight_error=true,expected=2},
  {name='outside engagement',distance=21,expected=2},
  {name='rejected Shaman',rejected=true,expected=2},
}) do
  local enemies={{x=7,y=0},{x=2,y=0},{x=case.distance or 12,y=0,priority=100}}
  local c,me,calls=scenario(false,enemies)
  me.mana,me.life=0,case.life or 100
  c.melee_ok=true
  enemies[1].name,enemies[2].name,enemies[3].name='Fallen',case.blocker or 'Fallen','Fallen Shaman'
  if case.flag then enemies[2][case.flag]=true end
  c.target_id,c.target_life,c.target_progress=1,100,10000
  c.retargeted=case.recent and 9800 or 0
  if case.weak then
    c.target_id,c.target_life=2,25
    enemies[2].life_percent=25
  end
  if case.dead then enemies[3].alive=false end
  if case.ignored then c.ignored[3]=20000 end
  if case.unattackable then enemies[3].attackable=false end
  if case.obscured or case.sight_error then
    c.sight={[3]={at=10000,seen=true}}
    map.line_of_sight=function(_,_,x)
      if x==enemies[3].x and case.sight_error then error('room changed') end
      return x~=enemies[3].x
    end
  end
  local accept=case.rejected and function(unit) return unit.id~=3 end or nil
  c:fight(me,20,accept,case.focus)
  assert(c.target_id==case.expected,'healthy Shaman priority must preserve protection: '..case.name)
  if case.expected==3 then
    assert(not c.resting and #calls.casts==0 and #calls.moves>0,
      'zero-mana Shaman selection must allow the existing melee approach outside attack range')
  end
end

for _,pair in ipairs({{'Fallen Shaman','Fallen'},{'Carver Shaman','Carver'},
  {'Devilkin Shaman','Devilkin'},{'Dark Shaman','Dark One'}}) do
  local enemies={{x=2,y=0,priority=100},{x=12,y=0}}
  local c,me=scenario(false,enemies)
  enemies[1].name,enemies[2].name=pair[2],pair[1]
  me.mana=0
  c.melee_ok=true
  c.target_id,c.target_life,c.target_progress=1,25,10000
  enemies[1].life_percent=25
  act.ready=function() return false end
  c:fight(me,20)
  assert(c.target_id==2,'Shaman preference must beat the ordinary Fallen score and weak-target retention: '..pair[1])
end

for _,kind in ipairs({'missile','pressure'}) do
  local enemies={{x=2,y=0},{x=12,y=0,priority=100}}
  local c,me,calls=scenario(false,enemies)
  enemies[1].name,enemies[2].name='Fallen','Fallen Shaman'
  c.target_id,c.target_life,c.target_progress=1,100,10000
  c.plan={ [kind]=true, since=10000 }
  local plan=c.plan
  act.ready=function() return false end
  c:fight(me,20)
  assert(c.target_id==2 and c.plan==plan and #calls.moves==0 and #calls.casts==0,
    'Shaman retargeting must preserve animation readiness and active escape ownership: '..kind)
end

for _,weak in ipairs({false,true}) do
  local enemies={{x=weak and 2 or 7,y=0},{x=2,y=0},{x=12,y=0,priority=100},{x=3,y=0}}
  local c,me=scenario(false,enemies)
  enemies[1].name,enemies[2].name,enemies[3].name,enemies[4].name='Fallen','Fallen','Fallen Shaman','Zombie'
  c.target_id,c.target_life,c.target_progress=1,100,10000
  if weak then enemies[1].life_percent=25; c.target_life=25 end
  act.ready=function() return false end
  c:fight(me,20)
  assert(c.target_id==(weak and 1 or 4),'bypassing ordinary Fallen must not hide a different immediate blocker')
end

for _,retained in ipairs({false,true}) do
  for _,elite in ipairs({false,true}) do
    local enemies={{x=2,y=0},{x=12,y=0,priority=100},{x=3,y=0}}
    local c,me=scenario(false,enemies)
    enemies[1].name,enemies[2].name='Fallen','Fallen Shaman'
    enemies[3].name=elite and 'Fallen' or 'Zombie'
    enemies[3].unique=elite
    if retained then c.target_id,c.target_life,c.target_progress=3,100,10000 end
    act.ready=function() return false end
    c:fight(me,20)
    assert(c.target_id==3,'new or close retained targets must still protect against non-ordinary blockers')
  end
end


for _,case in ipairs({
  {bolt=8,nova=1,static=0,off=true},
  {bolt=7,nova=1,static=1,off=false},
  {bolt=5,nova=0,static=0,level=5,off=true},
  {bolt=4,nova=0,static=0,level=5,off=false},
  {bolt=4,nova=1,static=0,level=6,off=false},
  {bolt=4,nova=0,static=1,level=6,off=true},
  {bolt=0,nova=0,static=0,off=false},
  {bolt=2,nova=0,static=0,off=false},
  {bolt=5,nova=0,static=0,fire=1,off=true},
}) do
  local c,me=scenario(true,{})
  c.build=require('lib.builds').sorceress
  me.level,me.skill_points=case.level or 9,12 -- Unspent reset points must not inflate the prefix.
  local skills={
    {id=38,name='Charged Bolt',base_level=case.bolt,level=case.bolt+3,owner=-1},
    {id=44,name='Frost Nova',base_level=case.nova,level=case.nova,owner=-1},
    {id=42,name='Static Field',base_level=case.static,level=case.static,owner=-1},
    {id=36,name='Fire Bolt',base_level=case.fire or 0,level=3,owner=-1},
    {id=0,name='Attack',base_level=20,level=20,owner=-1},
    {id=40,name='Frozen Armor',base_level=1,level=1,owner=123},
    {id=999,name='unknown',base_level=20,level=20,owner=-1},
  }
  game.skills=function() return skills end
  game.skill_info=function(id) if id~=999 then return {class_id=id==0 and -1 or 1} end end
  assert(c:off_build()==case.off,
    'build drift must compare only allocated class points against the current ordered prefix')
end

do
  local c,me=scenario(true,{})
  c.build=require('lib.builds').sorceress
  local bolt={id=38,name='Charged Bolt',base_level=0,level=3,owner=-1}
  local nova={id=44,name='Frost Nova',base_level=0,level=3,owner=-1}
  local static={id=42,name='Static Field',base_level=0,level=3,owner=-1}
  local skills={bolt,nova,static,
    {id=38,name='Charged Bolt',base_level=20,level=20,owner=123},
    {id=44,name='Frost Nova',base_level=20,level=20,owner=123},
    {id=42,name='Static Field',base_level=20,level=20,owner=123}}
  game.skills=function() return skills end
  game.skill_info=function() return {class_id=1} end
  game.class_skills=function() return {
    {id=38,name='Charged Bolt',required_level=1,max_level=20,prerequisites={}},
    {id=44,name='Frost Nova',required_level=6,max_level=20,prerequisites={}},
    {id=42,name='Static Field',required_level=6,max_level=20,prerequisites={}},
  } end
  assert(#c.build.skills==21 and c.build.skills[5]=='Frost Nova' and c.build.skills[6]=='Static Field',
    'the early utility points must move ahead of the fifth bolt without changing total planned ranks')
  local t,learned=10000,{}
  charon.now=function() return t end
  me.stat_points,me.skill_points=0,1
  act.add_skill=function(id)
    learned[#learned+1]=id; me.skill_points=me.skill_points-1
    for _,skill in ipairs({bolt,nova,static}) do
      if skill.id==id then skill.base_level=skill.base_level+1 end
    end
  end
  local function spend()
    t=t+1600; c:spend_points(me)
  end
  do -- stat points: the first ten to Vitality, then strength to its goal, then Vitality again
    local kept_base,kept_add,kept_level,kept_skill=game.base_stat,act.add_stat,me.level,me.skill_points
    local strength,spent=10,{}
    game.base_stat=function(_,_,stat) return stat==0 and strength or 10 end
    act.add_stat=function(stat) spent[#spent+1]=stat; if stat==0 then strength=strength+1 end; me.stat_points=me.stat_points-1 end
    me.skill_points=0
    me.level,me.stat_points=2,5
    for _=1,5 do spend() end
    assert(#spent==5 and spent[1]==3 and spent[5]==3,'the points of level 2 go to Vitality')
    me.level,me.stat_points=3,5; spent={}
    for _=1,5 do spend() end
    assert(#spent==5 and spent[1]==3 and spent[5]==3,'and so do those of level 3')
    me.level,me.stat_points=4,5; spent={}
    for _=1,5 do spend() end
    assert(#spent==5 and spent[1]==0 and spent[5]==0 and strength==15,'those of level 4 bring strength to 15')
    me.level,me.stat_points=5,5; spent={}
    for _=1,5 do spend() end
    assert(spent[1]==3 and spent[5]==3,'and from there it is Vitality again')
    game.base_stat,act.add_stat,me.level,me.skill_points=kept_base,kept_add,kept_level,kept_skill
    me.stat_points=0
  end
  for _,level in ipairs({2,3,4,4}) do -- the fourth allocation is the early Den reward
    me.level,me.skill_points=level,1
    assert(c:next_skill(me).id==38,'opening level and Den points must learn Charged Bolt despite bonus/item ranks')
    spend()
  end
  me.level,me.skill_points=5,1
  spend()
  assert(bolt.base_level==4 and me.skill_points==1 and c:next_skill(me)==nil and not c:off_build(),
    'four learned bolts at five must hold the spare point without reporting build drift')
  bolt.base_level=0; me.skill_points=5 -- a reset has the same ordered hold
  for _=1,5 do spend() end
  assert(bolt.base_level==4 and me.skill_points==1 and c:next_skill(me)==nil,
    'a reset backlog must fill the first four bolts and then wait for level six')
  me.level,me.skill_points=6,2
  assert(c:next_skill(me).id==44,'Frost Nova must be the first level-six allocation')
  spend()
  assert(c:next_skill(me).id==42,'Static Field must follow Frost Nova as the sixth allocated point')
  spend()
  assert(me.skill_points==0 and nova.base_level==1 and static.base_level==1 and not c:off_build(),
    'the saved point and level-six point must learn both utility skills without drift')
  assert(c:next_skill(me).id==38,'further points must return to Charged Bolt')
  me.skill_points=1; spend()
  assert(bolt.base_level==5 and learned[#learned]==38,'the moved fifth bolt must remain in the plan')
end
do
  local c,me=scenario(false,{})
  local builds=require('lib.builds')
  c.build=builds.barbarian
  me.level=6
  local bash={id=126,name='Bash',base_level=3,owner=-1}
  local sword={id=127,name='Sword Mastery',base_level=1,owner=-1}
  local axe={id=128,name='Axe Mastery',base_level=1,owner=-1}
  local howl={id=130,name='Howl',base_level=0,owner=-1}
  local prerequisites={bash.id}
  local howl_level=7
  game.skills=function() return {bash,sword,axe,howl} end
  game.class_skills=function() return {
    {id=126,name='Bash',required_level=1,max_level=20,prerequisites={}},
    {id=127,name='Sword Mastery',required_level=1,max_level=20,prerequisites={}},
    {id=128,name='Axe Mastery',required_level=1,max_level=20,prerequisites={}},
    {id=130,name='Howl',required_level=howl_level,max_level=20,prerequisites={}},
    {id=133,name='Double Swing',required_level=6,max_level=20,prerequisites=prerequisites},
  } end
  assert(c:next_skill(me)==nil,'an unavailable first required skill must block a later learnable barbarian skill')
  howl_level=1
  assert(c:next_skill(me).id==130,'the barbarian must resume its first required skill when available')
  howl.base_level=1; prerequisites={132}
  local class_skills=game.class_skills
  game.class_skills=function()
    local result=class_skills()
    result[#result+1]={id=132,name='Leap',required_level=6,max_level=20,prerequisites={}}
    return result
  end
  assert(c:next_skill(me).id==132,'a missing barbarian prerequisite must be learned before its planned skill')
  game.skills=function() return {bash,sword,axe,howl,{id=132,name='Leap',base_level=1,owner=-1}} end
  assert(c:next_skill(me).id==133,'the planned barbarian skill must resume after learning its prerequisite')
  c.build=builds.default
  assert(c:next_skill(me)==nil,'the default basic-attack build must still allocate no skills')
  c.build={skills={'Missing Metadata','Bash'}}; bash.base_level=0
  assert(c:next_skill(me).id==126,'missing class metadata must keep its existing skip behavior')
end

local function health_scenario(life, maximum, active, projected, items)
  local c,me=scenario(true,{})
  me.life,me.max_life,me.mana,me.class=life,maximum,100,'sorceress'
  local state={active=active,projected=projected,used={},items=items or {
    {id=40,code='hp1',location='belt',grid_x=0},
    {id=41,code='rvs',location='container',page=0}}}
  game.state=function(kind,id,flag)
    assert(kind=='player' and id==me.id and flag==100)
    return state.active
  end
  game.stat=function(kind,id,stat)
    assert(kind=='player' and id==me.id and stat==74)
    return state.projected
  end
  game.items=function() return state.items end
  act.use_belt=function(slot)
    for _,item in ipairs(state.items) do
      if item.location=='belt' and item.grid_x==slot then state.used[#state.used+1]=item; return end
    end
    error('requested an empty belt slot')
  end
  act.use_item=function(item) state.used[#state.used+1]=item end
  return c,me,state
end

for _,case in ipairs({
  {life=160,max=200,code='hp1'}, -- Drink above 70% when the complete dose fits.
  {life=170,max=200,code='hp1'}, -- Exact 30-point fit.
  {life=170.1,max=200},
  {life=60,max=100,active=true,projected=69,code='hp1'}, -- Conservative upper percent leaves 30 free.
  {life=60,max=100,active=true,projected=70},
  {life=60,max=100,active=true,projected=94},
  {life=160,max=200,projected=94,code='hp1'}, -- Inactive projection is stale.
  {life=948,max=1000,active=true,projected=94,code='hp1'}, -- Integer-percent rounding.
  {life=948,max=1000,active=true,projected=93}, -- Inconsistent even allowing rounding.
}) do
  local c,me,state=health_scenario(case.life,case.max,case.active,case.projected)
  c:drink(me)
  assert((state.used[1] and state.used[1].code)==case.code,'health use must fit the unfilled gap after queued healing')
end

for _,projected in ipairs({0,-1,101,math.huge,0/0,'94',false}) do
  local c,me,state=health_scenario(60,100,true,projected)
  c:drink(me)
  assert(#state.used==0,'pending or invalid active projection must withhold regular stacking')
end
do
  local c,me,state=health_scenario(60,100,true,nil)
  c:drink(me)
  assert(#state.used==0,'missing active projection must withhold regular stacking')
  state.active=false
  c:drink(me)
  assert(state.used[1].code=='hp1','effect end must restore current-gap potion fitting')
end
do
  -- Staff work that waits on her life: a potion though it heals more than she lacks.
  local hp1={{id=40,code='hp1',location='belt',grid_x=0}}
  local c,me,state=health_scenario(80,100,false,nil,hp1)
  c:drink(me)
  assert(#state.used==0,'at four fifths of her life a potion of thirty is too much, and kept')
  c.heal_first=charon.now()
  c:drink(me)
  assert(state.used[1] and state.used[1].code=='hp1','unless the fight in hand waits on her life: then she drinks and goes in')
  c,me,state=health_scenario(80,100,true,90,hp1)
  c.heal_first=charon.now()
  c:drink(me)
  assert(#state.used==0,'one potion at a time: with one at work she waits for it')
  c,me,state=health_scenario(80,100,false,nil,hp1)
  c.heal_first=charon.now()-5000
  c:drink(me)
  assert(#state.used==0,'and an old word from the policy no longer counts')
end

for class,coefficient in pairs({sorceress=1,necromancer=1,druid=1,amazon=1.5,paladin=1.5,assassin=1.5,barbarian=2}) do
  local dose=30*coefficient
  local c,me,state=health_scenario(1000-dose,1000,false)
  me.class=class
  c:drink(me)
  assert(state.used[1].code=='hp1','class-adjusted normal dose must fit exactly: '..class)
  c,me,state=health_scenario(1000-dose+0.1,1000,false)
  me.class=class
  c:drink(me)
  assert(#state.used==0,'class-adjusted dose must not exceed the available gap: '..class)
end

for _,case in ipairs({
  {life=250,max=400,items={{id=1,code='hp1',location='belt',grid_x=0},
    {id=2,code='hp3',location='container',page=0},{id=3,code='hp2',location='belt',grid_x=1}},pick=2},
  {life=160,max=200,items={{id=1,code='hp5',location='belt',grid_x=0},
    {id=2,code='hp1',location='container',page=0}},pick=2},
  {life=160,max=200,items={{id=1,code='hp1',location='container',page=0},
    {id=2,code='hp1',location='belt',grid_x=3},{id=3,code='hp1',location='belt',grid_x=0}},pick=3},
  {life=160,max=200,items={{id=5,code='hp1',location='container',page=0},
    {id=2,code='hp1',location='container',page=0}},pick=2},
  {life=100,max=200,items={{id=1,code='hp5',location='belt',grid_x=0},
    {id=2,code='hp3',location='container',page=0}},pick=2}, -- Floor still prefers a full fitting dose.
  {life=20,max=40,items={{id=1,code='hp1',location='belt',grid_x=0},
    {id=2,code='hp5',location='container',page=0}},pick=2}, -- Oversized emergency picks strongest.
  {life=29,max=100,items={{id=1,code='hp1',location='belt',grid_x=0},
    {id=2,code='rvs',location='belt',grid_x=1},{id=3,code='rvl',location='container',page=0}},pick=2},
  {life=50,max=100,items={{id=1,code='rvl',location='container',page=0}},pick=1},
  {life=125,max=200,items={{id=1,code='rvs',location='container',page=0}},pick=1},
  {life=131,max=200,items={{id=1,code='rvs',location='container',page=0}}},
  {life=60,max=100,items={{id=1,code='rvl',location='belt',grid_x=0}}},
  {life=60,max=100,items={{id=1,code='hp1',location='container',page=4},
    {id=2,code='hp1',location='equipped'}}},
  {life=60,max=100,items={{id=1,code='hpX',location='belt',grid_x=0}}},
  {life=50,max=100,items={{id=1,code='hpX',location='belt',grid_x=0}},pick=1},
  {life=60,max=100,class='unknown',pick=41}, -- Class-independent rejuvenation still fits.
  {life=50,max=100,class='unknown',pick=40},
  {life=50,max=100,class='unknown',items={{id=1,code='rvs',location='container',page=0},
    {id=2,code='hp1',location='container',page=0}},pick=2},
}) do
  local c,me,state=health_scenario(case.life,case.max,true,0,case.items)
  state.active=false
  if case.class then me.class=case.class end
  if case.life/case.max<=0.5 then
    state.active=true
    game.stat=function() error('emergency floor must use current HP without projection') end
  end
  c:drink(me)
  assert((state.used[1] and state.used[1].id)==case.pick,
    'select the largest fitting carried dose, preserving emergency fallback and ties: life=' .. case.life
    .. ' class=' .. tostring(case.class) .. ' expected=' .. tostring(case.pick) .. ' got=' .. tostring(state.used[1] and state.used[1].id))
end

do
  local c,me,state=health_scenario(20,40,true,0)
  c:drink(me)
  assert(state.used[1].code=='hp1','exactly half life must allow an oversized regular dose')
  charon.now=function() return 11200 end
  c:drink(me)
  assert(#state.used==1,'preserve the strict 1200 ms health request gap')
  charon.now=function() return 11201 end
  c:drink(me)
  assert(#state.used==2,'retry health use after the existing gap')
end

do
  local c,me,state=health_scenario(60,100,true,94)
  me.in_town=true
  game.state=function() error('town healing must not inspect potion effects') end
  c:drink(me)
  assert(#state.used==0,'town healing must not request potions')
end

do
  local c,me,state=health_scenario(60,100,true,94)
  local saved_print,logs=print,{}
  print=function(message) logs[#logs+1]=message end
  c:drink(me)
  charon.now=function() return 12000 end
  c:drink(me)
  me.life=95; c:drink(me)
  me.life=60; c:drink(me)
  assert(#logs==1,'log queued-healing waits once while the same recovery remains active')
  state.active=false; me.life=100; c:drink(me)
  state.active=true; me.life=60; c:drink(me)
  assert(#logs==2,'a newly observed recovery gets one waiting trace')
  print=saved_print
end

for _,location in ipairs({'belt','container'}) do
  local c,me=scenario(true,{})
  me.mana=0
  local active,t,used=true,10000,{}
  charon.now=function() return t end
  game.state=function(kind,id,state)
    assert(kind=='player' and id==me.id and state==106,'read the client mana-potion state')
    return active
  end
  local items={{id=40,code='mp1',location=location,page=0,grid_x=0}}
  game.items=function() return items end
  act.use_belt=function(slot) used[#used+1]=items[slot+1].code end
  act.use_item=function(item) used[#used+1]=item.code end
  c:drink(me)
  assert(#used==0 and c.last_mana==0,'active mana regeneration must preserve regular mana potions')
  t=12000
  c:drink(me)
  assert(#used==0,'the retry gap must not consume another potion while mana regeneration continues')
  items[#items+1]={id=41,code='rvs',location=location,page=0,grid_x=1}
  c:drink(me)
  assert(#used==1 and used[1]=='rvs' and c.last_mana==t,'instant rejuvenation remains available during mana regeneration')
  active=false
  t=13000
  c:drink(me)
  assert(#used==1,'preserve the existing mana-potion retry gap')
  t=14000
  c:drink(me)
  assert(#used==2 and used[2]=='mp1','expired mana regeneration permits another regular mana potion')
  c.resting=true
  items={}
  active=true
  assert(not c:out_of_mana(),'active mana regeneration must prevent restocking after the last potion')
  active=false
  assert(c:out_of_mana(),'an exhausted reserve after mana regeneration expires must still request restocking')
end

do -- a boss in sight is fought on a full pool: mana potions are drunk ahead, rejuvenations kept
  local c,me=scenario(true,{})
  me.max_mana,me.mana=100,60
  local active,t,used=false,20000,{}
  charon.now=function() return t end
  game.state=function() return active end
  local items={{id=40,code='mp1',location='belt',page=0,grid_x=0},{id=41,code='rvs',location='belt',page=0,grid_x=1}}
  game.items=function() return items end
  act.use_belt=function(slot) used[#used+1]=items[slot+1].code end
  act.use_item=function(item) used[#used+1]=item.code end
  c:drink(me)
  assert(#used==0,'at 60% mana with nothing special ahead no potion is drunk')
  c.boss_at=t
  c:drink(me)
  assert(#used==1 and used[1]=='mp1','with a boss in sight the pool is topped up from the belt')
  active=true; t=t+2000; c.boss_at=t
  c:drink(me)
  assert(#used==1,'a rejuvenation is not spent on a pool that is only less than full')
  active=false; me.mana=90; t=t+2000; c.boss_at=t
  c:drink(me)
  assert(#used==1,'a nearly full pool needs nothing')
  me.mana=60; t=t+2000
  c:drink(me)
  assert(#used==1,'once she is out of sight the usual threshold applies again')
end

do
  local enemies={{x=7,y=0,priority=100},{x=6,y=0}}
  local c,me=scenario(true,enemies)
  act.ready=function() return false end
  c.target_id,c.target_life,c.target_progress=1,24,10000
  enemies[1].life_percent=24
  c.retargeted=9800
  c:fight(me,20,nil,2)
  assert(c.target_id==1,'quest focus must retain the existing retarget cooldown')
  charon.now=function() return 10400 end
  c:fight(me,20,nil,2)
  assert(c.target_id==2,'quest focus must replace even a weak higher-value generic target after the cooldown')
end
for _,reason in ipairs({'missing','dead','ignored','obscured','distant','rejected'}) do
  local enemies={{x=7,y=0,priority=100},{x=6,y=0}}
  local c,me=scenario(true,enemies)
  act.ready=function() return false end
  local focus_id,accept=2,nil
  if reason=='missing' then focus_id=99 end
  if reason=='dead' then enemies[2].alive=false end
  if reason=='ignored' then c.ignored[2]=20000 end
  if reason=='obscured' then
    c.sight={[2]={at=10000,seen=true}}
    map.line_of_sight=function(_,_,x) return x~=6 end
  end
  if reason=='distant' then enemies[2].x=21 end
  if reason=='rejected' then accept=function(u) return u.id~=2 end end
  c:fight(me,20,accept,focus_id)
  assert(c.target_id==1,'invalid quest focus must fall back to generic selection: '..reason)
end
do
  local c,me=scenario(true,{{x=7,y=0,priority=100},{x=6,y=0},{x=2,y=0}})
  act.ready=function() return false end
  c.target_id,c.target_life,c.target_progress=1,100,10000
  c:fight(me,20,nil,2)
  assert(c.target_id==3,'an immediate blocker must still interrupt approach to a focused boss')
end

do
  local enemies={{x=8,y=0},{x=2,y=0}}
  local c,me=scenario(true,enemies)
  act.ready=function() return false end
  c.target_id,c.target_life,c.target_progress=1,100,10000
  local t=10000
  charon.now=function() return t end
  for i=1,3 do
    t=10000+(i-1)*400
    c:fight(me,20,nil,1)
    assert(c.target_id==2,'an unchanged immediate blocker must not alternate with quest focus')
  end
  t=11200; enemies[2].x=4.5
  c:fight(me,20,nil,1)
  assert(c.target_id==2,'retain a blocker through the release boundary after acquiring within 3.5')
  t=11600; enemies[2].x=4.51
  c:fight(me,20,nil,1)
  assert(c.target_id==1,'quest focus must resume once the blocker leaves personal pressure')
end

for _,reason in ipairs({'dead','ignored','obscured','rejected'}) do
  local enemies={{x=8,y=0},{x=2,y=0}}
  local c,me=scenario(true,enemies)
  act.ready=function() return false end
  c.target_id,c.target_life,c.target_progress=2,100,10000
  local accept
  if reason=='dead' then enemies[2].alive=false end
  if reason=='ignored' then c.ignored[2]=20000 end
  if reason=='obscured' then map.line_of_sight=function(_,_,x) return x~=2 end end
  if reason=='rejected' then accept=function(u) return u.id~=2 end end
  c:fight(me,20,accept,1)
  assert(c.target_id==1,'an invalid blocker must release quest focus: '..reason)
end

for _,rejected in ipairs({false,true}) do
  local c,me=scenario(true,{{x=8,y=0},{x=2,y=0}})
  act.ready=function() return false end
  local accept=rejected and function(u) return u.id~=2 end or nil
  c:fight(me,20,accept,1)
  assert(c.target_id==(rejected and 1 or 2),'initial focus acquisition must respect accepted immediate blockers')
end

for _,pressure in ipairs({false,true}) do
  local c,me=scenario(true,{{x=8,y=0},{x=2,y=0}})
  act.ready=function() return false end
  c.target_id,c.target_life,c.target_progress=1,100,9000
  c.misses=3
  local plan={x=-4,y=0,since=10000,from=2,pressure=pressure,path={{x=-4,y=0}}}
  c.plan=plan
  c:fight(me,20,nil,1)
  assert(c.target_id==2 and (c.plan==plan)==pressure,
    'target changes must preserve pressure retreat ownership and discard voluntary flanks')
  assert(c.misses==0 and c.target_progress==10000,
    'retained escape ownership must not retain the previous target damage history')
end
do
  local c,me,calls=scenario(true,{{x=6,y=0},{x=2,y=0},{x=-2,y=0},{x=0,y=2}})
  me.life=40
  map.safe_spot=function()
    return {found=true,x=0,y=-8,danger_here=3,danger_there=1,open_sectors=2,
      path={{x=0,y=-2},{x=0,y=-4},{x=0,y=-6},{x=0,y=-8}}}
  end
  local value=require 'lib.value'
  local dps=value.dps
  value.dps=function() return 8 end -- each would take a fifth of our remaining life per second
  c:fight(me,20,nil,1)
  value.dps=dps
  assert(c.target_id==2 and #calls.moves==1 and #calls.casts==0,
    'focused boss combat must clear immediate pressure and evade a dangerous close surround')
end

do
  local c,me,calls=scenario(true,{{x=6,y=0,priority=100},{x=3,y=0}})
  c.target_id,c.target_life,c.target_progress=1,100,10000
  c:fight(me,12,function(u) return u.id==2 end)
  assert(c.target_id==2 and #calls.casts==1,'travel filter must release a retained, skipped target')
end
do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  c.target_id,c.target_life,c.target_progress=1,100,10000
  c.resting=true
  assert(c:fight(me,12,function() return false end)=='clear')
  assert(not c.target_id and not c.resting and #calls.moves==0 and #calls.casts==0,
    'skipping the last target must release mana waiting without acting')
end

do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  c:fight(me,20)
  assert(calls.selected==1 and calls.casts[1]=='ranged', 'select then cast without an idle tick')
end

do
  local c,me,calls=scenario(false,{{x=2,y=0}})
  c:fight(me,20)
  assert(calls.casts[1]=='melee' and #calls.moves==0,'single melee target should be attacked aggressively')
end
do
  local c,me,calls=scenario(false,{{x=5,y=0},{x=-5,y=0},{x=0,y=5}})
  map.safe_spot=function()
    return {found=true,x=0,y=-3,danger_here=2,danger_there=1,open_sectors=2,
      path={{x=0,y=-1},{x=0,y=-2},{x=0,y=-3}}}
  end
  c:fight(me,20)
  assert(calls.moves[1].y<0,'melee should flank before three approaching enemies close the exits')
end
do
  local c,me,calls=scenario(true,{{x=2,y=0}})
  map.line_of_sight=function() return false end
  assert(not c:threat(me,20) and #c:hostiles(me,20)==0,'a nearby enemy behind a wall must not cause combat or evasion')
end
do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  c.plan={x=2,y=2,since=10000,from=3,why='test',
    path={{x=0,y=1},{x=0,y=2},{x=1,y=2},{x=2,y=2}}}
  assert(c:follow_plan(me))
  assert(calls.moves[1].x==0 and calls.moves[1].y==2,'must preserve tactical route corners')
end
do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  c.plan={x=3,y=1,since=10000,from=3,why='test',
    path={{x=0,y=1},{x=1,y=1},{x=2,y=1},{x=3,y=1}}}
  c:follow_plan(me)
  assert(calls.moves[1].x==0 and calls.moves[1].y==1,'do not skip an unreached nearby corner')
end
do
  local c,me,calls=scenario(false,{{x=10,y=0,priority=100},{x=2,y=0}})
  c.target_id,c.target_life,c.target_progress=1,100,10000
  c:fight(me,20)
  assert(c.target_id==2 and calls.casts[1]=='melee','fight the enemy blocking approach to a distant priority target')
end
do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  c.resting=true
  c:fight(me,20)
  assert(not c.resting and #calls.casts==1,'resume as soon as an effective spell is affordable')
end
do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  c.ignored[1]=20000
  assert(not c:threat(me,20) and #c:hostiles(me,20)==1,'ignored enemies remain physical threats')
end

do
  local c,me,calls=scenario(true,{{x=6,y=0}})
  me.x,me.y=0.5,0.5
  c.plan={x=-1,y=0,since=10000,path={{x=-1,y=0}},from=1,why='emergency gap'}
  c:follow_plan(me)
  assert(#calls.moves==1 and calls.moves[1].x==-1,'an adjacent cell must not be considered reached from a fractional start')
  me.x,me.y=-0.5,0.5 -- center of destination cell (-1, 0)
  map.safe_spot=function() return {danger_here=0} end
  charon.now=function() return 10200 end
  assert(not c:follow_plan(me) and not c.plan and #calls.moves==1,'cell centers must count as arrival without a duplicate movement request')
  me.x,me.y=0.5,1.5
  c.plan={x=0,y=3,since=10200,path={{x=0,y=1},{x=0,y=2},{x=0,y=3}},from=1,why='test'}
  c:follow_plan(me)
  assert(calls.moves[#calls.moves].y==3,'fractional arrival at an intermediate waypoint must advance the route')
end

do
  local enemies={{x=8,y=0,ranged=true}}
  local c,me,calls=scenario(true,enemies)
  local t=10000
  charon.now=function() return t end
  local missiles={{id=40,class_id=1,owner_type=1,owner=1,x=6,y=0},
    {id=41,class_id=1,owner_type=0,owner=1,x=6,y=0},
    {id=42,class_id=1,x=6,y=0}}
  charon.frame=function() return math.floor(t/40) end
  game.units=function(kind) return kind=='missile' and missiles or enemies end
  c:observe_missiles(me)
  assert(#c.missile_lanes==0,'one observation cannot establish incoming flight')
  t=10040; missiles[1].x=5
  c:observe_missiles(me)
  assert(#c.missile_lanes==1 and c.missile_lanes[1].to_x<0,'project only observed hostile flight, never own or unowned missiles')
  t=10041; c:observe_missiles(me)
  assert(#c.missile_lanes==1,'a second call within the same game frame must preserve measured flight')
  local queries=0
  map.safe_spot=function(o)
    queries=queries+1
    assert(o.missiles and o.radius==8 and o.occupied==1.5,'NN sidesteps use native searches with monster clearance')
    return {found=true,x=0,y=2,path={{x=0,y=1},{x=0,y=2}},danger_here=2,danger_there=0,
      missile_here=2,missile_there=0}
  end
  assert(c:dodge(me) and #calls.moves==1 and #calls.casts==0,'incoming lane must yield to a sidestep')
  t=10080; missiles[1].x=4
  assert(c:dodge(me) and queries==1 and #calls.moves==1,'active dodge must retain ownership between requests')
  c:fight(me,12)
  assert(c.plan and c.plan.missile and #calls.casts==0,'acquiring a target must not cancel an active missile dodge')
  c:fight(me,12,nil,1)
  assert(c.plan and c.plan.missile and #calls.casts==0,'quest focus must preserve an active missile dodge')
  t=10200; me.y=2; missiles[1].x=1
  map.safe_spot=function() return {missile_here=0} end
  assert(not c:dodge(me) and not c.plan,'once clear of the lane, resume normal work')
  t=10440; missiles[1].x=0
  c:observe_missiles(me)
  assert(#c.missile_lanes==0,'stale samples cannot invent a velocity')
  t=10480; c:observe_missiles(me)
  assert(#c.missile_lanes==0,'stationary effects do not cause sidesteps')
  t=10520; missiles[1].x=-1; map.line_of_sight=function() return false end
  c:observe_missiles(me)
  assert(#c.missile_lanes==0,'missiles behind walls are not immediate threats')
  t=10560; missiles[1].x=-2; me.area=3; map.line_of_sight=function() return true end
  c:observe_missiles(me)
  assert(#c.missile_lanes==0,'area transitions reset missile tracking')
end
do
  local c,me,calls=scenario(true,{{x=18,y=0}})
  c.target_id=1
  c.observe_missiles=function(self) self.missile_lanes={{x=4,y=0,to_x=-4,to_y=0}} end
  map.safe_spot=function(options)
    assert(not options.target,'global missile avoidance must not leash toward a retained combat target')
    return {found=true,x=0,y=2,path={{x=0,y=1},{x=0,y=2}},danger_here=2,danger_there=0,
      missile_here=2,missile_there=0}
  end
  assert(c:dodge(me) and #calls.moves==1,'unfocused global avoidance must still execute a safe sidestep')
end

do
  local enemies={{x=8,y=0,ranged=true}}
  local c,me,calls=scenario(true,enemies)
  local t=10000
  local missiles={{id=40,class_id=1,owner_type=1,owner=1,x=6,y=0},
    {id=41,class_id=1,owner_type=1,owner=1,x=9,y=0}}
  charon.now=function() return t end
  charon.frame=function() return t end
  game.units=function(kind) return kind=='missile' and missiles or enemies end
  map.safe_spot=function()
    return {found=true,x=0,y=2,path={{x=0,y=1},{x=0,y=2}},danger_here=2,danger_there=0,
      missile_here=2,missile_there=0}
  end
  c:observe_missiles(me)
  t=10040; missiles[1].x=5
  assert(c:dodge(me) and #calls.moves==1)
  local plan=c.plan
  t=10165; missiles[1].x=4
  assert(c:dodge(me) and c.plan==plan and #calls.moves==1)
  t=10194 -- A repeated position at plan age 154 ms, before the next 160 ms move request.
  assert(c:dodge(me) and c.plan==plan and #c.missile_lanes==1 and #calls.moves==1,
    'one unchanged missile snapshot must not release a known-flight dodge before its next movement step')
  assert(c.missile_samples[40].at==10165 and c.missile_samples[40].seen==10194,
    'unchanged flight must keep the position baseline separate from last observation time')
  t=10200
  assert(c:dodge(me) and c.plan==plan and #calls.moves==2,'known flight must retain ownership through the next movement request')
  t=10245; missiles[1].x=3
  c:observe_missiles(me)
  assert(#c.missile_lanes==1 and math.abs(c.missile_lanes[1].to_x-(-4.5))<0.00001,
    'new movement must use elapsed time since the changed-position baseline, without a post-zero velocity spike')
  t=10445
  c:observe_missiles(me)
  assert(#c.missile_lanes==1,'known motion remains valid at the 200 ms boundary')
  t=10446
  assert(not c:dodge(me) and not c.plan and #c.missile_lanes==0,
    'unchanged known flight must expire after 200 ms, while never-moved effects never initiate lanes')
end

for _,guard in ipairs({'owner_type','owner','class','area','wall','absent','stale'}) do
  local enemies={{x=8,y=0,ranged=true},{x=9,y=0}}
  local c,me=scenario(true,enemies)
  local t=10000
  local missile={id=40,class_id=1,owner_type=1,owner=1,x=6,y=0}
  local missiles={missile}
  enemies[2].owner=1 -- A friendly monster cannot establish hostile flight.
  charon.now=function() return t end
  charon.frame=function() return t end
  game.units=function(kind) return kind=='missile' and missiles or enemies end
  c:observe_missiles(me)
  t=10040; missile.x=5
  c:observe_missiles(me)
  assert(#c.missile_lanes==1)
  t=10080
  if guard=='owner_type' then missile.owner_type=0 end
  if guard=='owner' then missile.owner=2 end
  if guard=='class' then missile.class_id=2 end
  if guard=='area' then me.area=3 end
  if guard=='wall' then map.line_of_sight=function() return false end end
  if guard=='absent' then
    missiles={}; c:observe_missiles(me); t=10120; missiles={missile}
  end
  if guard=='stale' then t=10241; missile.x=4 end
  c:observe_missiles(me)
  assert(#c.missile_lanes==0,'held missile flight must respect current identity, visibility and freshness: '..guard)
end

-- Loot approaches prove the whole short corridor safe without taking actions.
for _,case in ipairs({
  {name='distant melee',enemy={x=5,y=12},safe=true},
  {name='player pressure',enemy={x=-6,y=0}},
  {name='loot pressure',enemy={x=14,y=0}},
  {name='middle pressure',enemy={x=4,y=6.5}},
  {name='ignored monster',enemy={x=4,y=6},ignored=true},
  {name='immune monster',enemy={x=4,y=6},immune=true},
  {name='untargetable monster',enemy={x=4,y=6},untargetable=true},
  {name='ranged exposure',enemy={x=4,y=20,ranged=true}},
  {name='behind wall',enemy={x=4,y=6},wall=true,safe=true},
  {name='item exposes monster',enemy={x=8,y=6},item_visible=true},
  {name='middle exposes monster',enemy={x=4,y=6},middle_visible=true},
  {name='offset doorway exposes monster',enemy={x=6,y=6},doorway=true},
  {name='offset doorway exposes missile',sample=true,doorway=true},
  {name='blocked corridor',blocked=true},
  {name='closed door',door=true},
  {name='pressure escape',pressure=true},
  {name='missile escape',escape=true},
  {name='pressure escape timeout boundary',pressure=true,since=9400},
  {name='expired pressure escape',pressure=true,since=9399,safe=true},
  {name='expired missile escape',escape=true,since=9399,safe=true},
  {name='observed missile',missile=true},
  {name='first hostile missile sighting',sample=true},
  {name='low health',hurt=true},
  {name='long diversion',far=true},
  {name='failed visibility query',los_error=true},
}) do
  local enemies=case.enemy and {case.enemy} or {}
  local c,me,calls=scenario(true,enemies)
  local item={x=case.far and 13 or case.doorway and 12 or 8,y=0}
  local objects=case.door and {{door=true,mode=0,x=7,y=3}} or {}
  local missiles=case.sample and {{id=40,class_id=1,owner_type=1,owner=1,x=5,y=0}} or {}
  if case.sample then
    enemies[1]={id=1,type='monster',alive=true,attackable=true,owner=-1,x=30,y=0}
    if case.doorway then missiles[1].x,missiles[1].y=6,6 end
  end
  game.units=function(kind)
    return kind=='object' and objects or kind=='missile' and missiles or enemies
  end
  if case.ignored then c.ignored[1]=20000 end
  if case.immune then value.priority=function() return -1 end end
  if case.untargetable then enemies[1].attackable=false end
  if case.blocked then map.line_clear=function() return false end end
  if case.wall or case.item_visible or case.middle_visible then
    map.line_of_sight=function(x,_,to_x,to_y)
      if to_x==item.x and to_y==item.y then return true end
      return case.item_visible and x==8 or case.middle_visible and x==4 or false
    end
  end
  if case.los_error then map.line_of_sight=function() error('room unavailable') end end
  if case.doorway then
    map.line_of_sight=function(x,y,to_x,to_y)
      if y==to_y then return true end
      local crossing=x+(to_x-x)*(3-y)/(to_y-y)
      return math.abs(crossing-5)<0.1 -- wall at y=3 with an offset opening at x=5
    end
  end
  if case.pressure then c.plan={pressure=true,since=case.since} end
  if case.escape then c.plan={missile=true,since=case.since} end
  if case.missile then
    c.observe_missiles=function(self) self.missile_lanes={{x=4,y=0,to_x=-4,to_y=0}} end
  end
  if case.hurt then me.life=40 end
  local plan=c.plan
  assert(c:loot_safe(me,item)==(case.safe or false),'loot safety: '..case.name)
  assert(#calls.moves==0 and #calls.casts==0 and calls.selected==0 and c.plan==plan,
    'loot safety must leave combat action ownership unchanged: '..case.name)
  value.priority=function(unit) return unit.priority or 1 end
end

-- Ground effects need no measured velocity, and killing their owner does not
-- extinguish them. Friendly sources and cosmetic missiles remain excluded.
for _,case in ipairs({
  {name='environment',owner_type=-1,expected=true},
  {name='object source',owner_type=2,expected=true},
  {name='unloaded monster',owner_type=1,owner=99,expected=true},
  {name='hostile',owner_type=1,owner=1,expected=true},
  {name='dead hostile',owner_type=1,owner=1,dead=true,expected=true},
  {name='untargetable hostile',owner_type=1,owner=1,untargetable=true,expected=true},
  {name='player',owner_type=0,owner=1},
  {name='pet',owner_type=1,owner=1,friendly=true},
  {name='hireling',owner_type=1,owner=1,mercenary=true},
  {name='cosmetic',owner_type=-1,cosmetic=true},
}) do
  local enemies={{x=10,y=0}}
  local c,me=scenario(true,enemies)
  local t=10000
  charon.frame=function() return t end
  charon.now=function() return t end
  enemies[1].alive=not case.dead
  enemies[1].attackable=not case.untargetable
  enemies[1].owner=case.friendly and 1 or -1
  enemies[1].mercenary=case.mercenary
  local patch={id=40,class_id=331,x=0,y=0,ground_fire=not case.cosmetic,
    ground_size=1,owner_type=case.owner_type,owner=case.owner or -1}
  local missiles={patch}
  game.units=function(kind) return kind=='missile' and missiles or enemies end
  c:observe_missiles(me)
  assert(#c.ground_hazards==(case.expected and 1 or 0),'first ground fire observation: '..case.name)
  t=t+4000; c:observe_missiles(me)
  assert(#c.ground_hazards==(case.expected and 1 or 0),'stationary ground fire persists: '..case.name)
  missiles={}; t=t+40; c:observe_missiles(me)
  assert(#c.ground_hazards==0,'disappeared ground effects must be discarded')
  missiles={patch}; me.area=3; c:observe_missiles(me)
  assert(#c.ground_hazards==(case.expected and 1 or 0),'area changes must refresh even within one frame')
  patch.ground_fire=false; t=t+40; c:observe_missiles(me)
  assert(#c.ground_hazards==0,'a reused unit ID must use current native classification')
end

do
  local c,me=scenario(true,{})
  local missiles={}
  for i=1,160 do missiles[i]={x=10,y=0,ground_fire=true,ground_size=1,owner_type=-1} end
  missiles[161]={x=0,y=0,ground_fire=true,ground_size=2,owner_type=-1}
  game.units=function(kind) return kind=='missile' and missiles or {} end
  c:observe_missiles(me)
  assert(#c.ground_hazards==128 and c.ground_hazards[1].x==0,
    'bounded hazard lists must retain the closest patches even when enumerated last')
end

for _,kind in ipairs({'escape','busy','refused','trapped'}) do
  local c,me,calls=scenario(true,{{x=6,y=0,unique=true}})
  local t=10000
  charon.frame=function() return t end
  charon.now=function() return t end
  game.units=function(type)
    return type=='missile' and {{id=40,x=0,y=0,ground_fire=true,ground_size=1,owner_type=-1}} or {}
  end
  game.skills=function() return {{name='Frost Nova',id=44,level=1,mana=9,usable=true}} end
  map.hazard_clear=function(x,y,tx,ty,hazards)
    assert(#hazards==1 and hazards[1].size==1)
    return x>=3 and tx>=3
  end
  map.safe_spot=function(options)
    assert(#options.hazards==1 and not options.target,'fire escape must have no target leash')
    -- Escaping fire can cost more monster danger; clear floor still takes priority.
    return {found=kind~='trapped',x=3,y=0,path=kind=='trapped' and {} or {{x=1,y=0},{x=2,y=0},{x=3,y=0}},
      danger_here=100,danger_there=110,hazard_here=100,hazard_there=0,missile_here=0,missile_there=0}
  end
  if kind=='busy' then act.ready=function() return false end end
  if kind=='refused' then act.move=function() return false end end
  assert(c:dodge(me),'floor fire owns the tick even without nearby monsters or moving missiles: '..kind)
  assert(#calls.casts==0 and calls.selected==0,'fire escape must not start another spell animation')
  if kind=='escape' then assert(#calls.moves==1 and c.plan.ground,'clear floor must be reached despite higher monster score') end
  if kind=='refused' then
    t=t+40
    assert(c:dodge(me) and c.escape_retry_until>t,'movement refusal must not authorize a cast in fire')
  end
  me.x=3; t=t+500
  assert(not c:dodge(me),'travel/combat resumes when clear of ground fire: '..kind)
end

do
  local c,me,calls=scenario(true,{})
  c.ground_hazards={{x=2,y=0,size=1}}
  c.plan={x=4,y=0,path={{x=1,y=0},{x=2,y=0},{x=3,y=0},{x=4,y=0}},since=10000,why='old escape'}
  map.hazard_clear=function(x,y,tx,ty) return x==tx and y==ty end
  assert(not c:follow_plan(me) and not c.plan and #calls.moves==0,
    'fresh ground fire must invalidate a previously safe tactical segment')
  c.observe_missiles=function() c.missile_lanes={} end
  game.units=function() return {} end
  assert(not c:loot_safe(me,{x=8,y=0}),'simple loot corridors must exclude known ground fire')
  map.hazard_clear=function() return true end
  assert(c:loot_safe(me,{x=8,y=0}),'ground effects away from the corridor must not forbid looting')
end

do
  local c,me,calls=scenario(true,{})
  me.x,me.y=2.5,0.5
  c.ground_hazards={{x=0.5,y=0.5,size=1}}
  c.plan={x=2,y=1,path={{x=2,y=1}},since=10000,why='safe beside fire'}
  map.hazard_clear=function(x,y,tx,ty)
    assert(x==2.5 and y==0.5,'start must use the native cell center')
    assert(tx==2.5 and (ty==0.5 or ty==1.5),'integer waypoints must name native cell centers')
    return true
  end
  assert(c:follow_plan(me) and #calls.moves==1 and calls.moves[1].x==2 and calls.moves[1].y==1,
    'a safe native route beside a patch must not be rejected for touching its raw integer boundary')
end

for _,blocked in ipairs({false,true}) do
  local enemies={{x=14,y=0}}
  local c,me,calls=scenario(true,enemies)
  game.units=function(kind)
    return kind=='missile' and {{id=40,x=5.5,y=0.5,ground_fire=true,ground_size=1,owner_type=-1}} or enemies
  end
  map.hazard_clear=function(x,y,tx,ty) return tx<4 end
  map.safe_spot=function(options)
    assert(options.hazards[1].x==5.5,'both tactical searches must retain floor hazards')
    if options.target then assert(options.target.x==14,'approach must retain its target') end
    return {found=not blocked,x=3,y=1,path=blocked and {} or {{x=1,y=1},{x=2,y=1},{x=3,y=1}},
      danger_here=0,danger_there=0.5,hazard_here=0,hazard_there=0,missile_here=0,missile_there=0}
  end
  assert(c:fight(me,20)=='fighting')
  for _,point in ipairs(calls.moves) do assert(point.x<4,'ordinary approach must never step back into known fire') end
  assert(#calls.moves==(blocked and 0 or 1) and #calls.casts==0,
    'a blocked approach must use the safe native detour, or hold if none is reachable')
end

do
  local c,me,calls=scenario(false,{{x=2,y=0}})
  c.build=require('lib.builds').barbarian
  game.skills=function() return {{name='Bash',id=126,level=1,mana=2,usable=true,owner=-1}} end
  game.skill_info=function() return {element=0} end
  game.skill_damage=function() return {physical_min=4,physical_max=4,elemental_min=0,elemental_max=0} end
  c:fight(me,12)
  assert(calls.skill==126 and calls.casts[1]=='melee','Bash must use client flat skill damage plus the weapon estimate through the public NN fight path')
end

local function escape_scenario()
  local c,me,calls=scenario(true,{{x=2,y=0,dps=12},{x=2,y=1,dps=12},{x=3,y=0,dps=12}})
  local t=10000
  charon.now=function() return t end
  charon.frame=function() return t end
  map.safe_spot=function()
    return {found=true,x=-4,y=0,path={{x=-4,y=0}},danger_here=7,danger_there=0,
      missile_here=0,open_sectors=2,hazard_there=0}
  end
  return c,me,calls,function(ms) t=ms end
end

do
  local c,me,calls,time=escape_scenario()
  c:fight(me,12)
  for _,t in ipairs{10160,10320,10480} do time(t); c:fight(me,12) end
  assert(#calls.moves==4 and #calls.casts==0 and c.escape_progress.at==10000,
    'repeated accepted NN movement must retain its original stationary progress budget')
  time(10640); act.ready=function() return false end; c:fight(me,12)
  assert(#calls.moves==4 and c.escape_progress,'an active animation must not consume the stationary retry budget')
  act.ready=function() return true end; time(10641); c:fight(me,12)
  assert(not c.plan and not c.escape_progress and c.escape_retry_until==11041 and c.last_action==10480,
    'stationary NN movement must yield finitely without claiming an action or forcing an unsafe cast')
  time(10681); c:fight(me,12)
  assert(#calls.moves==4,'stationary movement must respect its shared refusal delay')
  time(11041); c:fight(me,12)
  assert(#calls.moves==5 and c.escape_progress.at==11041,'NN escape may reassess traffic after the retry delay')
end

for _,reset in ipairs{'movement','gap','cast','refusal'} do
  local c,me,calls,time=escape_scenario()
  c:fight(me,12)
  time(reset=='gap' and 11120 or 10160)
  if reset=='movement' then me.x=-1 end
  if reset=='refusal' then act.move=function() return false end end
  if reset=='cast' then
    local enemies=game.units('monster')
    enemies[1].x,enemies[2].x,enemies[3].x=5,20,20
    map.safe_spot=function() return {found=false,path={},danger_here=0,missile_here=0,open_sectors=8} end
  end
  c:fight(me,12)
  if reset=='movement' or reset=='gap' then
    assert(c.escape_progress and c.escape_progress.at==(reset=='gap' and 11120 or 10160),
      'actual movement and an inactive gap must renew the NN escape progress budget: '..reset)
  elseif reset=='cast' then
    assert(#calls.casts==1 and not c.escape_progress,'an accepted attack must clear stale escape observations')
  else
    assert(not c.plan and not c.escape_progress and c.escape_retry_until==10560 and c.last_action==10000,
      'a refused NN move must clear progress and throttle without claiming a successful action')
  end
end

for _,refused in ipairs{false,true} do
  local enemy={x=10,y=0}
  local c,me,calls=scenario(true,{enemy})
  local t=10000
  charon.now=function() return t end
  charon.frame=function() return t end
  if refused then
    act.move=function(x,y)
      calls.moves[#calls.moves+1]={x=x,y=y}
      return false
    end
  end
  -- Sample rejected requests at retry expiry, isolating the damage deadline from waiting.
  local step=refused and 400 or 200
  for tick=0,30000/step do
    t=10000+tick*step
    enemy.x=14+tick%3 -- a moving target must not renew the no-damage deadline
    c:fight(me,20)
    assert(c.target_id==enemy.id and not c.ignored[enemy.id],
      'intermediate route yields must preserve the pending engagement')
    assert(c.target_progress==10000,'replanned approaches must not claim damage progress')
  end
  assert(#calls.moves>5 and #calls.casts==0,'exercise repeated routes without reaching attack range')
  if refused then assert(c.last_action==0,'rejected approaches must never claim an accepted action') end
  local before,actions=c.last_action,#calls.moves
  t=40200; c:fight(me,20)
  assert(c.target_id==nil and c.ignored[enemy.id]>t and not c.resting,
    'the NN must release a moving target after 30 seconds of stationary or refused approaches')
  assert(c.last_action==before and #calls.moves==actions and #calls.casts==0,
    'releasing a stalled target must not send or claim an action')
end

do
  local c,me,calls=scenario(true,{{x=14,y=0}})
  local t=10000
  charon.now=function() return t end
  charon.frame=function() return t end
  map.safe_spot=function()
    return {found=false,path={},danger_here=0,missile_here=0,open_sectors=8}
  end
  c:fight(me,20)
  t=15000; c:fight(me,20)
  assert(c.target_id==1 and not c.ignored[1],'unproductive waiting retains its five-second grace period')
  t=15001; c:fight(me,20)
  assert(c.target_id==nil and c.ignored[1]>t and c.last_action==0 and #calls.moves==0 and #calls.casts==0,
    'a target with no legal productive action must be released after five seconds of waiting')
  -- Given up from beyond her reach, it is hers again once she stands within it.
  assert(c.unreached and c.unreached[1],'one given up from afar is remembered as out of reach')
  t=16000
  assert(not c:threat(me,20),'while it is far it stays set aside')
  local kept_me=game.me
  game.me=function() return {x=13,y=0,life=me.life,max_life=me.max_life,area=me.area} end
  local left=c:monsters()[1]
  assert(left and left.id==1 and c:usable(left),'brought within her reach it is fought again')
  assert(c.ignored[1]==nil and not c.unreached[1],'and no longer set aside')
  game.me=kept_me
end

for _,waiting in ipairs{false,true} do
  local enemy={x=14,y=0}
  local c,me,calls=scenario(true,{enemy})
  local t=10000
  charon.now=function() return t end
  charon.frame=function() return t end
  game.units=function(kind) return not me.in_town and kind=='monster' and {enemy} or {} end
  if waiting then
    map.safe_spot=function() return {found=false,path={},danger_here=0,missile_here=0,open_sectors=8} end
  end
  c:fight(me,20)
  assert(c.target_id==1 and c.target_progress==10000,'begin a real field engagement before town')
  if waiting then assert(c.neural.wait_at==10000 and c.neural.wait_target==1,'exercise the real five-second waiting history') end
  me.stat_points,me.skill_points=0,0
  c.kills,c.last_potion,c.last_mana,c.last_points=7,9000,8000,7000
  c.health_wait_logged=true;c.buff_tried={armor=90000};c.belted={[42]=90000};c.neural.nova_at=9000
  c.ignored[99]=90000
  c.resting=true -- town must end a field mana-recovery state as well
  local action_at=c.last_action
  t=12000;me.in_town=true;me.area=1;c:upkeep(me)
  t=43001;c:upkeep(me)
  assert(not c.target_id and not c.target_life and not c.target_progress and not c.neural.wait_at and not c.neural.wait_target,
    'town upkeep must release field target and both no-progress deadlines')
  assert(not c.plan and not c.escape_progress and not c.resting,'town must not retain a tactical field route or mana-wait state')
  assert(c.kills==7 and c.last_potion==9000 and c.last_mana==8000 and c.last_points==7000 and c.last_action==action_at
    and c.health_wait_logged and c.buff_tried.armor==90000 and c.belted[42]==90000 and c.neural.nova_at==9000
    and c.ignored[99]==90000,'town engagement cleanup must preserve survival, skill cooldown and unrelated ignore history')
  me.in_town=false;me.area=2;t=43041
  local before=#calls.casts
  if not waiting then enemy.x=5 end
  c:fight(me,20)
  assert(c.target_id==1 and not c.ignored[1] and c.target_progress==t,'the same living unit must acquire a fresh field engagement after town')
  if waiting then
    assert(c.neural.wait_at==t and c.neural.wait_target==1 and #calls.casts==before,
      'unproductive post-town waiting must receive a fresh five-second grace period')
    enemy.x=5;t=t+80;c:fight(me,20)
  end
  assert(#calls.casts==before+1 and not c.ignored[1] and not c.neural.wait_at,'a real attack after town must not inherit either timeout')
end

do
  local c,me,calls=scenario(true,{})
  me.in_town=true;me.stat_points,me.skill_points=0,0
  c:upkeep(me)
  assert(not c.target_id and c:fight(me,12)=='clear' and #calls.casts==0 and #calls.moves==0,
    'town engagement cleanup must leave an initially targetless combat object valid')
end

value.priority=saved_priority
value.dps=saved_dps
print('combat decision regressions passed')
