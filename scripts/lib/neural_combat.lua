-- Legal actions share one authored candidate scorer (lib/combat_weights.lua).
--   local c = require('lib.combat').new(build)
-- Lua enumerates requests and describes each by its expected outcome: how much
-- of the best achievable damage rate it keeps, and how much of our remaining
-- life the monsters in reach would take meanwhile. Native safe_spot computes
-- the tactical routes; the network ranks the candidates.
local supplies = require 'lib.supplies'
local value = require 'lib.value'
local world = require 'lib.world'
local scorer = require 'lib.scorer'
local weights = require 'lib.combat_weights'
local neural = {}
neural.__index = neural
local SEARCH_MS = 120 -- reuse local geometry for three ticks unless position/area changes
local NOVA_MS = 1200 -- allow the client chill state to arrive before repeating control
local WALK_SPEED = 6 -- approximate subtiles per second
-- A unit attack is walked in by the game itself, so a swing is asked for
-- from a little beyond contact. Held to contact (3), she stood three to four
-- subtiles from a zombie with neither a swing nor a step on offer: too far
-- for the one, and the game refused the other because the zombie's bulk
-- stood on the ground asked for. Stepping back and coming in again, two
-- zombies took 100 seconds of one crossing of the Blood Moor.
local MELEE_CONTACT = 4
local MELEE_CLOSE = 2.25 -- where a melee approach aims: inside contact, so arriving is enough to swing
local HORIZON = 3 -- seconds of dealing damage that a move is judged over
local EXPOSURE_S = 4 -- seconds of taking damage that a position is judged over
local STRIKE_S = 1.2 -- one cast and the first steps away: what a strike adds to the exposure of leaving
local STRIKE_MS = 2500 -- after a strike the next cast in danger is judged over EXPOSURE_S again, this long
local STRIKE_CLEAR = 0.2 -- ...or until the exposure has dropped below this
-- Above this share of life a duel takes every cast it can get. A boss that
-- outruns us is not left by stepping away: with one strike in 2.5 s Andariel
-- lost three quarters of her life in thirty seconds beside her, where six
-- casts of Static Field in four take more. Below it the step is owed as
-- before, and the survival rules decide when to leave.
local DUEL_FLOOR = 0.6
local FAR_LETHAL = 0.2 -- an approach whose end would cost this much is traded for one that ends at casting range
local CHILL_SPEED = 0.5 -- share of its walking speed a chilled monster keeps when its row does not say
local LEARN_INTAKE, LEARN_FACTOR = 0.08, 1.5 -- a class is marked stronger from losses this large (share of full life per second) and this far off its table
local CAST_S, SWING_S = 0.55, 0.7 -- one spell or weapon animation, for comparing damage rates
local MELEE_FULL, MELEE_REACH = 2, 9 -- a melee monster threatens fully this close, not at all this far
local RANGED_SHARE = 0.5 -- a shooter in sight threatens wherever we stand; dodging handles its missiles
local CHILL_SLOW = 0.6 -- share of its damage rate a chilled monster keeps
local NOVA_REACH, CLOSE = 7, 4.5 -- Frost Nova's reach; monsters this near are in our face
local PACK_REACH, PACK_SHARE = 3.5, 0.5 -- stray bolts find monsters this close to the target or its line
local FAN_COS = 0.8 -- a fanned volley also meets monsters within about 35 degrees of the aim
local MISSILE_LETHAL = 0.25 -- a missile taking this share of our remaining life is a full threat
local CLOSING = 4 -- subtiles per second a melee monster walks into a volley already in flight
local ESCAPE_EXPOSURE = 0.1 -- below this lethality there is nothing to escape from but missiles
local MISS_DISCOUNT = 0.25 -- each recent cast without damage lowers the hit estimate here (three at most)
local CLEAR_CASTS = 14 -- a fight that takes more casts than this is not decided where we stand
-- Staff work may take longer than that: it costs no mana, only the life the
-- arithmetic already charges for, and the other choice is to walk round what
-- she will not club. At eight players a Blood Moor zombie takes some twenty
-- blows; held to fourteen she kited it instead, and it is slow enough to
-- kite for ever.
local STAFF_BLOWS = 30
local CLEAR_SLACK = 1.25 -- fights run longer than their arithmetic: retargeting, misses, a potion
local POTION_BUDGET = 0.5 -- healing in hand counts toward what a fight may cost, up to this share of full life
local STATIC_SHARE = 0.25 -- of its current life Static Field takes from everything in reach
local HARDER_CAP = 3 -- measured losses correct the damage tables by at most this; beyond it is poison or fire
-- Rationing mana (see `ration` below).
--   swings  the staff takes what it kills in this many blows
--   pack    with more than this many others in the volley's way, the cast is worth its mana
--   harm    and only what would take less than this share of her life while she swings
--   plenty  with this many mana potions in hand there is nothing to ration
--   keep    nor with more than this share of her pool: it fills by itself (all
--           of it in two minutes), and what it would gain while full is lost.
--           Rationed whatever the pool held, it stood between 70% and full
--           through two whole Den clears while she walked up to a hundred
--           monsters to club them. The campaign's `save_mana` (something
--           ahead that will take a pool) rations up to a full one.
local RATION = { swings = 5, pack = 1, harm = 0.15, plenty = 4, keep = 0.6 }
-- A fanned volley is cast from where at least this share of it lands (the
-- maintainer: "unless we're sure charged bolt can hit multiple monsters"),
-- while there is a way closer that is safe to take. From the edge of her
-- range a quarter of it connects: four casts to a zombie of Corpsefire's
-- pack, a whole pool for one kill, and two minutes for the next pool.
local SURE = 0.5
-- With somewhere to go, a subtile onward is worth twice what a subtile walked
-- costs the safe-spot search (its travel weight, 0.04): a dodge on the way
-- then steps on out of the lane. With onward only breaking ties she dodged
-- back and forth under archers in the Tower's cellars and got nowhere.
local ONWARD = 0.08
-- Missiles: `count` bolts leave together and fan out, so all connect within
-- `spread` subtiles and proportionally fewer beyond; `range` is the farthest we cast from.
local BOLTS = {
  { name = 'Charged Bolt', range = 10, spread = 2.5, count = function(level) return math.min(24, level + 2) end },
  { name = 'Fire Bolt', range = 7 },
  { name = 'Ice Bolt', range = 7 },
}
-- What one cast of the best learned bolt does to what it hits, at most: every
-- bolt of the volley connecting. `known` is combat:known_skills().
function neural.cast_power(known)
  local best = 0
  for _, entry in ipairs(BOLTS) do
    local s = known and known[entry.name]
    if s and (s.level or 0) > 0 then
      best = math.max(best, (entry.count and entry.count(s.level) or 1) * value.cast_damage(s))
    end
  end
  return best
