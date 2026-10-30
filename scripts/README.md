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
| `tick(frame, ms)` | 25 times a second (the game's logic rate). `frame` counts ticks since load; `ms` is a millisecond clock. |
| `on_<event>(...)` | Before `tick`, for each queued event (reserved for future event sources). |

Keep `tick` short; a tick over 8 ms logs a warning. Long-running behaviour is a
state machine advanced by one step per tick; `lib/fsm.lua` provides one.

When a script raises an error, Charon logs the error and its stack trace and
stops calling that script; the game carries on. A script reloads automatically
when it or any module it required changes on disk. In game, `/lua reload` and
`/lua stop` control the game script, and `/lua mem` reports memory use (see
`charon.memory()`). Output from `print` goes to the in-game
message log and to `%APPDATA%\Charon\lua.log`.

Lua's `os.exit` ends the whole game process.

## Shared modules

### `charon`

| call | returns |
|---|---|
| `charon.context()` | `'oog'` or `'game'` |
| `charon.frame()` | current tick count |
| `charon.now()` | milliseconds since Charon started |
| `charon.log(text)` | same as `print` |
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

## `game`: what the client knows

Units are plain snapshot tables; refer to a unit later by `(type, id)` or by
passing its table back. Positions are world subtiles (5 per tile).

| call | returns |
|---|---|
| `game.ready()` | the player exists and stands in a room |
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
| `game.waypoints()` | `{ [level] = true }` for waypoints activated on this difficulty, as of the last waypoint panel opened (`nil` before any) |
| `game.level_info(id)` | Levels.txt facts for the current difficulty: `name, act, monster_level, density, unique_packs_min/max, waypoint, inside, monsters, unique_monsters` (spawnable class ids) |
| `game.monster_info(class [, level])` | MonStats facts: `name, level, own_level, min_life, max_life, experience` (scaled with MonLvl.txt as the game does), `resist` (`physical, magic, fire, lightning, cold, poison`), `boss, npc, undead, demon, killable, speed` |
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

## `act`: doing things

Calls return immediately; watch `game` to see the result. Movement, attacks and
interaction go through the client's own command path, so the character animates
exactly as for a mouse click.

| call | does |
|---|---|
| `act.ready()` | the player can start a new action now |
| `act.move(x, y [, walk])` | walk or run toward a point (short range; use `map.path` for long routes) |
| `act.move_to(unit [, walk])` | run toward a unit |
| `act.select_skill(hand, id [, owner])` | put a skill on `'left'` or `'right'` |
| `act.cast(hand, x, y)` / `act.cast_at(hand, unit [, stand_still])` | use the hand's skill on a point or unit |
| `act.interact(unit)` | talk, open, or use a waypoint, portal or stairs (stand next to it) |
| `act.pickup(item [, to_cursor])` | pick up a ground item |
| `act.use_belt(slot [, mercenary])` | use belt slot 0–15 (0–3 is the bottom row) |
| `act.add_stat(stat [, count])` | spend stat points: 0 strength, 1 energy, 2 dexterity, 3 vitality |
| `act.add_skill(id)` | spend a skill point |
| `act.waypoint(level)` | travel from the open waypoint panel (interact with a waypoint first) |
| `act.use_item(item)` | right-click an inventory item: town portal scroll or tome, potions |
| `act.to_belt(item)` | move a potion from the inventory to the belt |
| `act.pick_item(item)` / `act.place_item(item, page, x, y)` | lift an item onto the cursor; put the cursor item into a cell of `inventory`, `cube` or `stash` (cube and stash must be open) |
| `act.transfer_gold(way, amount)` | `'deposit'` inventory gold into the open stash or `'withdraw'` it; stash gold survives death (`game.me().stash_gold`) |
| `act.leave_game()` | leave the game, as the escape menu's Save and Exit does; the menus return (close the escape menu first) |
| `act.key(vk)` | press a key in the game window |
| `act.send(bytes)` | send a raw client request |

## `map`: layout and path finding

Levels of the current act are generated on the client from the game seed, so
the whole layout is available before it is explored. Path finding runs in C++.

| call | returns |
|---|---|
| `map.path(x, y [, {step=15}])` | waypoints `{ {x, y}, ... }` from the player to a point, or `nil, reason` |
| `map.path_to_level(level [, {step=15}])` | waypoints that end just inside a neighbouring level |
| `map.rooms([level])` | rooms: `x, y, width, height, center_x, center_y` (walkable cell nearest the centre) |
| `map.presets([level])` | planned spawns: `type, class_id, x, y` |
| `map.exits([level])` | neighbouring levels: `level, kind` (`'warp'` with `x, y` for stairs and entrances, `'border'` for open ground) |
| `map.bounds([level])` | `x, y, width, height` |
| `map.walkable(x, y)`, `map.line_clear(x1, y1, x2, y2)` | collision queries |
| `map.line_of_sight(x1, y1, x2, y2)` | whether a missile can fly between two points (live collision: walls and closed doors block; end cells not checked) |
| `map.safe_spot{threats = {{x, y, weight}, ...}, target = {x, y}, range = 7}` | where to stand when monsters close in: `found, x, y, danger_here, danger_there, open_sectors, path`. Danger fades from each threat (assumed to keep closing in); cells beside a threat, slim gaps and live collision (monster footprints) are excluded, and routes need a corridor about a tile wide. `found` is false when nothing reachable is safer (trapped); `open_sectors` (0-8) counts the directions still offering a wide way out |
| `map.current_room()` | the player's room rectangle |
| `map.explore_plan([cell])` | a sweep of the player's level for exploring or clearing: `{ {x, y}, ... }` in walking order, and the planning time in ms. Reachable ground (one flood fill) is split into blocks of `cell` (12) subtiles ordered depth-first, dead ends first; linear in the level size, a few ms even for large areas |
| `map.explore_mark(x, y, radius)`, `map.explore_seen(x, y)`, `map.explore_reset([level])` | the level's coverage: cells seen from a point (walls block sight) |
| `map.explore_next([minimum])` | the nearest unseen walkable patch by walking distance: `x, y, steps, unseen, total`, or `nil` when nothing reachable is unseen |
| `map.doors([level])` | door spawns `class_id, x, y`; paths go through doorways, so open the live door object (`act.interact`) when you reach it |
| `map.invalidate()` | forget cached layouts (after doors open, for example) |

`lib/move.lua` follows these waypoints one tick at a time and plans again when
the character gets stuck. `lib/explore.lua` walks the sweep from
`map.explore_plan`, skips what has been seen, and finishes with frontier search.

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
