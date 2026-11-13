# YARB: Charon Lua scripting

Charon embeds LuaJIT 2.1 to automate Diablo II, both in the menus and in game.
Two scripts can run at once, each in its own Lua state:

```
Game.exe -loaddll Charon.dll -oogscript scripts\oog\create_character.lua -gamescript scripts\game\clear_blood_moor.lua
```

| option | meaning |
|---|---|
| `-oogscript <file>` | Runs while out of game, for the whole session. |
| `-gamescript <file>` | Loads fresh when a game starts and unloads when it ends. |
| `-luaffi` | Lets scripts `require 'ffi'`. Off by default: FFI gives scripts unchecked memory access. |

Relative paths resolve from `Game.exe`'s folder. `require` searches the script's
folder and its parent, so `scripts/game/x.lua` can `require 'lib.fsm'`.

The API reads only client data and sends only the requests the client itself
sends, so scripts work the same in single player and in TCP/IP games.

## Writing a script

Scripts never sleep or wait. Charon calls the script's global functions:

| function | when |
|---|---|
| `tick(frame, ms)` | In a game: once per game frame (one step of the game's logic, 25 a second at the stock rate). In menus: 25 times a second. `frame` counts ticks since load; `ms` is `charon.now()`. |
| `on_<event>(...)` | Before `tick`, for each queued event (reserved for future event sources). |

Keep `tick` short; a tick over 8 ms logs a warning. Long-running behaviour is a
state machine advanced by one step per tick; `lib/fsm.lua` provides one.

When a script raises an error, Charon logs the error and its stack trace and
stops calling that script; the game carries on. A script reloads automatically
when it or any module it required changes on disk. In game, `/lua reload` and
`/lua stop` control the game script, and `/lua mem` reports memory use (see
`charon.memory()`). Script output, including `print`, `charon.log`, errors and
timing warnings, goes only to `%APPDATA%\Charon\lua.log`. Explicit `/lua mem`
requests also display their result in game.

Lua's `os.exit` ends the whole game process.

## Shared modules

### `charon`

| call | returns |
|---|---|
| `charon.context()` | `'oog'` or `'game'` |
| `charon.frame()` | current tick count |
| `charon.now()` | the script clock in milliseconds. In a game it is game time: 40 ms per game frame, however fast the game runs (single player steps its server in lockstep with the frame rate, so a higher target rate plays the same game faster). Outside a game it follows the wall clock. Time every game mechanic with it |
| `charon.wall()` | wall-clock milliseconds since Charon started, for measuring how long something really took. Nothing in a game is timed with it: the client's sender drops a request identical to the one before it within 200 ms (50 ms for skill and attack requests), and Charon runs that window at the game's pace in single player, so identical repeats, such as buying the same potion twice, are spaced on `charon.now()` |
| `charon.log(text)` | same as `print` |
| `charon.overlay(lines)` | a table of up to 12 strings shown in the upper right corner of the game screen while the automap is off; `nil` or `{}` clears it. The lines stay until replaced, across games |
| `charon.memory()` | bytes in use: `oog` and `game` (each script's Lua heap, live objects plus uncollected garbage; absent when that script is not loaded), `map` (the `map` module's layout and coverage caches) and `process` (Game.exe's private bytes, game included) |
| `charon.version` | Charon version string |

### `store`

A process-wide key/value store shared by both scripts (strings, numbers, booleans).
The out-of-game script can hand facts to the game script and back.

`store.get(key)`, `store.set(key, value)`. Setting `nil` removes the key.

## `oog`: menus

| call | returns |
|---|---|
| `oog.screen()` | `'main_menu'`, `'character_select'`, `'create_class'`, `'create_name'`, `'difficulty'`, `'popup'`, `'transition'`, `'none'` or `'unknown'` |
| `oog.controls()` | list of every control on screen (below) |
| `oog.click(control)` | presses a button, class portrait or character slot like a mouse click; `false, reason` if disabled, inactive or behind a popup |
| `oog.set_text(control, text)` | fills an edit box |
| `oog.focus(control)` | gives an edit box keyboard focus (takes effect next frame) |
| `oog.key(vk)` | sends a virtual key to the menus (Enter `13`, Escape `27`) |
| `oog.type(text)` | types characters into the focused edit box, validated as if typed |
| `oog.characters()` | saved characters: `name, class, class_id, level, hardcore, dead, expansion` |
| `oog.create_state()` | new-character settings: `class_id, hardcore, expansion, selected_class` |

Each control is a table with `id, index, type, x, y, width, height, enabled,
active, modal, focused, text` and, for controls the game uses for character
creation and selection, a `role`: `single_player`, `create_new`,
`character_slot`, `select_ok`, `class` (with `class`, `class_id`,
`anim_state`), `name`, `create_ok`, `hardcore`, `expansion`,
`hardcore_confirm`, `normal`, `nightmare`, `hell`. Pass the table (or its
`id`) back to `click`; a control that left the screen raises an error. Act once
per tick and read the screen again on the next tick: menus change after each
action.

Valid character names have 2 to 15 letters with at most one `-` or `_`, not
first or last; `lib/names.lua` checks and generates them.

Character-slot controls expose `character_name` from the saved-character entry
associated with the visible slot, independently of joined display rows/cells.

## `game`: what the client knows

Units are plain snapshot tables; refer to a unit later by `(type, id)` or by
passing its table back. Positions are world subtiles (5 per tile).
Positioned units also expose `area` from their client room's level (0 when
unavailable); carried items have no world position or area.

| call | returns |
|---|---|
| `game.ready()` | the player exists and stands in a room |
| `game.players()` | `{supported, configured, revision}` for Charon's local player-count configuration. `configured` is absent on unsupported modes, including joined TCP/IP clients. `revision` changes only when the native client accepts a `/players` command; applying a saved setting does not acknowledge a command. This is Charon-owned configuration, not server memory, remote-host state or the scaling of existing monsters |
| `game.me()` | the player: `name, class, level, x, y, area, act, in_town, difficulty, expansion, life, max_life, mana, max_mana, stamina, experience, gold, stat_points, skill_points, strength, dexterity, vitality, energy, left_skill, right_skill, alive, mode, can_act, cursor_item` |
| `game.units([type])` | every unit the client tracks, or one type: `'player'`, `'monster'`, `'object'`, `'missile'`, `'item'`, `'warp'` |
| `game.unit(type, id)` | one unit or `nil` |
| `game.stat(type, id, stat [, layer])` / `game.base_stat(...)` | a stat (life, mana and stamina are fixed point ×256) |
| `game.state(type, id, state)` | whether a unit has a state |
| `game.items()` | the player's items: equipped, inventory, belt, stash, cube and cursor |
| `game.skills()` | the player's skills: `id, name, level, base_level, left, right, usable, mana, charges, owner` |
| `game.skill_info(id)` / `game.class_skills([class_id])` | Skills.txt facts: `name, class_id, required_level, max_level, prerequisites, passive, aura, element, state` (the state it puts on the caster, 0 for none) |
| `game.skill_damage(id [, level])` | `physical_min/max, elemental_min/max, element, to_hit_bonus, mana`, as the skill tree computes them for the player |
| `game.attack_damage()` | character-screen damage of the equipped weapon: `min, max` |
| `game.ui([panel])` | whether a panel is open, or a table of open panels (`inventory`, `npc_menu`, `npc_shop`, `waypoint`, `stash`, `cube`, `escape_menu`, `skill_tree`, `character`, ...) |
| `game.area()`, `game.level_name(id)` | current level id; localized level name |
| `game.locale(id)` | a localized string |
| `game.to_screen(x, y)` | screen pixel of a world position |
| `game.quest(id)` | quest state from the client's quest record: `completed` (reward collected), `reward_pending`, `completed_this_game`, `blocked_this_game`, `started`, `bits`. Act 1: 1 Den of Evil, 2 Sisters' Burial Grounds, 3 Tools of the Trade, 4 Search for Cain, 5 Forgotten Tower, 6 Sisters to the Slaughter |
| `game.quest_revision()` | process-wide count of full quest records applied by the client (server packet `0x28`, update type 6). Capture before requesting a refresh and poll for a different value on later ticks; unchanged quest bits still advance the count. Survives script reloads and game changes |
| `game.waypoints()` | `{ [level] = true }` for waypoints activated on this difficulty, as of the last waypoint panel opened (`nil` before any) |
| `game.level_info(id)` | Levels.txt facts for the current difficulty: `name, act, monster_level, density, unique_packs_min/max, waypoint, inside, monsters, unique_monsters` (spawnable class ids) |
| `game.hit_chance(class [, level])` | `percent, block`: the chance a blow from a monster of that class lands on the player (its to-hit against her defence and both levels, held between 5 and 95, as the character screen and the hit roll work it out), and her chance to block |
| `game.monster_info(class [, level])` | MonStats facts: `name, level, own_level, min_life, max_life, experience` and physical damage per hit `attack_min/max, attack2_min/max, skill_min/max` (all scaled with MonLvl.txt as the game does), `melee`, `ranged`, `threat` (MonStats.txt's own target ranking), `resist` (`physical, magic, fire, lightning, cold, poison`), `boss, npc, undead, demon, killable, speed, cold_effect`. `cold_effect` is the signed slowing percentage for this difficulty; negative values slow, zero means no chill slowing, separately from cold resistance |
| `game.object_info(class)` | Objects.txt facts for a class id: `code, door, shrine, waypoint, subclass, operate, open_warp` |
| `game.difficulty_info()` | DifficultyLevels.txt facts: `difficulty, static_field_min` |

Every unit has `type, type_id, class_id, id, mode, flags, x, y` (no position for
carried items). Monsters add `name, alive, attackable, life_percent, npc,
interactable, unique, champion, superunique, minion, boss, undead, demon,
mercenary, owner, enchants`. Objects add `name, code, door, shrine, subclass,
operate, open_warp`. Items add `name, code, quality, location` (`container`,
`equipped`, `belt`, `ground`, `cursor`, `socketed`), `page, body_location,
grid_x, grid_y, item_level, identified, ethereal, socketed, runeword, width,
height`, plus `staff_mods` (the class whose skills the item type can roll) and
`class_only` (the class it is restricted to) from ItemTypes.txt when set. Players
add `name, class, level, alive`.
Missiles add `owner_type, owner` when known. Verified damaging ground-fire
effects add `ground_fire=true, ground_size` (1 point, 2 cross, 3 square).
This includes Fire Wall's two alternate client sprites; harmless blood and
decorative flames do not receive the flag. Ownership still determines whether
the effect threatens the player; absent ownership does not establish safety.

## Policy networks

YARB's bots make two kinds of choice with small neural networks whose weights
are written by hand, not trained: which combat action to take this tick
(`combat-v4`) and whether to prepare in the Forgotten Tower or push on
(`strategy-v2`). Both use the same pieces.

**Native scorer.** `policy.compile(weights)` validates and copies a network
into a model userdata and returns `model, inputs, hidden`. A network is
`inputs → hidden ReLU → 1 score`; its shape comes from the tensors
(`hidden = #b1 = #w2`, `inputs = #w1 / hidden`, each at most 64). `w1` is a
flat hidden-row-major array. Weights must be finite numbers in −1000…1000 and
the arrays dense; when the table has `features`, it must name every input.
`policy.rank(model, rows)` takes 1–64 dense rows of `inputs` finite numbers in
0…1 and returns the one-based index of the best row and every score. The first
candidate wins ties. Inference runs synchronously in C++ on the game thread
and does not read game memory or use FFI.

```
h = ReLU(W1 x + b1)
score = W2 h + b2
choice = first argmax(score over the legal candidates)
```

**Named features (`lib/scorer.lua`).** Scripts never index a feature row by
position. `scorer.new(weights, produced)` compiles the artifact and checks
that the features the caller produces are exactly the ones the artifact names;
`scorer:row{ name = value, ... }` builds a row (values clamped to 0…1, booleans
as 1/0, anything left out 0) and `scorer:rank(rows)` scores them. A renamed,
added or dropped input fails at load instead of silently reading zero, and the
order of inputs in an artifact does not matter to the code that fills them.

**Direct weight authoring.** `tools/author_policy_weights.py` holds both
networks as named equations and writes `scripts/lib/combat_weights.lua` and
`scripts/lib/strategy_weights.lua`:

```
python tools/author_policy_weights.py           # write the artifacts
python tools/author_policy_weights.py --check   # verify they match the source
```

```
score = bias + Σ BASE[f]·f + Σ out·ReLU(Σ coef[f]·f + gate_bias)
```

The first hidden units pass each input through unchanged (inputs are 0…1, so
ReLU is the identity on them) and carry the `BASE` preferences. One more hidden
unit per `GATE` encodes an interaction. The generator assigns the numbers
directly; there is no data set, optimizer or search. To change a policy, edit
the named coefficients, regenerate, and run the regressions. The artifacts are
ordinary modules, so a running script reloads them.

These are authored heuristics. They encode current hypotheses about good play
and are judged by end-to-end runs, not by a loss.

### Combat (`combat-v4`)

Every `combat.new(build)` ranks its legal actions with this network: bolts,
Static Field, Frost Nova, staff or weapon attacks, Bash, an escape route, an
approach route, and waiting. Target acquisition, potions, quest routing,
emergency portals and chicken stay outside it. Missile avoidance during travel
uses the same weights with attacks and approaches left out. Standing in known
ground fire is an immediate escape, before any ranking.

Each candidate is described by what it is expected to achieve
(`lib/neural_combat.lua`):

| input | meaning |
|---|---|
| `tempo` | its damage rate as a share of the best rate on offer. For a move: the rate from where it ends, for what remains of a 3 s horizon |
| `finish` | share of the target's remaining life it removes now |
| `progress` | for an approach that starts out of casting range: share of the gap it closes |
| `lethality_after` | how much of our remaining life the monsters in reach would take from where the action leaves us, as `x/(1+x)` of that share `x` (0.33 = half, 0.5 = all of it). Over 4 s for a position; for a strike, see below |
| `outlast` | share of what a fight may cost (life plus part of the healing in hand) that is left once the threats in reach are dead, fighting from where the action leaves us; 0 when the fight is not decided there |
| `missile_after` | projectile danger where the action leaves us (native lane danger / 2), scaled by the share of our remaining life such a missile takes (a quarter or more counts in full) |
| `move`, `time` | whether it walks; seconds it commits (capped at one) |
| `mana`, `mana_cost`, `thrift` | mana fraction; the cast's cost / maximum mana; its damage per mana as a share of the best the mana could buy (1 for what costs none) |
| `nova`, `target_chill`, `kite_intent` | Frost Nova; the target is unchilled, chillable, in Nova's reach and in sight; the first ranking chose escape |
| `continuity`, `target_priority`, `wait`, `engaged` | same action family and target as last time; elite or resurrector target; doing nothing; a fight is on (not the travel-time missile check) |

**Damage dealt** comes from the client's skill and monster data. A volley of
Charged Bolts fans out: all `level + 2` bolts connect within 2.5 subtiles and
proportionally fewer beyond, stray bolts find the target's neighbours and
whatever stands between us and it, a melee monster walks into a volley already
in flight, and every recent cast that did no damage discounts the estimate
from here. Damage is capped by the life it can still remove, so two attacks
that both kill are equal and the cheaper wins. An act boss's row carries the
boss's own life and damage; other uniques are scaled from their class.

**Damage taken** comes from `value.dps`: the monster's stronger MonStats
attack, scaled by MonLvl as the game does, about once a second; elites hit
harder. A melee monster threatens fully within 2 subtiles and not at all beyond
9; a shooter in sight counts half wherever we stand (dodging handles its
missiles); a chilled monster keeps 60% of its rate and, walking at half speed,
reaches half as far. Only monsters in line of sight count. The tables know a
blow, not spells, poison or enchantments, so the model is corrected by what
is measured: when life is lost faster than the monsters in reach account for,
they all count that much stronger (up to three times) for as long as it lasts,
and a large discrepancy is remembered for the session against the class that
was taking most (`value.learn`, up to four times). What every session measured
the same way is known from the start (`MEASURED` in `value.lua`): the Tainted
and the Afflicted spit lightning, a missile whose damage the attack columns do
not hold, and took 2.2 to 4 times their table in each of six runs. Because the
share is of the life that *remains*, the same pack matters more as life drops.

**Two kinds of fight are settled where she stands instead of kited.** A duel:
an act boss must die, cannot be shed and has more life than any kite is worth.
And staff work: out of mana where the campaign allows swinging (areas whose
monsters are level two at most), waiting for mana costs more time than
clubbing what dies in a few blows and hurts little. For every other fight
`outlast` is 0 and exposure counts in full: with spells in hand against packs
these plans were tried and withdrawn, because every one of eighteen sudden
losses of life in a run followed an attack they had allowed at an exposure of
0.3 or more.

- *Staying.* The fight is finished where we stand: each cast is the better
  of Static Field (a quarter of the current life of everything in its reach)
  and the best volley, until the threats in reach are dead, with a quarter
  added for what goes wrong. `outlast` is the share of the life budget that
  leaves; it is 0 when the fight needs more than 14 casts or more mana than
  the pool and the potions carried. The exposure thresholds below move up
  with it: Andariel alone at 88% died in 14 seconds of this. Staff work is
  budgeted in life alone (its point is to save the gold that potions cost)
  and walks straight up to its target when no safe route ends beside it, so
  three Fallen are clubbed and a pack that needs many blows or hits hard is
  left to the kite as before.
- *Striking* (duels only). With an escape route on offer, one cast costs the exposure of
  that cast and the first steps away (1.2 s) plus what leaving costs anyway.
  The step away is then owed: until the exposure has dropped or 2.5 s have
  passed, the next cast in danger is judged in full again. This is how a boss
  with a thousand life that outruns us is fought: Static Field when she
  arrives, away, again. With at least 60% of our life in hand the step is not
  owed and every cast is a strike: stepping away from a boss that outruns us
  buys nothing, and one strike in 2.5 s took thirty seconds beside Andariel
  for three quarters of her life.

Weights: `6·tempo + 2·finish + 2·progress − 14·missile_after − 0.4·move −
0.5·time − 0.5·mana_cost − 0.1·nova + 0.15·continuity + 0.3·target_priority −
0.2·wait`, and these gates:

| gate | effect |
|---|---|
| `exposure` | `−14·ReLU(lethality_after − 0.6·outlast − 0.1)`: free up to a ninth of the life that is left, so weak monsters are no reason to move |
| `heavy_exposure` | `−30·ReLU(lethality_after − 0.6·outlast − 0.2)`: steepens so that a real escape wins near 0.3. In live play nearly every large loss of life followed a choice made at 0.3 or more |
| `grave_exposure` | `−14·ReLU(lethality_after − 0.6·outlast − 0.4)`: steepens again past two thirds of the life that is left |
| `idle_in_a_fight` | `−1.5·ReLU(wait + engaged − 1)`: standing idle with a target achieves nothing |
| `scarce_mana` | `−3·ReLU(mana_cost − 0.35·mana + 0.1)`: a last sliver of mana is not spent on a weak cast |
| `wasted_mana` | `−12·ReLU(0.5 − mana − thrift)`: below half a pool, mana waits for what it buys most with (Static Field on a boss about to arrive, not a volley from where one bolt in ten connects) |
| `chill_before_kite` | `+24·ReLU(nova + target_chill + kite_intent − missile_after − 2)`: chill a reachable target before kiting it |

The ranking runs twice at most. The first pass leaves out a Nova whose only
purpose is a kite setup. If that pass chooses escape, `kite_intent` is set on
every candidate and all are ranked again, so an unchilled target in reach is
chilled first. Nova's cooldown, mana and usability still apply.

**Legality and execution.** Lua offers only usable, affordable skills in range
and line of sight, and routes whose every segment is clear of ground fire. A
weaker bolt is masked while the strongest learned bolt is unaffordable (the
build does not fall back to a one-point skill); staff hits are offered where
the campaign allows them. Native path finding supplies the escape and the
approach, cached for up to 120 ms. Animation ownership, refused requests and
movement progress budgets stay in the executor (`lib/combat.lua`), which also
publishes the policy's projection of the fight in hand: `combat:winning(margin)`
for the survival rules, and the mana the fight needs, so that a mana potion
is drunk at its first cast rather than when the pool is empty.

Every changed choice, and an unchanged one once a second, logs the version,
target and its distance, life, mana, exposure (with the measured correction),
`outlast`, missile danger and every candidate's score, so a bad decision can
be traced to the candidates, the inputs, the weights or the execution.

### Tower strategy (`strategy-v2`)

At campaign decision points one network compares optional Forgotten Tower
preparation with the next main-route area after Black Marsh. Mandatory quests,
town services, corpse recovery, supplies and emergencies keep their priority.

Normal sorceresses become eligible after the Den, from level five, once Black
Marsh is reached, its waypoint is known, or a descent is under way. A descent
in progress is committed. Level eleven, three failed searches or no route make
the Tower unavailable. Choosing progress ends preparation for this character
and session; inside the Tower it leaves the game to resume from town.

| input | meaning |
|---|---|
| `progress`, `tower` | which candidate this is |
| `bolt_gap`, `control_gap`, `level_gap` | `(4 − bolt_rank) / 4`; missing Nova and Static (half each); `(10 − level) / 4`: preparation is favoured to about level nine, less as its wall time grows |
| `xp_value`, `travel_cost`, `reset_cost` | the candidate's share of both `leveling.prior` values; route hops / 6; the Tower needs a new game |
| `first_clear`, `commitment` | the Countess was never killed; a descent is under way |
| `failures`, `tower_time` | failed searches / 3; wall time since preparation began / 600 s |

The priors are estimated experience per damage times area density: comparable
area values, not measured rates. The network rewards missing controls, bolt
ranks, early levels and a first clear for the Tower, and charges travel,
resets, failed attempts and time already spent. Each changed decision logs both
utilities and their inputs.

## `act`: doing things

Calls return immediately; watch `game` to see the result. Movement, attacks and
interaction go through the client's own command path, so the character animates
exactly as for a mouse click.

| call | does |
|---|---|
| `act.ready()` | the player can start a new action now |
| `act.players(count)` | submit only `/players N` through the native client command path; exact integer 1–8, supported single-player/local-host mode and active player required. Returns whether a new native-command acknowledgement matches the requested count. Like Charon's manual command, this saves the setting and persists across games; local test harnesses must back up and restore settings |
| `act.move(x, y [, walk])` | walk or run toward a point, replacing a queued interaction as a new ground click does (short range; use `map.path` for long routes); returns whether the native local path search succeeds, not whether movement completes |
| `act.move_to(unit [, walk])` | run toward a unit |
| `act.select_skill(hand, id [, owner])` | put a skill on `'left'` or `'right'` |
| `act.cast(hand, x, y)` / `act.cast_at(hand, unit [, stand_still])` | use the hand's skill on a point or unit |
| `act.interact(unit)` | talk, open, or use a waypoint, portal or stairs (stand next to it) |
| `act.pickup(item [, to_cursor [, nearby_only]])` | request a ground pickup; returns whether a request was sent. `nearby_only` checks live client ground mode, rooms, native range and wall/door collision before sending; it does not confirm receipt or guarantee server range under latency |
| `act.use_belt(slot [, mercenary])` | use belt slot 0–15 (0–3 is the bottom row) |
| `act.add_stat(stat [, count])` | spend stat points: 0 strength, 1 energy, 2 dexterity, 3 vitality; count must be 1..unspent points |
| `act.add_skill(id)` | spend a skill point |
| `act.waypoint(level)` | travel from the open waypoint panel (interact with a waypoint first) |
| `act.use_item(item)` | right-click an inventory item: town portal scroll or tome, potions |
| `act.to_belt(item)` | move a potion from the inventory to the belt |
| `act.pick_item(item)` / `act.place_item(item, page, x, y)` | lift an item onto the cursor; put the cursor item into a cell of `inventory`, `cube` or `stash` (cube and stash must be open) |
| `act.identify(item, scroll)` | read a scroll of identify (`isc`) on an unidentified item she carries; the cursor must be empty |
| `act.equip_action(item, slot [, ignore_requirements])` | what putting `item` into a body slot would do, by the client's own check: `0` it does not go there or she cannot wear it, `1` into the empty slot, `5` swap with what is worn, `2`/`7` a two-handed weapon in place of one or two held items. Slots: 1 head, 2 amulet, 3 body, 4 right hand, 5 left hand, 6 right ring, 7 left ring, 8 belt, 9 boots, 10 gloves. The item may be in the pack, on the ground or in a shop |
| `act.equip(slot)` | put the cursor item into a body slot as a click on the inventory screen does; returns `sent, action`. What was worn there comes back on the cursor: place it with `act.place_item` |
| `act.transfer_gold(way, amount)` | `'deposit'` inventory gold into the open stash or `'withdraw'` it; stash gold survives death (`game.me().stash_gold`) |
| `act.leave_game()` | leave the game, as the escape menu's Save and Exit does; the menus return (close the escape menu first) |
| `act.key(vk)` | press a key in the game window |
| `act.quest_log(open)` | open or close the quest panel through the native UI command, independently of key bindings. `open` must be a boolean. Returns whether the requested panel state is active; opening may be refused by native UI guards or its 250 ms reopen debounce. Only a closed-to-open transition requests fresh quest data; requesting an already-open panel leaves it unchanged. Poll `game.quest_revision()` for the reply |
| `act.send(bytes)` | send a raw client request |

## `map`: layout and path finding

Levels of the current act are generated on the client from the game seed, so
the whole layout is available before it is explored. Path finding runs in C++.

| call | returns |
|---|---|
| `map.path(x, y [, {step=15, avoid={{x, y, radius}, ...}}])` | waypoints `{ {x, y}, ... }` from the player to a point, or `nil, reason`. `avoid` is ground to keep off (a dangerous pack): the route goes around every zone when such a way exists and is not much longer than the direct one (twice its cells plus 60), and the second result is then `true`. A zone that holds the player or the goal is ignored |
| `map.path_to_level(level [, {step=15, avoid=...}])` | waypoints that end just inside a neighbouring level; `avoid` as above |
| `map.rooms([level])` | rooms: `x, y, width, height, center_x, center_y` (walkable cell nearest the centre) |
| `map.presets([level])` | planned spawns: `type, class_id, x, y` |
| `map.exits([level])` | neighbouring levels: `level, kind` (`'warp'` with `x, y` for stairs and entrances, `'border'` for open ground) |
| `map.bounds([level])` | `x, y, width, height` |
| `map.walkable(x, y)`, `map.line_clear(x1, y1, x2, y2)` | collision queries |
| `map.line_of_sight(x1, y1, x2, y2)` | whether a missile can fly between two points (live collision: walls and closed doors block; end cells not checked) |
| `map.safe_spot{threats = {{x, y, weight}, ...}, target = {x, y}, range = 7, range_weight = 0.15}` | where to stand when monsters close in: `found, x, y, danger_here, danger_there, open_sectors, path`. Danger fades from each threat (assumed to keep closing in); cells beside a threat, slim gaps and live collision (monster footprints) are excluded, and routes need a corridor about a tile wide. `range_weight` (finite, 0-4) is the score cost per subtile beyond the target range; higher values favor staying near the target, but range remains a soft preference. `found` is false when nothing reachable is safer (trapped); `open_sectors` (0-8) counts the directions still offering a wide way out |
| `map.current_room()` | the player's room rectangle |
| `map.explore_plan([cell])` | a sweep of the player's level for exploring or clearing: `{ {x, y}, ... }` in walking order, and the planning time in ms. Reachable ground (one flood fill) is split into blocks of `cell` (12) subtiles ordered depth-first, dead ends first; linear in the level size, a few ms even for large areas |
| `map.explore_mark(x, y, radius)`, `map.explore_seen(x, y)`, `map.explore_reset([level])` | the level's coverage: cells seen from a point (walls block sight) |
| `map.explore_mark_rooms()` | marks the room the player stands in and the rooms the game keeps beside it, whose units the client holds: for a search after monsters that ground is done. Returns the cells newly marked |
| `map.explore_next([minimum])` | the nearest unseen walkable patch by walking distance: `x, y, steps, unseen, total`, or `nil` when nothing reachable is unseen |
| `map.explore_choose{minimum=24, cell=12, targets={{id,x,y},...}, exclude={{x,y},...}, heading_x=0, heading_y=0}` | adaptive selection by walking distance: always returns `found, unseen, total, gain, steps, elapsed_ms`; when found, also `kind` (`monster` or `patch`), `x, y`, and monster `id`. Reachable supplied targets take priority; otherwise the nearest connected unseen patch is chosen, where a patch that belongs to a large unsearched region counts as up to five times as far, so pockets beside the way are searched while they are close. `minimum=1` includes tiny remnants; exclusions temporarily suppress patch blocks without marking them searched. Maximum 512 targets and 64 exclusions; cell 4–40, minimum 1–1600 |
| `map.doors([level])` | door spawns `class_id, x, y`; paths go through doorways, so open the live door object (`act.interact`) when you reach it |
| `map.invalidate()` | forget cached layouts (after doors open, for example) |

`map.safe_spot` also accepts `missiles = {{x, y, to_x, to_y}, ...}`: up to
64 finite world-coordinate segments describing imminent missile flight.
Their danger is added along the route and at its endpoint; `missile_here` and
`missile_there` report that component separately (`missile_there` only when
`found`). Stationary segments contribute no flight danger.
`hazards = {{x, y, size}, ...}` accepts up to 128 confirmed ground effects with
finite world coordinates and integer size 1–3. Their floor-cell footprints are
expanded by the player's cross-shaped footprint. `hazard_here` and
`hazard_there` report overlap danger separately (the latter when `found`).
Routes cannot enter fire from clear ground, including diagonal corner crossings;
starting inside fire permits escape to a clear endpoint even if total monster
danger there is greater. `map.hazard_clear(x1, y1, x2, y2, hazards)` tests the
same footprints along a segment, including endpoints and touched corners.
A zero-length segment tests the point's floor cell. When checking native
waypoints, pass their cell centers (`floor(x)+0.5, floor(y)+0.5`).
Optional `heading = {x, y}` is a finite numeric direction vector, normalized
internally. It breaks ties between equal complete scores; it cannot outweigh
danger, travel cost or target range, or create movement without an improvement.
A zero vector keeps the default choice. It does not guarantee forward progress.
With `heading_weight` (0 to 1, default 0) each subtile of progress along the
heading adds that much to a spot's score, so a dodge made on the way somewhere
steps onward out of a missile's lane; the travel weight it is set against is
0.04 a subtile walked.

`lib/move.lua` follows these waypoints one tick at a time and plans again when
the character gets stuck. `lib/explore.lua` pursues loaded same-level hostiles
(those its `accept` function allows, when one is set), then uses
`map.explore_choose` for the next unseen patch: nearest first, pockets before
the open ground ahead, so that little is left behind to walk back to. It
retains a useful approach, with a small preference for continuing the
previous direction. On paper maps (`tests/pathfinding.cpp`) a branching cave
is searched in a quarter fewer steps than by ground gained per step, open
ground in a fifth fewer, and a hall with side rooms in about as many. The legacy sweep and nearest-frontier APIs remain available.

## `town`: vendors and mercenaries

Talk to an NPC with `act.interact` first; the client runs its own handshake and
opens the NPC menu (`game.ui('npc_menu')`). Requests return at once; read the
results back from `game.items()` and `game.me().gold`.

| call | does |
|---|---|
| `town.trade(npc)` | choose Trade (or Trade/Repair) in a vendor's open menu |
| `town.close()` | leave the shop or NPC menu the way the UI does |
| `town.respec(akara)` | Akara's one-time skill and stat reset (after the Den of Evil), from her open menu; the points come back in `game.me()` if it was available |
| `town.price(item, npc, mode)` | the client's price for `'buy'`, `'sell'` or `'repair'` |
| `town.sell_value(item [, npc_class=148])` | read-only client sale quote for a loaded ground or carried item; no shop required. Vendor classes: Akara 148, Charsi 154, Gheed 147. Returns `nil` if the item or client quest record is unavailable. Uses currently known item properties; identification can change the quote |
| `town.buy(npc, item [, fill])` | buy from the vendor's stock (`game.items(npc)`); `fill` is shift-buy |
| `town.sell(npc, item)` | sell an item from the inventory |
| `town.repair_all(npc)` | repair everything |
| `town.hire_offers()` | hirelings on offer after talking to a hireling NPC: `index, name, cost, level, life` |
| `town.hire(npc, index)` | hire an offer |
| `town.mercenary()` | our hireling: `id, class_id, level, life_percent, dead`, or `nil` |

`game.items(npc)` lists a unit's inventory, such as a vendor's stock once trade is open.

## Examples

- `oog/create_character.lua` creates a single player character with a random
  name and enters the game. It publishes the name as store `character`.
- `game/clear_blood_moor.lua` walks from the Rogue Encampment into the Blood
  Moor, explores every room and kills every hostile monster. It drinks potions,
  picks up gold and potions, spends stat and skill points, heals at Akara when
  out of potions, and recovers its corpse after a death.
- `oog/campaign.lua` + `game/act1.lua` play one character through Act 1 toward
  Andariel across as many games as needed (see the header of `act1.lua` for the
  plan). The libraries under `lib/` hold the reusable parts: `travel` (level
  routes, stairs), `combat` and `builds` (targeting, kiting, potions, point
  spending), `value` and `leveling` (what monsters and areas are worth to the
  build), `services` (healing, selling, potions), `loot`, `explore`, `menus`.

The campaign combat helper uses innate skills only (`owner == -1`); item-owned
skills and duplicate item grants are excluded from build selection, buffs and
point accounting. The native `game.skills` / `act.select_skill(..., owner)` API
continues to support item-owned skills. A failed innate attack selection stops
the script with a logged error; failed buff selection has a bounded retry delay.
Learned bolt damage distinguishes spell immunity from a temporary mana shortage;
unaffordable spells do not make the target immune.
Stash recovery returns `failed` after ten seconds if
its own pending item cannot be settled; services stops with an error and retains
the recovery state instead of reporting a successful visit.

Combat keeps a target while it remains useful, but fights enemies blocking its
approach before pursuing a distant priority target. Ranged attacks stop at casting
range. In generic fights at 90% life or better, a visible Shaman inside the
engagement radius takes priority over ordinary Fallen, including a weak retained
Fallen, even during mana-starved melee. Elite and other monster blockers, quest
focus, target-switch cooldowns and emergency escapes keep their existing priority.
Ranged attacks use stand-still casts; melee stays aggressive against individual
targets and uses the native safe-spot planner to flank a crowd while favoring
positions near striking range. Short escape routes preserve their corners and
are reassessed as enemies move. Ignoring an immune target never removes it from
the danger calculation.
Charged Bolt is cast from up to ten subtiles and its whole volley connects
within 2.5; other bolts have a seven-subtile cast limit. The approach on offer
ends where the volley connects. From beyond casting range, when that spot
would cost a fifth of her life (`lethality_after` of 0.2), a second search
offers a leg that ends where the farthest bolts reach instead: with only the
close spot on offer, a pack beside it kept a sorceress with full life and
mana waiting fifteen subtiles off until the stall rule left the game. The
network weighs that progress against current crowd, missile and exposure
risk, and a move it turns down flat is logged with its walking time and the
lethality and missile share at its end. A fixed number of
retreats never forces an attack. One-cell tactical routes use cell identity for
fractional player positions; movement refusal and accepted movement without
progress have bounded retry delays and preserve animation readiness.

Missile enumeration and ID lookup use D2Client's local-effects table; other
unit types use its server-synchronized client table. Combat samples
hostile-owned client missiles once per game frame and projects
their observed motion 600 ms ahead. `combat:dodge(me)` uses a short native search
to step off incoming flight lanes, then resumes fighting or travel once clear.
The campaign resolves its current travel threat before global dodging. A passage
with no accepted threat suppresses stale combat-target retention and supplies
an onward heading during missile arbitration. That heading uses
a fresh native path toward the current hop, preserving the mover's retry and
progress state; stale hops, closed doors and nearby warp interactions provide
no hint. Equal-score choices prefer that direction. Fighting supplies its
validated target explicitly; active casts and dodge plans keep ownership.
Measured flight survives repeated positions for at most 200 ms after the last
motion sample; later movement uses that position's timestamp to avoid velocity spikes.
It ignores friendly, unowned, never-moving, stale and wall-obscured observations;
it does not predict a shot before the missile appears or infer splash radius.
Verified persistent ground fire is tracked separately from observed flight,
including environmental effects and patches whose monster owner has died or
unloaded. Player, pet and hireling fire is excluded. Ground overlap takes
priority over another cast, including Frost Nova. Native escape and approach
routes avoid known patches; firing positions and short loot walks use the same
footprint checks. Effects remain hazards until fresh client snapshots remove
them. The nearest 128 patches within 24 subtiles bound the work.
The campaign checks dodging after its emergency exit/restocking decisions.

Action readiness gates both attacks and evasive moves. Skill selection and the
following cast share a tick because selection updates the client synchronously.
Self-centered Nova/Static casts are evaluated against the nearby pack, not the
distance to the retained target. A temporarily unaffordable main bolt does not
promote a weaker one-point backup; combat resumes once an effective attack is
available, and the campaign still owns potion restocking and chicken decisions.
Frost Nova competes on chill, elite control, damage and casting risk. A reachable
unchilled primary is eligible for chill setup before an intended retreat;
groups, elites and close threats while hurt also supply defensive opportunities.
Actual mana cost, readiness and the 1.2-second per-controller cooldown apply.
Global missile arbitration can select eligible defensive Nova, while retaining
projectile escape and exposure penalties. A retained target supplies control
context only while hostile, visible and within the engagement range; global
sidesteps have no target leash and never initiate offensive attacks or approaches.

Exploration records wall-limited searched ground within 24 subtiles during combat
and looting as well as movement. Loaded monsters may be known farther away and
remain navigation targets even on searched ground or behind walls. Room allocation
is not proof of monster visibility: map loading allocates rooms for their layout.
Call `explorer:observe(me[, monsters])` during those interruptions;
`explorer:tick(me)` also observes before moving. A mover resumed after skipped
script frames replans from the current position if the player moved, instead
of returning to a stale waypoint. Dead ends can still require retracing.
Movement routes start at the player's actual cell. If clearance excludes that
cell, pathfinding uses exact collision instead of snapping across a nearby wall.
If the exact snapshot also blocks the occupied starting cell, a local search copy
opens that cell alone when a cardinal neighbor is passable and live blocking bits
contain only unit presence (`0x1000`), which native player pathing removes;
surrounding collision
and the cached map remain unchanged, and the live collision mask is logged.
Failed destinations get a temporary retry delay and never fabricate coverage.
Optional farms can finish with tiny patches remaining. `explorer:cleanup()` keeps
existing coverage and includes every reachable unseen cell. The Den first uses
that cleanup, then re-sweeps if necessary; only quest credit establishes completion.
The campaign also defends itself while warming map layouts after a hot reload.

Travel switches to walking below 20% stamina and resumes running above 60%,
recovering stamina while moving instead of stopping to rest.
The Act 1 campaign calls `stamina.upkeep(me)` to drink a stamina potion below
40% before walking becomes necessary. Town supplies include bounded reserves
of `vps` (stamina) and `yps` (antidote); `services.prepare_antidote(me)` prepares
up to two acknowledged doses near Andariel, with bounded retries.

The sorceress enters the Den at level 2 and then advances toward Andariel without
global character/area-level gates. Travel engages close threats, elites and
nearby dense packs; it limits loot and container detours. Farming ends as soon
as its goal is met, and resource farms stay on the main route. The sorceress
skips automatic Rogue hiring. In Normal, after the Den and reaching Black Marsh,
the strategic network may choose Tower preparation from level 5, refreshing
Countess and boss packs only while their preparation utility wins. Level 11 is
a maximum fail-safe, rather than a target. Two carried mana reserves permit Tower departure;
an actual shortage starts recovery, which buys six when affordable. P8 readiness
still requires six. Once inside the Tower, she continues with
one carried mana/rejuvenation potion or at least half her maximum mana.
Below both, a funded shortage uses a portal when available to preserve the descent.
Completion inside the Tower exits to town instead of walking
back up; three exhaustive searches without finding Countess abandon optional
preparation and resume the main route instead of failing Act 1.
Loaded Countess data behind a wall keeps exploration active until she is visible.
The campaign uses flexible player counts (PX), capped at the standard 1–8.
It reconciles P1 on each game load and before recovery or non-XP movement.
Before a Den or Tower-floor-1–4 hop, a Normal sorceress may select P8 when she
has at least 85% life, 75% mana, two healing/rejuvenation and six mana/rejuvenation
reserves, a portal scroll, action readiness, and no nearby hostiles or observed
missile lanes. These are initial tuning thresholds, not benchmarked optima.
Eligible Tower hops use P3 until Static Field has a learned point and positive
effective level, then permit P8 within the saved cap. Item-only skills and the
currently selected hand do not satisfy this gate; temporary skill unavailability
does not erase a learned point. Failed supply or safety readiness still selects P1.
The decision holds for that hop. Countess and Andariel approaches select P1
before their level transitions; lowering the setting does not undo the scaling
of monsters already created. A Tower chicken after acknowledged P8 exposure caps
later Tower XP attempts at P3; another after P3 exposure caps them at P1. The
per-character cap survives games in the same session and does not rise automatically.
Hot reload clears the current game's exposure history but preserves this cap.
Field, Den and P1-only chickens do not lower it. Den PX and all readiness and
emergency checks stay unchanged. Unsupported remote-client modes leave PX inactive.
This needs the DLL's `act.players`/`game.players` pair; a missing or unacknowledged
command fails explicitly instead of letting movement proceed under an assumed count.
Before Countess or Andariel, an empty healing reserve prompts a portal visit from
a quiet position when carried cash covers the missing mana and healing reserves,
or vendor loot can fund them. A failed purchase cannot repeat that visit until
healing supplies have actually been obtained.
After the Den, ordinary travel also seeks a quiet portal visit with at most one
carried healing/rejuvenation potion, no hostiles within twelve subtiles and no
observed missile lanes. It reserves funds for actual missing mana purchases
and two healing potions, or sells carried vendor loot. Waypoint activation takes
priority. Failed purchases are limited to one attempt per area until more than
one healing reserve has been obtained; the unfinished-Den opening is exempt.
Other builds retain the optional Countess resource run. The early route and supply choices draw on
[Teo's LoD guide](https://pf-mail.wixsite.com/d2speedruns/route-sgy2j) and
[Void's guide](https://nightfallx2.wixsite.com/d2speedruns/gw); full-run gear and
level milestones are not prerequisites for this Act 1 endpoint.
Melee remains an early mana fallback when cash and saleable loot are scarce;
once a town visit can fund potions, running dry triggers restocking.
Den recovery keeps the current game: use a portal when available, otherwise
walk home with defensive melee available against blockers. A failed path or
60-second walking deadline still exits, as do emergency chicken/frozen checks.
The deadline is checked before reactive dodges can take ownership of the tick.
Initial reserves of two requested mana and healing potions precede portal and stamina
stock; additional mana takes priority over optional stamina. Missing basic
mana/healing reserves hold the remaining cash instead of buying cheaper
optional supplies. Fewer than two carried mana/rejuvenation potions prevent a
initial Tower departure or a portal
return to a hard area. Dry recovery uses monster-level-two-or-lower farms where
the staff fallback works, stopping at six carried reserves (two during Tower
preparation when six are unaffordable). Town trades wait for both the inventory
and gold replies before sending another request, with a 240 ms minimum spacing.
The native sender suppresses identical outgoing packets less than 200 ms apart.
Missing item replies time out after 1500 ms and skip that item or kind; a received
item with an unsettled gold balance ends trading safely for that visit.
Belt transfers also wait for an ownership receipt. An unacknowledged transfer
after 700 ms skips that potion kind for the visit, so a full equipped belt does
not cost one failed request for every inventory potion; later visits may retry.
Plain javelins and throwing
knives join the small vendor-loot pool. A completed Den uses a portal to town
when available. Services skip a full unequipped starter belt and go directly
to the stash when no healing or trade is needed; quest-notification flags never
trigger Escape. At game start, quest synchronization opens the quest panel with
`act.quest_log(true)`, waits for `game.quest_revision()` to advance, and closes
the panel before trusting quest credit. An already-open panel closes first;
failed opens retry up to three times. Missing replies reset the game after ten
seconds instead of trusting stale records. No NPC trip is needed.
An observed Andariel death requests this refresh before resupply or further
movement. Missing credit after that attempt resets the game instead of waiting
at the corpse; field dodges cannot postpone the refresh deadline indefinitely.
Travel engages ordinary monsters in the next short stretch of its path before
walking into them. Skipped nearby threats also suppress loot and container
stops, so travel keeps moving out of their reach. A money sweep ends for a
saleable drop or when 90 seconds pass without increasing the affordable/carried
mana reserve.
The NN ranks native approach routes against attacks, escapes and waiting using
current crowd and projectile risk. Missed shots can offer a closer firing
position or a four-subtile angle change. Tactical routes retain native corners
and use cell identity for fractional player positions; combat has no separate
ordinary mover or arrival tolerance.
Rejected tactical movement releases combat ownership without consuming an attack
interval. Dodge and retreat searches share a 400 ms retry delay after rejection,
so repeated path refusals cannot indefinitely suppress attacks.
Accepted tactical requests also yield to that retry delay after 600 ms without
0.75 subtiles of actual movement, even across replacement dodge/retreat plans.
Movement refreshes the budget; casting or a gap over 1000 ms without a tactical
request discards it, so a later fight starts fresh.
Thirty seconds without target damage releases that target. Unproductive waiting
also releases it after five seconds when a mana shortage is not the cause.
Ordinary movers shorten refused waypoints down to three subtiles, wait 300 ms
between retries, and consume the existing finite replan budget. Stationary
interruptions do not renew the progress deadline. A destination's repeated-game
failure budget survives earlier successful hops and clears upon reaching it.

Loot uses the client's sale quote for loaded items, accepting at least 120 gold
total and 60 gold per inventory cell. This includes valuable gems, class items
and larger gear that the old whitelist missed. A sorceress through level 4 with
under 300 gold and fewer than four mana potions accepts 60 gold total and 20 per
cell to fund the opening. Already-carried vendor loot retains that lower sale
floor so it can be sold after cash or reserves improve. Quotes expire after one second
and immediately after identification changes; unavailable quotes retain the
known-item fallback. Quest items, potions, runes and required scrolls are not
sold. Ground potion pickups stop at four healing, ten mana and four
rejuvenation potions across belt and inventory, keeping room for vendor loot.
One ground portal scroll can replace a purchase.
Travel expands the pickup radius from six to twelve subtiles for visible,
saleable drops quoted at 600 gold or more; ordinary drops retain the short radius.
Campaign ticks also pick up filtered items within three subtiles while moving,
standing or casting, without stopping the current activity. Pickup requests share
a cooldown, including the final gather pass. Requests are logged separately from
receipts: only membership in our own `game.items()` confirms acquisition; ground
disappearance and gold-balance increases do not identify a successful pickup.
Three unsuccessful attempts suppress that identity for 60 seconds across loot
instances. History is bounded to 64 entries; old absent-drop history can be
discarded without claiming receipt so it does not block fresh supplies.
A short, straight loot approach may
precede combat only while the player, item and corridor remain unthreatened;
new melee pressure or ranged fire returns control to combat on the next tick.
A fresh safe drop already in reach keeps loot ownership during the shared
500 ms pickup pacing; missing, unsafe, pending or refused drops still yield.
Recovery's six-potion target counts carried mana/rejuvenations. Cash reaching
the target triggers shopping, and a recovery destination is reconsidered if
the carried reserve falls below two while travelling.
The client mana-potion state (106) prevents stacking regular mana potions while
one is still restoring mana. Rejuvenations remain available for an instant
refill, and active regeneration delays an empty-reserve restock.
Regular healing chooses the largest carried healing potion whose normal dose fits
the missing life, including the healing already queued by an active potion
(client state 100 and projected final life in stat 74). A smaller inventory
potion remains eligible when a belt potion is too large. At or below half
current life, an oversized potion is allowed, including rejuvenation as a fallback;
critical life still prioritizes rejuvenation. Outside that emergency floor,
fallback rejuvenation must also fit its full healing amount. Active healing with
a pending or inconsistent projection holds normal requests until the data arrives;
emergency requests remain available.
Expired effects ignore stale projections. Normal doses use the vanilla 1.14d
potion values and class coefficients; unpredictable doubled healing is reflected
in the client projection after consumption.
Visible Andariel takes the boss combat path, which excludes trash beyond five
subtiles and passes her id as combat's explicit focus. Immediate blockers and
defensive movement still take priority. Walls and closed doors keep the
approach/door route active instead of suspending movement for an obscured boss.
Reacquisition follows her live position, then her last observed live position,
and uses the spawn preset only before she has been seen. Live positions are saved
before restocking; small position changes do not restart route retries. The single
map refresh remains a per-game allowance across town visits. A terminal approach
logs the boss lookup, visibility, selected goal and actual route endpoint once.
Wide doors beside a blocked route are considered within four subtiles of its
line; the wider check never opens a side door beside a clear route.
With the early Den reward, the sorceress puts her first four points into Charged
Bolt and saves the level-five point. At level six she learns Frost Nova, then
Static Field as her sixth allocated point, before adding more Charged Bolt.
Skill allocation holds points for the first unmet known entry in the build;
an unavailable level or prerequisite cannot send them into a later entry.
Build-drift checks compare only the allocated-point prefix; unspent points,
item bonuses and basic skills do not force a reset.
Offensive self-casts such as Static Field obey normal escape decisions; Frost
Nova can cast first to chill nearby threats and create room.
During clearing, farming and boss fights, missile arbitration retains a fresh
engagement target beyond firing range as defensive-control context. Global
sidesteps have no target leash; normal combat ranks any later approach separately.
Recovery keeps the shorter retention range, and travel supplies its accepted
target or route heading. Hostility, sight and native route safety still apply.
Damage and target-value estimates apply the visible elite resistance modifiers
in their recorded order, including the two-immunity limit and Ghostly's physical
resistance. Ordinary minions retain class-base resistances. This estimates the
verified modifiers; temporary resistance changes from curses and auras are not
reconstructed.
With no healing or rejuvenation potions left, retreat starts below half life.
Rapid loss of at least 20% of maximum life within two seconds, ending below
60% life near danger, starts an urgent portal attempt even with potions left.
It uses a scroll or a nearby owned portal and skips loot. Existing hard chicken
thresholds still take priority. Damaging ground overlap counts as nearby danger
even without live monsters. An urgent attempt gets four seconds to reach town;
reactive dodges and the stall watchdog cannot cancel or extend that deadline.
An existing portal within six subtiles is reused; a farther one is replaced
with a fresh scroll when available, preserving the fight without a long exposed
run. Pending casts wait for the portal before retrying or using the old one.
Portal approach requests and entry retries have separate cooldowns, so arriving
within interaction range permits the first click without waiting on movement.
Before entering a portal to town, the campaign records reachable enemies within
five subtiles of its field endpoint. Two ordinary enemies, any elite, or ground fire mark the
return unsafe. After restocking, a known progression waypoint replaces that
return in the same game, preserving cleared areas. Safe returns and returns
without a suitable waypoint keep using the portal.
Boss approach keeps an immediate blocker until it dies or leaves the personal
space radius, then resumes boss focus. Target changes preserve an active escape
from pressure; a boss already in close range keeps focus while escape decisions
handle surrounding enemies.
Forward travel deliberately detours for Black Marsh, Outer Cloister and
Catacombs Level 2 waypoints, allowing up to ninety seconds per attempt. After
the Den, a Normal sorceress also establishes Cold Plains as a recovery anchor,
with a forty-five-second limit during travel or gold farming. Farm activation
resumes its existing commitment after acknowledgement or timeout; a failed
same-game attempt is not repeated. Held vendor loot is cashed in first. A dry
ninety-second Cold Plains farm refreshes through its known waypoint instead of
walking back through the Moor. Other waypoints have a thirty-second limit and
must already be visible within twelve subtiles. Travel skips optional containers
once carried potions and gold
cover six mana doses. Andariel quest credit ends the campaign before any further
healing, shopping or looting.
An active exploration route keeps movement ownership through a brief neighboring
area crossing, so farming travel cannot pull it back at the boundary. Coverage is
recorded only in the explored area; a failed route or five-second detour releases
ownership for normal travel recovery.
Border travel likewise keeps a moving hop through a third-area detour for up to
five seconds. A stopped or failed mover replans from the actual area; warp hops
continue to replan immediately on area changes.
After the Den, gold recovery prefers reachable, unspent Cold Plains over another
Blood Moor sweep. A Normal sorceress at levels 3–5 can resume before Tower
preparation with two carried mana or rejuvenation potions when six are
unaffordable; affordable reserves still require shopping. Outside town on Cold
Plains, Stony Field, Underground Passage Level 1 or Dark Wood, she can also skip
or end a discretionary gold farm with at least 85% life, 75% mana, one carried
mana/rejuvenation and one separate healing/rejuvenation potion. This requires
the Den complete and no Tower preparation; Black Marsh departure reserves,
actual dry-combat recovery, town services and survival checks remain active.