end

-- The same for one bolt at a level she may not have yet: what lib/gear asks
-- when it weighs an item that adds to it.
function neural.power_at(name, id, level)
  for _, entry in ipairs(BOLTS) do
    if entry.name == name then
      return (entry.count and entry.count(level) or 1) * value.cast_damage({ id = id, level = level })
    end
  end
  return 0
end

-- Every input this module fills in; lib.scorer checks it against the artifact.
local FEATURES = {'tempo','finish','progress','lethality_after','outlast','missile_after','move','time','mana',
  'mana_cost','thrift','nova','target_chill','kite_intent','continuity','target_priority','wait','engaged'}
local function now() return charon.now() end
local function clip(n) return math.max(0, math.min(1,n)) end
local function ratio(n,d) return d > 0 and clip(n/d) or 0 end
local function elite(u) return u.unique or u.superunique or u.champion or u.boss end
local function elite_pack(u) return elite(u) or u.minion end
local function available(s,me)
  return s and s.level > 0 and s.usable and type(s.mana)=='number' and me.mana >= s.mana
end
local function can_chill(unit)
  local info=unit.class_id and value.monster(unit.class_id)
  return value.through(value.resist(unit,'cold'))>0 and (not info or info.cold_effect==nil or info.cold_effect<0)
    and not game.state('monster',unit.id,11)
end
function neural.new()
  local s=scorer.new(weights,FEATURES)
  return setmetatable({scorer=s,model=s.model,version=weights.version,nova_at=-NOVA_MS},neural)
end

-- Two short native routes give the scorer an escape and a useful firing position.
-- Cache only their geometry/metrics; legal actions and fire checks are rebuilt every tick.
function neural:routes(c,me,target,range,heading)
  local key = tostring(me.area)..':'..tostring(target and target.id)..':'..tostring(range)
  local options={threats=c:threats(me),missiles=c.missile_lanes,hazards=c.ground_hazards,
    radius=8,occupied=1.5,clearance=1,lead=0.5,heading=heading,heading_weight=heading and ONWARD or nil}
  if self.route_area~=me.area or not self.route_from or now()-(self.route_at or 0)>=SEARCH_MS
    or world.distance(me,self.route_from)>=1 then
    self.route_area,self.route_at,self.route_from=me.area,now(),{x=me.x,y=me.y}
    self.escape=map.safe_spot(options)
    self.route_key,self.approach=nil,nil
  end
  -- Global missile checks and the fight in the same tick share the escape
  -- query; a target-less check must not discard the cached approach.
  if target and self.route_key~=key then
    options.target={x=target.x,y=target.y}
    options.range,options.range_weight=range,4
    options.radius=12
    self.approach=map.safe_spot(options)
    self.route_key=key
  end
  return self.escape,target and self.approach or nil
end

-- The approach that ends at casting range rather than where the volley
-- connects, searched and cached apart from the usual one.
function neural:far_approach(c,me,target,range,heading)
  local key=tostring(me.area)..':'..tostring(target.id)..':'..tostring(range)
  if self.far_key~=key or not self.far_from or now()-(self.far_at or 0)>=SEARCH_MS
    or world.distance(me,self.far_from)>=1 then
    self.far=map.safe_spot{threats=c:threats(me),missiles=c.missile_lanes,hazards=c.ground_hazards,
      radius=12,occupied=1.5,clearance=1,lead=0.5,heading=heading,
      target={x=target.x,y=target.y},range=range,range_weight=4}
    self.far_key,self.far_at,self.far_from=key,now(),{x=me.x,y=me.y}
  end
  return self.far
end

local function route_legal(c,me,spot)
  if not spot or not spot.found or not spot.path or #spot.path==0
    or (spot.hazard_there or 0)>0 or world.distance(me,spot)<1 then return false end
  -- Check every segment for newly appeared fire; a straight endpoint shortcut
  -- would reject legitimate native detours around a patch.
  local previous=me
  for _,point in ipairs(spot.path) do
    if not c:ground_clear(previous,point) then return false end
    previous=point
  end
  return true
end

-- What each visible hostile would take from us per second, whether Nova would
-- slow it, and the life it has left (`static`: the part Static Field can take).
local function threat_list(me,near,floor)
  local list={}
  for _,u in ipairs(near) do
    local chillable=can_chill(u)
    local chilled=not chillable and game.state('monster',u.id,11)
    local full=value.life(u)
    local life=full*(u.life_percent or 100)/100
    -- A chilled monster walks slower, so it reaches less far within the window.
    local info=u.class_id and value.monster(u.class_id)
    local slow=info and info.cold_effect and info.cold_effect<0 and math.max(0.25,1+info.cold_effect/100) or CHILL_SPEED
    list[#list+1]={x=u.x,y=u.y,ranged=u.ranged,dps=value.dps(u)*(chilled and CHILL_SLOW or 1),
      class_id=u.class_id,reach=MELEE_FULL+(MELEE_REACH-MELEE_FULL)*(chilled and slow or 1),
      chilled_reach=MELEE_FULL+(MELEE_REACH-MELEE_FULL)*slow,
      nova=chillable and world.distance(me,u)<=NOVA_REACH,life=life,
      static=math.max(0,life-full*floor/100)*value.through(value.resist(u,'lightning'))}
  end
  return list
end

-- Life per second the threats would take if we stood at `at`, and the one
-- that takes most. `chilled` applies a Frost Nova cast from here.
local function pressure(list,at,chilled)
  local rate,worst,worst_rate=0,nil,0
  for _,t in ipairs(list) do
    local slowed=chilled and t.nova
    local dps=slowed and t.dps*CHILL_SLOW or t.dps
    local taken=0
    if t.ranged then
      taken=dps*RANGED_SHARE
    else
      local d,reach=world.distance(at,t),slowed and t.chilled_reach or t.reach
      if d<reach then taken=dps*(1-math.max(0,d-MELEE_FULL)/(reach-MELEE_FULL)) end
    end
    rate=rate+taken
    if taken>worst_rate then worst,worst_rate=t,taken end
  end
  return rate,worst
end

-- How much of our remaining life that is over EXPOSURE_S, as x/(1+x) so that
-- it keeps rising past "all of it": 0.33 is half our life, 0.5 all of it.
local function lethality(list,at,life,chilled)
  local share=pressure(list,at,chilled)*EXPOSURE_S/math.max(1,life)
  return share/(1+share)
end

function neural:candidates(c,me,target,global,heading)
  local known=c:known_skills()
  local near=c:hostiles(me,16)
  local distance=target and world.distance(me,target) or 0
  local target_life=target and value.life(target)*(target.life_percent or 100)/100 or 0
  -- A fanned volley also meets the target's neighbours and whatever stands
  -- between us and it: their life adds to what one cast can usefully remove,
  -- and the nearest of them is where the bolts start connecting.
  local pack,pack_life,pack_near=0,0,distance
  if target and distance>0 then
    local ax,ay=(target.x-me.x)/distance,(target.y-me.y)/distance
    for _,u in ipairs(near) do
      if u.id~=target.id then
        local d=world.distance(me,u)
        local ahead=d>0 and d<=distance+PACK_REACH and ((u.x-me.x)*ax+(u.y-me.y)*ay)/d>=FAN_COS
        if ahead or world.distance(u,target)<=PACK_REACH then
          pack,pack_life=pack+1,pack_life+value.life(u)*(u.life_percent or 100)/100
          if ahead then pack_near=math.min(pack_near,d) end
        end
      end
    end
  end
  local miss=1-MISS_DISCOUNT*math.min(3,c.misses or 0)
  -- Expected damage of one volley from `d` subtiles: on the target, and in all.
  local function volley(entry,skill,d,discount)
    local per=value.cast_damage(skill,target)
    local count=entry.count and entry.count(skill.level) or 1
    local share=entry.spread and math.min(1,entry.spread/math.max(d,0.1)) or 1
    local on_target=math.min(count*per*share*(discount or 1),target_life)
    if not entry.spread or pack==0 then return on_target,on_target end
    -- Bolts that miss the target still connect with the pack, from its nearest member on.
    local first=math.min(d,math.max(entry.spread,pack_near-(distance-d)))
    local connect=math.min(1,entry.spread/math.max(first,0.1)*(1+PACK_SHARE*pack))
    return on_target,on_target+math.max(0,math.min(count*per*connect-on_target,pack_life))
  end
  -- The learned bolt with the best damage rate from its ideal distance leads the build.
  local best,has_spells
  if target then
    for _,entry in ipairs(BOLTS) do
      local s=known[entry.name]
      if s and s.level>0 then
        has_spells=true
        local _,total=volley(entry,s,entry.spread or entry.range)
        if value.cast_damage(s,target)>0 and (not best or total>best.total) then
          best={entry=entry,skill=s,total=total}
        end
      end
    end
  else
    for _,entry in ipairs(BOLTS) do
      local s=known[entry.name]
      if s and s.level>0 then has_spells=true end
    end
  end
  -- An elite and its company are fought from range or not at all, while she
  -- has a spell that touches them: the staff among them is paid for by the
  -- whole pack at once. Dry in the Den at 59% life she clubbed the zombies
  -- around Corpsefire, drank her three potions in ten seconds and left the
  -- game; that pack ended two runs of five.
  local company=false
  if target and best and not target.boss then
    company=elite_pack(target) and true or false
    for _,u in ipairs(near) do
      if company then break end
      company=elite_pack(u) and world.distance(u,target)<=MELEE_REACH or false
    end
  end
  local melee_allowed=not has_spells or c.melee_ok
  local melee=melee_allowed and not company
  -- Stance is shared by every candidate: a dry or weaponless caster closes to
  -- contact, a caster with mana closes to where its volley connects.
  local weapon=melee and game.attack_damage and game.attack_damage()
  local weapon_damage=weapon and (weapon.min+weapon.max)/2 or 1
  weapon_damage=weapon_damage*(target and value.through(value.resist(target,'physical')) or 1)
  local swing=target and math.min(weapon_damage,math.max(target_life,1)) or weapon_damage
  -- Mana is rationed (the maintainer, on how runners clear the Den: "they
  -- just ration their mana and melee enemies that could reliably be killed
  -- with melee instead of forcing use of spells for every single one", and
  -- later: "use melee more often unless we're sure charged bolt can hit
  -- multiple monsters"). Where the campaign allows the staff it takes what
  -- dies in RATION.swings blows and will not hurt her much in that time,
  -- when a cast would not take a pack along with it; the pool is kept for
  -- packs and for what the staff cannot deal with. Cast at everything, a
  -- new character's pool was gone in ten casts and she fought half of every
  -- Den clear without it. It was rationed only with no mana potion in her
  -- belt; then the two or three she had went the way of the pool first.
  local blows=weapon_damage>0 and math.ceil(target_life/weapon_damage) or math.huge
  local ration=melee and c.melee_ok and has_spells and best~=nil and target and not target.boss
    and supplies.mana()<RATION.plenty and me.mana<(c.save_mana and 1 or RATION.keep)*me.max_mana
    and blows<=RATION.swings
    and pack<=RATION.pack and value.dps(target)*blows*SWING_S<=RATION.harm*me.life or false
  local melee_stance=melee and (ration or not best or me.mana<best.skill.mana or swing/SWING_S>=best.total/CAST_S)
  local range=melee_stance and MELEE_CONTACT or best and best.entry.range or c.build.range
  -- Close to where the whole volley connects; single missiles only need their range.
  local firing_range=melee_stance and MELEE_CLOSE or best and best.entry.spread or math.max(2.5,range-0.5)
  local shortage=ration or best and me.mana<best.skill.mana or false
  self.attack_mana=best and best.skill.mana or nil -- what the build's attack costs, for combat:dry
  local sight=target and map.line_of_sight(me.x,me.y,target.x,target.y) or false
  -- Best damage rate our legal attacks reach from `d` subtiles, sight assumed.
  local function offense(d)
    local rate=0
    if not shortage then
      for _,entry in ipairs(BOLTS) do
        local s=known[entry.name]
        if available(s,me) and d<=entry.range then
          local _,total=volley(entry,s,d)
          rate=math.max(rate,total/CAST_S)
        end
      end
    end
    if melee and d<=MELEE_CONTACT then rate=math.max(rate,swing/SWING_S) end
    return rate
  end
  -- Every candidate's damage rate is judged against the best one on offer,
  -- including what the leading attack would reach from its ideal distance.
  local best_rate=math.max(melee and swing/SWING_S or 0,best and not shortage and best.total/CAST_S or 0)
  local escape,approach=self:routes(c,me,global and nil or target,firing_range,heading)
  local here=escape or approach or {}
  -- Staff work walks up to its target. The safe-spot search will not end a
  -- route beside a monster, which left a dry sorceress waiting six subtiles
  -- from a Fallen she could have clubbed: go straight at it instead.
  -- That holds whether or not the search found a spot of its own: the spot
  -- it finds is never beside the monster, and with one found and useless
  -- she stood three or four subtiles from her target with neither a swing
  -- nor a step on offer, more than half the time she was that near. The
  -- step is at least one subtile, which is the least a route may be.
  if not global and target and melee_stance and c.melee_ok and sight and distance>MELEE_CONTACT then
    local k=math.max(1,distance-MELEE_CLOSE)/distance
    local x,y=me.x+(target.x-me.x)*k,me.y+(target.y-me.y)*k
    approach={found=true,x=x,y=y,path={{x=x,y=y}},danger_here=0,danger_there=0,missile_there=0,hazard_there=0}
  end
  -- A lane counts for what its missile would take from us: a Fallen Shaman's
  -- fireball is not worth a step, an arrow volley that halves our life is.
  local lanes=c.missile_lanes or {}
  local harm=#lanes==0 and 1 or 0 -- danger without a known lane counts in full
  for _,lane in ipairs(lanes) do
    harm=math.max(harm,(lane.hit or me.life)/math.max(1,me.life)/MISSILE_LETHAL)
  end
  harm=math.min(1,harm)
  local incoming=here.missile_here or 0
  local static=known['Static Field']
  local static_ok=available(static,me)
  local static_reach=static_ok and 5+static.level-1 or 0
  local floor=static_ok and (game.difficulty_info() or {}).static_field_min or 0
  local threats=threat_list(me,near,floor)
  -- The tables know a monster's physical blow, not its spells, auras or
  -- enchantments: when we lose life faster than they say, every threat here
  -- hits that much harder than modelled.
  local modelled,worst=pressure(threats,me)
  local intake=c.intake and c:intake() or 0
  -- The losses are those of the last seconds, so the tables are asked about
  -- the same seconds: what they made of the threats at each decision since,
  -- on average. Asked only about this moment, they said next to nothing of
  -- the zombie she had just stepped back from, the blow it had landed came
  -- out at many times its table, and in every run the first blow in the Den
  -- marked zombies (and Fallen with them) four times as dangerous for the
  -- session; a sorceress out of mana then would not close with one to club
  -- it, and stood off the last of the Den until the game was given up.
  local window=c.INTAKE_MS or 2000
  local at=now()
  if #lanes>0 or incoming>0 then self.missiles_at=at end
  -- What the class that presses hardest lands of what its table promises,
  -- stretch by stretch (value.observe): a stretch is the time since the last
  -- decision, and not one with missiles in the air, whose hits are not blows.
  local lost_total=c.lost_total or 0
  local stretch=math.min(500,at-(self.stood_at or at))
  local lost=lost_total-(self.lost_seen or lost_total)
  self.stood_at,self.lost_seen=at,lost_total
  if worst and worst.class_id and modelled>0 and stretch>0 and at-(self.missiles_at or -math.huge)>window then
    local rated=value.observe(worst.class_id,modelled*stretch/1000,lost,me.max_life)
    if rated then
      print(string.format('neural: class %d lands %.0f%% of its table damage',worst.class_id,rated*100))
    end
  end
  local seen=self.pressures or {}
  self.pressures=seen
  seen[#seen+1]={at=at,rate=modelled,class_id=worst and worst.class_id}
  while seen[1] and (at-seen[1].at>window or at<seen[1].at) do table.remove(seen,1) end
  local sum,by_class,most,most_rate=0,{},nil,0
  for _,s in ipairs(seen) do
    sum=sum+s.rate
    if s.class_id then
      by_class[s.class_id]=(by_class[s.class_id] or 0)+s.rate
      if by_class[s.class_id]>most_rate then most,most_rate=s.class_id,by_class[s.class_id] end
    end
  end
  local mean=sum/#seen
  local base=math.max(modelled,mean)
  local harder=base>0 and math.min(HARDER_CAP,math.max(1,intake/base)) or 1
  if harder>1 then
    for _,t in ipairs(threats) do t.dps=t.dps*harder end
    -- A real loss far off the table is remembered against the class that
    -- was taking most of it, once the model covers enough of those seconds
    -- to be compared with them.
    -- Not while missiles fly, though: what a Fallen Shaman's fireball takes
    -- is no blow of the Fallen who stand nearest, and with the two told
    -- apart by nothing the Fallen and the zombies of the Den were marked
    -- three and four times as dangerous in every run, after which a dry
    -- sorceress kept her staff down (half her choices there were to step
    -- away or wait) and the Den took five minutes and more.
    local quiet=at-(self.missiles_at or -math.huge)>window
    local off=mean>0 and intake/mean or 0
    if most and quiet and at-seen[1].at>=window/2 and off>=LEARN_FACTOR and intake>=LEARN_INTAKE*me.max_life
      and value.learn(most,value.harder(most)*math.min(HARDER_CAP,off)) then
      print(string.format('neural: class %d takes %.1f times its table damage',most,value.harder(most)))
      -- What was just learned is in the tables from now on, and the
      -- seconds behind us were reckoned without it: they are not compared
      -- again, or the same blows would be learned twice and three times
      -- over (zombies went from 1.6 to 2.7 to 4 on one exchange).
      self.pressures={}
    end
  end
  self.harder=harder
  local exposed=lethality(threats,me,me.life)
  -- Out of range, with the spot where the whole volley connects too dangerous
  -- to walk to, the leg on offer ends where the farthest bolts reach instead;
  -- closing in is a second step, taken or not once she can shoot. With only
  -- the close spot on offer and a pack beside it, a sorceress with full life
  -- and mana waited fifteen subtiles off until the stall rule left the game,
  -- and the Den was cleared three times over.
  if not global and target and not melee_stance and best and best.entry.spread and distance>range
    and approach and approach.found and lethality(threats,approach,me.life)>=FAR_LETHAL then
    approach=self:far_approach(c,me,target,math.max(2.5,range-0.5),heading)
  end
  -- Two kinds of fight are settled where we stand rather than kited.
  -- A duel: an act boss must die, cannot be shed and has more life than any
  -- kite is worth. And staff work: out of mana where the campaign allows
  -- swinging, waiting for mana costs more time than clubbing what dies in a
  -- few blows and hurts little (ninety seconds in the Den went to 305
  -- escapes and 224 waits against ten swings).
  local duel=not global and target and target.boss or false
  local staff=not global and target and melee_stance and c.melee_ok or false
  -- A duel may cost the life we have plus part of the healing in hand, and
  -- the mana we hold plus what the potions carried restore. So may staff
  -- work. It was paid for in life alone, to save the gold potions cost;
  -- then, in the Den with her mana gone and her life at 56%, she stood off
  -- the last monsters for ten minutes with three healing potions in her
  -- belt, neither drinking (a potion heals more than she lacked) nor going
  -- in. Ten minutes cost more than the potions.
  local budget=me.life+math.min(supplies.life_points(me),POTION_BUDGET*me.max_life)
  local cast_mana=math.max(best and not melee_stance and best.skill.mana or 0,static_ok and static.mana or 0)
  local mana_bank=me.mana+supplies.mana_points(me)
  -- Share of that budget left once the threats in reach are dead, fighting
  -- from `at` with the target `d` away: every melee monster in reach arrives,
  -- each cast is the better of Static Field and the best volley, and a fight
  -- that outlasts CLEAR_CASTS or the mana is not decided here. Also returns
  -- the mana the fight takes.
  --
  -- Against packs with spells in hand this plan and the strike below were
  -- tried and withdrawn: every one of eighteen sudden losses of life in a
  -- run followed an attack they had allowed at an exposure of 0.3 or more.
  local function outlast(at,d,purse)
    if not (duel or staff) then return 0,0 end
    local rate,pool,in_static=0,0,0
    for _,t in ipairs(threats) do
      local reach=world.distance(at,t)
      if t.ranged or reach<MELEE_REACH then
        rate=rate+t.dps*(t.ranged and RANGED_SHARE or 1)
        pool=pool+t.life
        if reach<=static_reach then in_static=in_static+t.static end
      end
    end
    if rate<=0 then return 1,0 end
    local volley_damage=offense(d)*CAST_S
    local casts=0
    while pool>1 do
      local field=in_static*STATIC_SHARE
      local hit=math.min(volley_damage,pool)
      if math.max(field,hit)<=0 or casts>=(staff and STAFF_BLOWS or CLEAR_CASTS) then return 0,0 end
      if field>hit then
        in_static,pool=in_static-field,pool-field
      else
        in_static,pool=in_static*(1-hit/pool),pool-hit
      end
      casts=casts+1
    end
    local need=casts*cast_mana
    if need>mana_bank then return 0,0 end
    return clip(1-rate*casts*CAST_S*CLEAR_SLACK/math.max(1,purse or budget)),need
  end
  local stay,stay_mana=outlast(me,distance)
  self.stay,self.stay_mana=stay,stay_mana
  local target_chill=target and distance<=NOVA_REACH and can_chill(target) and sight or false
  local priority=target and (elite(target) or value.is_resurrector(target)) and 1 or 0
  local actions={}
  local function add(a)
    if a.spot then
      local previous=me
      a.seconds=0
      for _,point in ipairs(a.spot.path) do
        a.seconds=a.seconds+world.distance(previous,point)/WALK_SPEED
        previous=point
      end
      -- A move keeps the damage we can deal from its end for the rest of the horizon.
      -- An approach still short of range is the first leg of a longer walk.
      local from=target and world.distance(a.spot,target) or 0
      local beyond=a.name=='approach' and math.max(0,from-range) or 0
      a.rate=target and not global
        and offense(from-beyond)*math.max(0,HORIZON-a.seconds-beyond/WALK_SPEED)/HORIZON or 0
      -- Out of range, an approach that still ends short closes part of the gap;
      -- within range its worth is the damage rate at its end (tempo).
      local gap=target and distance-range or 0
      if a.name=='approach' and gap>0 then
        a.progress=(gap-math.max(0,world.distance(a.spot,target)-range))/gap
      end
    else
      a.rate=a.damage and a.damage/a.seconds or 0
    end
    best_rate=math.max(best_rate,a.rate)
    a.family=a.family or a.name
    actions[#actions+1]=a
  end
  if not global and target then
    -- Whether she can simply walk closer: the step is there and ends in no danger.
    local closer=now()>=(c.escape_retry_until or 0) and route_legal(c,me,approach)
      and world.distance(approach,target)<distance-0.5 and lethality(threats,approach,me.life)<FAR_LETHAL
    -- Mask cheaper bolts while the strongest learned bolt is unaffordable.
    for _,entry in ipairs(BOLTS) do
      local s=known[entry.name]
      if available(s,me) and not shortage and distance<=entry.range and sight then
        -- A melee monster keeps coming while the cast completes; a shooter stands off.
        local meets=entry.spread and not target.ranged
          and math.max(entry.spread,distance-CLOSING*CAST_S) or distance
        local on_target,total=volley(entry,s,meets,miss)
        -- What the whole volley could do here, were all of it to land (see SURE).
        local whole=math.min((entry.count and entry.count(s.level) or 1)*value.cast_damage(s,target),target_life+pack_life)
        if entry.spread and closer and total<SURE*whole then total=0 end
        if total>0 then
          add{name=entry.name,family='bolt',skill=s,damage=total,target_damage=on_target,seconds=CAST_S,attack=true}
        end
      end
    end
    if melee and distance<=MELEE_CONTACT and sight then
      -- Use the existing character-screen binding; old hosts conservatively use one damage.
      if weapon_damage>0 then
        add{name='melee',damage=swing,target_damage=swing,seconds=SWING_S,attack=true,melee=true}
      end
      local skill=c.build.melee_skill and known[c.build.melee_skill]
      if available(skill,me) then
        -- Conservative: the skill API supplies flat components, not Bash's full weapon multiplier.
        local damage=math.min(weapon_damage+value.cast_damage(skill,target),math.max(target_life,1))
        if damage>0 then
          add{name=c.build.melee_skill,skill=skill,damage=damage,target_damage=damage,seconds=SWING_S,attack=true,melee=true}
        end
      end
    end
    if static_ok then
      local total,target_damage=0,0
      for _,u in ipairs(near) do
        if world.distance(me,u)<=static_reach then
          local full=value.life(u)
          local hp=full*(u.life_percent or 100)/100
          local damage=math.min(hp*STATIC_SHARE,math.max(0,hp-full*floor/100))*value.through(value.resist(u,'lightning'))
          total=total+damage
          if u.id==target.id then target_damage=damage end
        end
      end
      if total>0 then
        add{name='Static Field',skill=static,damage=total,target_damage=target_damage,seconds=CAST_S,attack=true,self_cast=true}
      end
    end
  end
  local nova=known['Frost Nova']
  if available(nova,me) and now()-self.nova_at>=NOVA_MS then
    local chill,close_chill,elite_chill,damage,target_damage=0,0,false,0,0
    for _,u in ipairs(near) do
      if world.distance(me,u)<=NOVA_REACH then
        local hit=math.min(value.cast_damage(nova,u),value.life(u)*(u.life_percent or 100)/100)
        damage=damage+hit
        if target and u.id==target.id then target_damage=hit end
        if can_chill(u) then
          chill=chill+1; elite_chill=elite_chill or elite(u)
          if world.distance(me,u)<=CLOSE then close_chill=close_chill+1 end
        end
      end
    end
    local defensive=elite_chill or chill>=3 or close_chill>=2 or (me.life<me.max_life*0.7 and close_chill>0)
    if target_chill or defensive then
      add{name='Frost Nova',skill=nova,damage=damage,target_damage=target_damage,seconds=CAST_S,attack=true,
        self_cast=true,nova=true,kite_only=not defensive}
    end
  end
  if now()>=(c.escape_retry_until or 0) then
    local dodging=incoming>0.25 and incoming-(escape.missile_there or 0)>=0.25
    local relevant=dodging or not global and exposed>=ESCAPE_EXPOSURE
    if relevant and route_legal(c,me,escape) and ((escape.danger_here or 0)-(escape.danger_there or 0)>0.05) then
      add{name='escape',family='move',spot=escape}
    end
    if not global and target and route_legal(c,me,approach) then
      -- Closer by a real step, or the step that brings an out-of-reach target into reach.
      local ends=world.distance(approach,target)
      local useful=ends<distance-0.5 or distance>range and ends<=range
        or (c.misses or 0)>=3 and world.distance(me,approach)>=4
      if useful then add{name='approach',family='move',spot=approach} end
    end
  end
  add{name='wait'}
  -- The most damage a point of mana buys: the leading volley from its
  -- distance, Static Field on the target once in reach, or what is on offer now.
  local frugal=best and not shortage and best.total/math.max(1,best.skill.mana) or 0
  if static_ok and target then
    local full=value.life(target)
    frugal=math.max(frugal,math.min(target_life*STATIC_SHARE,math.max(0,target_life-full*floor/100))
      *value.through(value.resist(target,'lightning'))/math.max(1,static.mana))
  end
  for _,a in ipairs(actions) do
    if a.skill and (a.skill.mana or 0)>0 then frugal=math.max(frugal,(a.damage or 0)/a.skill.mana) end
  end
  -- An attack is judged by the cheaper of two plans. Staying: the fight is
  -- finished here (outlast) under the full exposure window. Striking, with a
  -- way out: one cast, exposed for STRIKE_S, and then what leaving costs
  -- anyway. The step away is then owed: until it is taken, the next cast in
  -- danger counts in full again.
  local full=lethality(threats,me,me.life)
  if self.struck and (now()-self.struck>STRIKE_MS or full<STRIKE_CLEAR) then self.struck=nil end
  local way_out
  for _,a in ipairs(actions) do
    if a.name=='escape' then way_out=a.spot end
  end
  local leaving=way_out and pressure(threats,way_out)*EXPOSURE_S or 0
  local function strike_cost(chilled) -- share of our remaining life
    return (pressure(threats,me,chilled)*STRIKE_S+leaving)/math.max(1,me.life)
  end
  -- Staff work she can afford only with the healing in her belt begins
  -- with a potion (the maintainer's rule: "if she's kiting because of
  -- health, but otherwise would melee, she needs to drink a potion anyway
  -- so she can get back in the fight"). combat:drink reads the mark.
  if staff then
    for _,a in ipairs(actions) do
      if a.name=='melee' or a.name=='approach' then
        local at=a.spot or me
        local d=a.spot and world.distance(a.spot,target) or distance
        if outlast(at,d)>0 and outlast(at,d,me.life)<=0 then c.heal_first=now() end
      end
    end
  end
  local committed=duel and me.life>=DUEL_FLOOR*me.max_life
  local striking=duel and way_out and (committed or not self.struck) and (stay<=0 or 1-stay>strike_cost())
  self.strike=striking and full>=STRIKE_CLEAR
  local rows={}
  for i,a in ipairs(actions) do
    rows[i]=self.scorer:row{
      tempo=best_rate>0 and a.rate/best_rate or 0,
      finish=ratio(a.target_damage or 0,target_life),progress=a.progress or 0,
      lethality_after=a.spot and lethality(threats,a.spot,me.life)
        or a.attack and striking and strike_cost(a.nova)/(1+strike_cost(a.nova))
        or lethality(threats,me,me.life,a.nova),
      outlast=(a.name=='wait' or a.attack and striking) and 0
        or a.spot and outlast(a.spot,target and world.distance(a.spot,target) or 0) or stay,
      missile_after=(a.spot and (a.spot.missile_there or 0) or incoming)/2*harm,
      move=a.spot~=nil,time=a.seconds or 0,
      mana=ratio(me.mana,me.max_mana),mana_cost=ratio(a.skill and a.skill.mana or 0,me.max_mana),
      thrift=a.skill and (a.skill.mana or 0)>0 and frugal>0 and ratio((a.damage or 0)/a.skill.mana,frugal) or 1,
      nova=a.nova,target_chill=target_chill,wait=a.name=='wait',engaged=target~=nil and not global,
      continuity=self.last_family==a.family and self.last_target==(target and target.id),
      -- kite_intent stays 0 here; rank() raises it for the second pass.
      target_priority=(a.damage or a.name=='approach') and priority or 0}
  end
  return actions,rows,shortage,has_spells and not best and not melee_allowed,exposed
end

function neural:rank(actions,rows)
  -- Ask the same model whether it wants to escape before rewarding chill setup.
  local baseline,indices={},{}
  for i,row in ipairs(rows) do
    self.scorer:set(row,'kite_intent',0)
    if not actions[i].kite_only then baseline[#baseline+1],indices[#indices+1]=row,i end
  end
  local selected,values=self.scorer:rank(baseline)
  local index,scores=indices[selected],{}
  for i,value in ipairs(values) do scores[indices[i]]=value end
  if actions[index].name=='escape' then
    for _,row in ipairs(rows) do self.scorer:set(row,'kite_intent',1) end
    index,scores=self.scorer:rank(rows)
  end
  return index,scores
end

function neural:choose(c,me,target,global,heading)
  if not global and c.target_progress and now()-c.target_progress>30000 then
    c.ignored[target.id],c.target_id=now()+30000,nil
    c.resting=false
    print('neural: skipping target after 30 seconds without damage '..tostring(target.id))
    return false
  end
  local actions,rows,shortage,immune,exposed=self:candidates(c,me,target,global,heading)
  if not global then
    c.resting=shortage and not c.melee_ok
    c.attack_mana=self.attack_mana or c.attack_mana
    -- The projection of the fight in hand, for the survival rules and the potions.
    c.outlast,c.outlast_at,c.mana_need=self.stay,now(),self.stay>0 and self.stay_mana or nil
  end
  if immune and not global then
    c.ignored[target.id],c.target_id=now()+30000,nil
    print('neural: skipping immune target '..tostring(target.id))
    return false
  end
  local index,scores=self:rank(actions,rows)
  local a=assert(actions[index],'policy.rank returned an invalid candidate')
  -- The travel-time missile check has nothing to report while it only waits.
  if (self.logged~=a.name or now()-(self.logged_at or 0)>=1000) and not (global and a.name=='wait') then
    local summary={}
    for i,candidate in ipairs(actions) do
      summary[#summary+1]=candidate.name..'='..(scores[i] and string.format('%.2f',scores[i]) or 'kite setup only')
      -- A move turned down flat says why: how long the walk, what waits at its end.
      if candidate.spot and scores[i] and scores[i]<=-5 then
        summary[#summary]=summary[#summary]..string.format('{%.1fs lethal %.2f missile %.2f}',candidate.seconds or 0,
          self.scorer:get(rows[i],'lethality_after'),self.scorer:get(rows[i],'missile_after'))
      end
    end
    print(string.format('neural %s: %s target=%s at %.1f life=%.2f mana=%.2f exposed=%.2f%s outlast=%.2f incoming=%.2f [%s]',
      self.version,a.name,tostring(target and target.id),target and world.distance(me,target) or -1,
      ratio(me.life,me.max_life),self.scorer:get(rows[index],'mana'),exposed,
      (self.harder or 1)>1 and string.format(' (x%.1f measured)',self.harder) or '',self.stay or 0,
      self.scorer:get(rows[#rows],'missile_after'),table.concat(summary,',')))
    self.logged,self.logged_at=a.name,now()
  end
  if a.name=='wait' then
    if not global or c.plan and c.plan.missile then c.plan=nil end
    if not global and not shortage then
      local away=world.distance(me,target)
      if self.wait_target~=target.id or not self.wait_at then
        self.wait_target,self.wait_at,self.wait_from=target.id,now(),away
      end
      if now()-self.wait_at>5000 then
        c.ignored[target.id],c.target_id=now()+30000,nil
        -- From beyond her reach: see combat:usable.
        c.unreached=c.unreached or {}
        c.unreached[target.id]=world.distance(me,target)>c.build.range or nil
      end
    end
    return false
  end
  if a.spot then
    -- Reuse accepted routes until the next search, retaining follow_plan's real
    -- progress budget and refusal handling rather than renewing it each tick.
    if not c.plan or not c.plan.neural or c.plan.x~=a.spot.x or c.plan.y~=a.spot.y
      or now()-c.plan.since>600 then
      c.plan={x=a.spot.x,y=a.spot.y,path=a.spot.path,since=now(),from=a.spot.danger_here,
        start={x=me.x,y=me.y},lead=0.5,occupied=1.5,why='neural '..a.name,
        missile=global,pressure=a.name=='escape',neural=true}
    end
    local accepted=c:follow_plan(me)
    if accepted then
      self.last_family,self.last_target=a.family,target and target.id
      -- A dodge that brings her no nearer does not start the wait over: with
      -- a shaman in sight across ground she could not cross, every dodge of
      -- its fireballs did, and she stood there until the thirty-second rule.
      if not (a.name=='escape' and target and self.wait_target==target.id and self.wait_from
        and world.distance(me,target)>self.wait_from-1) then
        self.wait_at,self.wait_target=nil,nil
      end
    end
    return accepted
  end
  c.plan=nil
  local skill=a.skill and a.skill.id or 0
  local hand=a.melee and 'left' or 'right'
  if (hand=='left' and me.left_skill or hand=='right' and me.right_skill)~=skill
    and not act.select_skill(hand,skill) then error('neural: could not select skill '..skill) end
  local accepted
  if a.self_cast then accepted=act.cast(hand,math.floor(me.x),math.floor(me.y))
  else accepted=act.cast_at(hand,target,not a.melee) end
  if accepted==false then return false end
  c.last_action,c.escape_progress=now(),nil
  if self.strike and not global then self.struck=now() end
  if global and a.self_cast then c.dodge_cast=true end
  self.last_family,self.last_target=a.family,target and target.id
  self.wait_at,self.wait_target=nil,nil
  if a.nova then self.nova_at=now() end
  if not a.self_cast and not a.melee then c.misses=c.misses+1 end
  return true
end

function neural:fight(c,me,target)
  c:observe_missiles(me)
  if not c:ground_clear(me) then c:dodge(me,target); return 'fighting' end
  self:choose(c,me,target,false)
  return c.resting and 'resting' or 'fighting'
end
function neural:dodge(c,me,target,passage_heading,retain_engagement)
  -- Global travel never attacks or approaches a target. A retained target is
  -- diagnostic context only; passage intent is passed to the native search.
  if #(c.missile_lanes or {})==0 then
    if c.plan and c.plan.missile then c.plan=nil end
    return false
  end
  if not target and not passage_heading and c.target_id then
    local fresh=game.unit('monster',c.target_id)
    if fresh and c:usable(fresh) and world.distance(me,fresh)<=
      (retain_engagement and c.engagement_radius or c.build.range) then target=fresh end
  end
  local heading=passage_heading and passage_heading(me)
  -- With a fight in hand the step out of a lane is the fight's to take or
  -- leave: it sets the step against the attack it could make instead. Asked
  -- here, where the only other choice is to wait, the step won every time,
  -- and boxed in by a pack that spat lightning she gave every other action
  -- to a sidestep that left her in as much danger as before: "all she could
  -- do was dodge". So with a target engaged the fight's own ranking is asked
  -- first, and this check acts only when that, too, would step away.
  if target and c.target_id==target.id then
    local actions,rows=self:candidates(c,me,target,false,heading)
    local chosen=actions[self:rank(actions,rows)]
    -- (A Nova cast in defence is this check's own to make, as before.)
    if chosen and chosen.name~='escape' and not chosen.nova then
      if c.plan and c.plan.missile then c.plan=nil end
      return false
    end
  end
  return self:choose(c,me,target,true,heading)
end
return neural
