# Working on Charon

This file is for AI coding agents (Claude, GPT, and others) and for the humans who
drive them. It covers how Charon is put together, the rules that keep it stable,
and how we develop YARB, the Lua scripting framework, and the bots written with it.
Read it before you change anything.

## What Charon is

Charon is a DLL for Diablo II 1.14d (Windows, 32-bit x86). Game.exe loads it at
startup, and it patches the game in memory: quality-of-life features, a native
OpenGL renderer, and YARB. It targets single player and TCP/IP games only;
Battle.net is disabled.

Only 1.14d is supported. Every address, structure offset and calling convention
in the code is specific to that exact Game.exe. Nothing here carries over to
other patches.

### Repository map

| path | contents |
|---|---|
| `dllmain.cpp` | entry point |
| `features/` | one file per feature. Each defines a `Feature` subclass (`headers/feature.h`) and overrides the hooks it needs: `gameLoop`, `oogLoop`, `chatInput`, `keyEvent`, the draw callbacks and so on |
| `framework/` | shared machinery: hooking (`hook.cpp`), the OpenGL backend (`opengl_*.cpp`), YARB's host and bindings (`script_*.cpp`), path finding (`pathfinding.cpp`) |
| `headers/` | declarations. `remote.h` holds the game's functions and globals by address; `D2Structs.h` and `headers/ghidra/` hold game structures; `script_game.h` holds the client layouts YARB uses |
| `scripts/` | YARB's Lua side: `oog/` menu scripts, `game/` in-game scripts, `lib/` shared modules. `scripts/README.md` is the API reference |
| `tests/` | standalone tests run by CTest (renderer decoders, script host, path finding, ABI checks) |
| `third_party/luajit/` | vendored LuaJIT 2.1 plus a CMake build for it |
| `PORTING.md` | design notes and evidence for the OpenGL renderer |
| `work/` | ignored local scratch: build trees, logs, test harnesses. Never commit from here |

## Ground rules

- **Main thread only.** Charon does its work on the game's main thread. Do not
  start threads that touch game state.
- **Optimization stays off for the DLL.** The assembly hooks and indirect game
  callbacks depend on code paths that the optimizer or linker can remove. Keep
  compiler optimization, whole-program optimization, reference elimination and
  identical-code folding disabled. LuaJIT and the standalone tests may be
  optimized.
- **Addresses come from evidence.** Every function, global and structure offset
  must trace back to the game binary. Use the Redivivus Ghidra databases (see
  [Reverse engineering](#reverse-engineering)). Treat decompiled C as a
  hypothesis and check calling conventions against the disassembly: stack purge,
  which registers carry arguments, and what the function preserves.
- **Pin layouts.** When you add a structure, put the offset in a comment on each
  field and add a `static_assert` for the offsets you rely on (see
  `headers/script_game.h`).
- **Leave unrelated code alone.** Make the smallest change that solves the
  problem, and match the style of the file you are in.
- **Don't commit, push or publish unless the maintainer asks.** Do not overwrite
  their game install, saves or settings without keeping a backup.

## Building and testing

CMake is the development build. From a Visual Studio developer shell:

```powershell
cmake -S . -B work/local-build -A Win32 -DCHARON_BUILD_OPENGL_TESTS=ON -DCHARON_BUILD_LOCAL_TESTS=ON
cmake --build work/local-build --config Release --parallel
ctest --test-dir work/local-build -C Release --output-on-failure
```

The build writes `Charon.dll` and `Charon.map` to `Release/` (or `Debug/`) at
the repository root. Keep the map file: crash logs give Charon offsets, and the map
turns them into function names.

- The Visual Studio project (`Charon.vcxproj` / `.sln`) has not caught up with
  YARB: it lacks the `script_*`, `pathfinding` and `Scripting` sources and
  LuaJIT. Bring it in sync before a release; until then, build with CMake.
- CI (`.github/workflows/build.yml`) builds and packages the DLL, the `scripts/`
  folder and LuaJIT's license. Tests run locally, not in CI.
- Renderer tests need a working OpenGL 3.3 driver.

Release zips ship the DLLLoader as `dbghelp.dll`, which Game.exe side-loads. For
local testing, the maintainer installs the freshly built `Charon.dll` directly as
`dbghelp.dll` in the game folder. Back up whatever is there first.

## Reverse engineering

The Redivivus project keeps Ghidra databases of 1.14d Game.exe (and the Mac
build) with names, types and comments. Agents can read them through the
Redivivus MCP server: decompile, disassemble, list cross-references, read data,
and so on. Claude agents with the Redivivus plugin should load its skill before
the first call.

How we use them:

- Look a function up by address, read the decompiler output, then confirm the
  ABI in the disassembly before declaring it in C++.
- Functions with a custom register contract need an assembly adapter. See the
  adapters at the top of `framework/script_game.cpp`.
- Game data tables (Skills.txt, ItemTypes.txt, Objects.txt and so on) are read
  in memory at fixed record offsets. Record each offset in a comment where it is
  used, along with where it came from.
- A corrected name or type can be proposed back to Redivivus with the MCP
  `propose` tool. Proposals need evidence; follow the skill's rules.

**Crashes.** `features/Scripting.cpp` installs a crash tracer that writes to
`%APPDATA%\Charon\lua.log`. It logs the faulting address as module+offset, the
data address, the thread, the registers, the code addresses found on the stack,
and the last YARB binding entered. Resolve Charon offsets with `Charon.map` and
game offsets with Redivivus.

## YARB architecture

YARB is the scripting framework (Yet Another Reason Bot).

```
features/Scripting.cpp      command line, per-frame tick driver and game clock, /lua chat commands, log file, crash tracer
framework/script_host.cpp   one LuaJIT state per script, error isolation, hot reload, shared store
framework/script_game.cpp   `game` (reads client state) and `charon`
framework/script_actions.cpp `act` (moves, casts, interactions, item and gold handling)
framework/script_map.cpp    `map` (layout, path finding, exploration, line of sight, safe spots)
framework/script_town.cpp   `town` (vendors, hirelings, respec)
framework/script_oog.cpp    `oog` (menus and character select)
framework/script_policy.cpp `policy` (scores candidate rows with an authored network; neural_policy.cpp is the math)
framework/pathfinding.cpp   grid path finding, free of game dependencies and unit-tested
```

- `-oogscript` runs for the whole session. `-gamescript` loads when a game
  starts and unloads when it ends. Each script gets its own Lua state.
- In a game, `tick(frame, ms)` runs once per game frame: one step of the game's
  logic, 25 a second at the stock rate. Single player steps its server in
  lockstep with the frame rate, so a higher "Target Single Player FPS" plays
  the same game faster. That setting is the only limit: Charon paces the
  loop itself (`features/GameLoop.cpp`, `headers/frame_wait.h`) in place of
  the game's sleep after every pass, and vertical sync gives way when the
  target is above the display's refresh rate. A target of 175 plays about
  seven times real time. `charon.now()` is therefore game time (40 ms per
  frame; wall time only in menus): scripts time everything with it and never
  count ticks or assume a frame rate, and a run plays the same at any speed.
  The client's sender (0x478350) drops a request identical to the previous
  one within 200 ms, 50 ms for skill and attack requests, so identical
  repeats (two of the same purchase) are spaced by more. Charon runs that
  window on the game's pace as well (left on real time it was 1.4 seconds
  of game time at a target of 175, and a character whose step was
  interrupted stood still for it), so game time measures the gap too.
  `charon.wall()` is real time, for measuring a run. A tick over 8 ms logs
  a warning.
- An error in a script is logged with a stack trace, and that script stops; the
  game keeps running. In game, `/lua reload` and `/lua stop` control the game
  script.
- A script reloads when its file, or any module it loaded with `require`,
  changes on disk. Files loaded with `dofile` are not tracked: touch the
  top-level script to reload them.
- `print` and other script diagnostics go only to `%APPDATA%\Charon\lua.log`.
- `store` is process-wide (it survives game changes and is shared by both
  scripts). Plain Lua tables die with the game script.

### YARB's rules

These are the project's design decisions. Do not work around them.

1. **Client-side data only.** Bindings read what the game client holds and send
   only the requests the client itself sends. Never read server-side structures
   (`pGame`, D2Game units). Other Charon features do, but YARB must work in a
   TCP/IP game hosted elsewhere. Check every new binding against this rule.
2. **Clean room.** Do not read, copy or port code from other Diablo II bot or
   scripting projects. Sources allowed: Charon itself, Redivivus, and your own
   reasoning and testing.
3. **Nothing blocks.** No sleeping or waiting, in Lua or in bindings. A call
   starts something and returns; the script watches `game` on later ticks to see
   the result. Long behaviour is a state machine (`lib/fsm.lua`).
4. **Heavy work in C++.** Path finding, flood fills, exploration plans and threat
   maps belong in C++. Lua decides; C++ computes.
5. **FFI stays opt-in** (`-luaffi`): it gives scripts unchecked memory access.

### Adding a binding

1. Find the game state or function in Redivivus and confirm its ABI.
2. Add the layout or function pointer to `headers/script_game.h`, with offset
   comments and a `static_assert`.
3. Write the binding in the matching `framework/script_*.cpp`. Read arguments
   with `Script::ArgInt`, `ArgString` and the other readers, and report failures
   by throwing `Script::Error`. **Never raise a Lua error directly while C++
   objects are alive:** LuaJIT on x86 Windows unwinds with `longjmp` and skips
   destructors. `Script::Bind<F>` converts the exception for you once the stack
   is clean.
4. Validate before acting. Check that the player exists, that the game isn't
   being left (`RequirePlayer`), that the panel the request needs is open, and
   that ids and amounts are in range. A bad packet can desync or crash the client.
5. Register the function in the file's `luaL_Reg` table.
6. Document it in `scripts/README.md`, in the matching module's table.
7. Test it live (see [Live testing](#live-testing)) and read `lua.log`.

Request packets should copy what the UI sends, byte for byte. Do not call a UI
teardown function without checking what it frees. Calling the hire list's cancel
routine while the list was open corrupted memory and crashed the sound thread
several frames later. The bot now saves and exits after hiring, which resets the
UI cleanly.

## Writing Lua

- **LuaJIT 2.1 is Lua 5.1.** There is no `//` operator and no integer division
  (use `math.floor(a / b)`), and no 5.3 bit operators (use `bit.band` and the
  rest of the `bit` library).
- Modules in `lib/` return a table, usually with a `new` constructor, and keep
  their per-instance state in the object. Put a usage example in the header
  comment, like the existing modules.
- Constants are upper case at the top of the file, each with a comment saying
  why it has that value. Behaviour is mostly tuned by changing these.
- Units from `game` are snapshots. Keep the `id` (and type), not the table, and
  look the unit up again on later ticks.
- Positions are world subtiles; a tile is 5 subtiles.
- The policy networks are written by hand in `tools/author_policy_weights.py`.
  Change a named coefficient there, regenerate, and run the tests; never edit
  the generated `*_weights.lua`. Features are addressed by name through
  `lib/scorer.lua`, never by position.
- Prefix log lines with the module or state (`stash: deposit 1200 gold`). The
  log is the main debugging tool, and every decision a bot makes should be
  traceable in it.
- Act once per tick in menus, then read the screen again: menus change after
  every action.

## The Act 1 bot

`oog/campaign.lua` creates or reuses a character and enters games.
`game/act1.lua` plays Act 1 toward Andariel. It is an `fsm` whose `decide`
state picks the next job in priority order:

1. sync quest state
2. respec (only when the skill points have drifted from the build)
3. hire a mercenary
4. sync waypoints
5. town services
6. corpse recovery
7. Andariel or the Den of Evil
8. gold farming
9. the Countess
10. the next level toward the goal

The other states carry out those jobs. Progress survives across games in
`store` keys named `act1.<character>.<flag>` and in the game's own quest and
waypoint records, so a fresh game picks up where the last one stopped.

| module | role |
|---|---|
| `builds.lua` | per-class skill and stat order, buffs, supply ordering and optional melee skill data |
| `combat.lua` | targets, line-of-sight filter, shared survival, tactical route execution, potions and point spending |
| `neural_combat.lua` | sole combat action policy: legal spell/melee candidates and native routes, described by expected damage rate and exposure, ranked by authored weights |
| `strategy.lua` | Tower preparation versus the main route, ranked by its own authored weights |
| `scorer.lua`, `*_weights.lua` | named-feature front end for the native `policy` scorer; the generated weight artifacts (`tools/author_policy_weights.py`) |
| `supplies.lua` | counts of carried potions and scrolls, shared by every module that asks |
| `value.lua`, `leveling.lua` | what monsters and areas are worth to the build; which area to farm |
| `travel.lua`, `move.lua`, `explore.lua` | routes between levels, path following with door handling, level sweeps |
| `services.lua`, `stash.lua` | town visits: heal, sell, buy, belt, stash |
| `loot.lua`, `containers.lua` | what to pick up and sell; chests and other objects to open |
| `gear.lua` | which change of gear is the best on offer: the character as she is against the character with the item on |

### Gameplay knowledge baked in

These came from watching the bot play, and most came from the maintainer's
feedback. Keep them unless you have a reason backed by testing.

- **Build.** The sorceress follows the speedrun build: Charged Bolt takes most
  points, and Frost Nova and Static Field get one point each (Frost Nova or
  Frozen Armor, never both). Stats go into Vitality. No Telekinesis early. Don't spread points;
  Charon's respec is repeatable, and the bot respecs when its points drift from
  the build.
- **The opening** (the maintainer's route). A new sorceress reads the town
  portal scroll she starts with beside the Den's entrance and leaves the
  portal standing, fights on to the Cold Plains waypoint (before her first
  spell she fights everything in sight: those kills are level 2; but
  nothing that would take more than three casts unless it is upon her: at
  eight players a zombie is ten Fire Bolts or twenty blows), rides the
  waypoint to town and steps through the portal to clear the Den with the
  bolt and the staff. With the Cold Plains waypoint known, the cleared Den
  ends the game: the next one starts beside Akara and the shop, and the
  waypoint leads on with no ground walked twice. Without the scroll, or in
  a later game, the Den is simply next. The Blood Moor is crossed at eight
  players on the way out and the Den cleared at one (what the runner the
  maintainer watched does: level 2 comes a few Fallen in, and the drops
  are the gold and potions the Den is otherwise fought without). The targets are the maintainer's
  (2026-11-13): the Den beaten by about 4:30, Andariel by about 30
  minutes. As of that day she enters the Den at about 1:45 and it is
  credited between 4:46 and 7:49, every run of the last ten finishing. A search of the Blood Moor for gold before going on
  (`moor_first`) was tried and switched off: 300 to 600 seconds in four
  runs, and no richer for it on an unlucky map. She does not wait by the waypoint
  for level 2 (the maintainer's trial: it comes soon enough). While the
  portal by the Den stands no other scroll is read to restock (a second
  portal closes the first, and she walked to the Den from town after all),
  and a visit to Akara is not over until her menu has come up and been
  closed: left talking, on a first meeting she gives the Den's speech and
  the character answers nothing for its length, the portal included.
- **Gear is judged by what it does to her, in the maintainer's order**
  (2026-11-12), each line deciding only between loadouts the lines above
  call equal: damage of one cast of her attack (skill levels to it, the
  hardest thing to come by); mana after each kill; cast frames (faster
  cast rate counts only where it reaches a step); effective life (life by
  the average of her resistances, the physical one being damage reduced
  with block behind it); mana up to the size of a potion; run/walk; hit
  recovery; more mana and regeneration; then defence, potion slots on the
  belt, gold find and the weapon's own blow. A loadout is judged whole: a
  two-handed staff is weighed against wand and shield together.
  Requirements are a filter, by the client's own check. Her first five
  stat points go to Vitality (against early chickens), the next five to
  strength, 10 to 15, for the armour that asks it, the rest to Vitality.
  What is better goes on before a town visit sells it, and in the field
  when nothing is near. What is picked up to be weighed goes by quality
  against what the slot holds (the maintainer's rule): unique, set and
  rare together (a good rare can beat a bad unique, and all three nearly
  always beat magic), then magic, each when everything it would take the
  place of is its like or less. That is for what is not yet read; an item
  already known where it lies (anything plain) is weighed there and then,
  and picked up only if putting it on would be a gain (by quality alone a
  sorceress with a hand free picked up a short bow). Gear she carries and
  will not wear is sold for whatever it fetches: what came off, what was
  read and found wanting. So more picked up is more gold as well. Finds are
  read with scrolls of identify in town, as many as she can pay for after
  her restock; the rest are sold unread. Charms go in no slot and are for
  sale: the pack space is worth more than they are. Anything she wears
  that is down to a third of its durability is mended at Charsi's on the
  next town visit, after Akara's business. Nothing is bought for
  wearing yet. Not yet tried in a live game.
- **Skill choice is for damage now,** not mana efficiency. Compare damage per
  cast. Frost Nova only when unchilled, chillable monsters are close enough to
  matter. Static Field when its percentage damage across the monsters in range
  beats the best bolt. Never fall back to a one-point skill as the main attack.
- **Gold like a goblin.** Pick up gold, staves, wands and scepters of any
  quality (their staff mods sell well, unless the item type is class-only),
  small magic items, and runes. Open chests, urns, corpses and the like when
  they can be reached, but only while she is broke (short of the gold for a
  full restock, stash included): after that each one is a stop for nothing. Farm the Forgotten Tower (Countess) for gold, experience
  and runes.
- **Stash the gold.** A death drops the gold carried, but vendors can spend
  stash gold, so the bot deposits everything after selling.
- **Survival.** Drink a healing potion at 70% life; use mana potions, then
  rejuvenations, for mana. Never drink in town; visit the healer instead. Chicken
  (save and exit) at 15–25% life depending on the crowd, or after a sudden
  burst of damage.
- **Don't walk in under-levelled.** A build's `enter_gap` is how far an area's
  monsters may out-level the character (one level for the sorceress). Below
  that the bot levels up nearby instead of pushing on: at two levels behind,
  nearly every pack in the Barracks and the Catacombs forced an emergency portal
  or a chicken, and each chicken re-walks from the last waypoint. Where she
  may walk in she may also level: with a narrower farming bound she entered
  the Barracks, found the Jail gated, and walked back out to farm lower.
- **Where to level is the optimizer's choice** (`lib/leveling.lua`), among
  every area of the act: experience per point of damage her element gets
  through, spawn density, what was measured there at her level, and the way
  there. The way is one jump from the nearest waypoint of the act, whether
  she has it or not, plus the walk on, charged once in time against the stay
  it buys; a waypoint she lacks is fetched on foot when the choice hangs on
  it. Nothing but pay and the way enters the score: no bonus for lying on
  the route or anything else (the maintainer's rule), and a kill costs at
  least one cast, so that overkill is not counted as pay (by damage alone
  the Blood Moor out-scored the crypts at level 3, and she spent nine
  minutes there for a level). By this reckoning the Burial Grounds' crypts pay best to level 7 and
  the Tower's cellars from 8 to 12, the choice speedrunners made by hand.
  Reckoned from the waypoints she had, with a flat discount per level
  walked, she stayed in the crypts beside the Cold Plains at level 8. With
  a game's best areas cleared she takes a new game when that pays more
  with its cost counted: everything back, from town, and one more hop of
  travel for leaving and loading.
- **One evaluation, trickling down.** For builds with `follow_pay` the
  question "what is she doing?" is answered in one place (`objective` in
  `game/act1.lua`) from the character alone, and held until its area is
  cleared or she gains a level. The planner asks there; the states below it
  carry the answer out and do not decide for themselves. Rules that each
  worked it out from where she stood sent her back and forth between two
  waypoints four times in a day (the Barracks, the Catacombs waypoint, the
  Monastery Gate, the Black Marsh and the Cold Plains). When a rule seems
  to be needed, it belongs in that evaluation, not beside it.
- **Broke mode.** Her means are part of that evaluation. With fewer than
  two mana doses in hand or within her gold she is poor until she can pay
  for six, and while poor the choice is among areas whose monsters are at
  least five levels under her, and of level 3 at the least (the
  maintainer's figures: the floor is "whatever lets her fight in cold
  plains, crypts, and mausoleum"; it was 1 until a level-4 character was
  sent back to the Blood Moor after the Den and searched it for 770
  seconds for coins), where she opens chests and keeps what drops, and where the
  staff always comes before a restock. What she carries that sells counts
  as her means: once it would pay for the six, selling it is what she does
  next (counting gold alone, she searched the Blood Moor for ten minutes
  with 51 gold and two thousand in staves on her back). Out of mana and broke with town
  far, she fights on with the staff, wherever she is, before the game is
  given up. Sent to the Tower with 69 gold she arrived
  dry, left the game, and did the same in the next one.
- **Progression.** Once an area of the next act pays more than anything a
  fresh game of this act offers, the act boss is the objective and stays
  it until dead (the maintainer's rule). From then on she is at one
  player. A boss is fought as soon as she stands on its level. Two
  chickens on the way to the boss put it off until her next level, and she
  levels where it pays meanwhile: at level 11 nine in a row went to the
  elite Afflicted by the stairs to Andariel.
- **When to level** is an experiment in progress (2026-11-12). With
  `follow_pay` in the build (the sorceress has it) nothing is gated by
  level: after the Den she is always where a kill pays best, and falling
  pay in out-levelled areas is what moves her on; a boss is fought as soon
  as she stands on its level. Without it she walks on while the next area's
  monsters are within `enter_gap` of her and asks the optimizer where to
  make up the difference. One run each so far: following the pay she was
  level 10 after an hour and had not left the Tower; the margin with the
  hand-written Tower preparation at eight players had reached level 12 and
  Andariel's level. The Tower preparation (`tower_prep`, the strategy
  network) is switched off while the optimizer is tried in its place.
- **Players by gold.** After the Den the players setting is one with less
  than a restock of gold in hand (stash included), and one more for each
  further restock she could pay for, up to eight (the maintainer's rule):
  more players is more experience per monster for the same mana per point
  of it, and more potions on the way. It is kept at all times, on the way
  as well as where she levels (what she kills while travelling pays the
  same, and a level has spawned by the time a count set on arrival would
  apply); only Andariel's level and the one before it are entered at one
  player. Each emergency portal or
  chicken out of such a stint takes one player off, while she is that
  level, in every area whose monsters are at least as high as those that
  caused it: the monsters she left keep the life they spawned with, but
  what spawns next is lighter.
- **The Tower pays for the monastery.** Until the Barracks a sorceress cannot
  afford the mana potions her kills cost; the Countess and her cellars give
  the first gold and levels. Skipping them with a three-level gate reached
  the Barracks four minutes sooner at level 6, broke, and left the game seven
  times in five minutes.
- **A way home before potions.** The first 100 gold of any town visit buys
  a town portal scroll, before the Den as after it (the maintainer's rule):
  without one every restock is a new game and the walk back from the last
  waypoint, or three minutes on foot out of the Den and back. Left out
  before the Den, she came home through a portal with 1,141 gold, bought
  sixteen potions and went back with no scroll for the next time. A portal
  trip returns through its portal even when the visit could not buy the
  full reserve.
- **Out of mana among weak monsters, use the staff.** Mana comes back slowly
  without potions, and a town trip costs more than a few blows. In areas
  whose monsters are level two at most (the Moor, the Den, the Cold Plains)
  a dry sorceress clubs what dies in a few swings and hurts little, judged
  by what the fight costs against her life and the healing potions in her
  belt (life alone at first, to save their gold: then she stood off the
  last of the Den for ten minutes at 56% life with three potions unused,
  too healthy to drink one and too hurt to go in); what
  hits hard or takes many blows is still kited, and with a scroll and the
  gold for potions she restocks instead. Deeper in, monsters hit too hard
  for a staff to pay.
- **Ration mana.** Where the staff is allowed and she has fewer than four
  mana potions, one or two monsters that die in five blows and will not
  hurt her much meanwhile are clubbed; three or more together, and
  whatever the staff cannot deal with, get the spell (the maintainer, on
  how runners clear the Den: melee what can reliably be meleed, and "use
  melee more often unless we're sure charged bolt can hit multiple
  monsters"). In the Den that holds whatever her pool, short of full,
  until its elite pack is dead; elsewhere only below six tenths of the
  pool, which fills by itself. Cast at everything, a new character's pool
  was gone in ten casts and half of each Den clear was fought without it.
  The staff is the slower way all the same: clubbing and the walking up
  to it were over half of the clears where she rationed, and the fastest
  clears on record are those with seven to ten mana potions and no staff.
- **Cast from where the volley lands.** Charged Bolt fans out: from the
  edge of her range a quarter of it reaches the target. While less than
  half of a volley would land and there is a safe step closer, she takes
  the step first; what the bolts that do arrive would kill is shot where
  it stands. Cast from ten subtiles, Corpsefire's zombies took four casts
  each: a pool for one kill, and two minutes for the next pool.
- **An elite pack is fought from range.** No staff against an elite, its
  minions or anything beside them while she has a spell that touches
  them, dry or not (dry in the Den at 59% life she clubbed the zombies
  around Corpsefire, drank three potions in ten seconds and left the
  game). The Den's pack, found with less than ten casts in hand, is left
  where it stands and the rest cleared first; with nothing else left she
  stands off until the pool has filled (two minutes at most) and goes in
  then. Sent in at once she kited it dry and walked home hurt.
- **Shamans first, hurt or not** (the maintainer's rule: it saves killing
  their Fallen twice). One in sight and in reach of the fight is the
  target ahead of the Fallen around it; only a monster within 3.5 subtiles
  that is no Fallen comes before it. The rule had been kept for when she
  had nine tenths of her life, and she drinks at seven. MonStats `threat`
  (ten for most of Act 1, twelve for a Fallen Shaman) scales every
  target's priority as well. One she can see and not reach is given up
  after five seconds; dodging its fireballs does not start that count
  again.
- **Out of mana on the way, keep moving.** Leaving the game for mana cost the
  whole walk again, several times over. A dry traveller runs past what she
  cannot fight (`combat:dry`) and goes to town only when a scroll and the gold
  for potions are both there.
- **Bosses.** Andariel runs faster than we do (11 subtiles a second against
  9), hits for three to four times her table damage, and does not heal while
  we are in town; her hall holds shooters and a shaman. So: her company is
  fought first, by the usual target choice; she is met back along the way in,
  not in her hall; she is struck with Static Field when she arrives and left
  again, or simply stood up to when the arithmetic says the fight is won
  (the README's two plans, for bosses only: against packs they caused every
  sudden loss of life in a run); a boss with less of her life left than
  we have of ours is finished rather than left; and the potion reserve is
  topped up on the way in, never once the fight is on. Each engagement takes
  15 to 30% of her life, and what keeps the fight short is not losing that:
  a chicken is a new game and her life back, a town trip is not. So the
  chicken waits for the boss threshold while the emergency portal is being
  opened too (forgetting that left the game a third of a second after the
  portal was decided, twice); below half life with potions not keeping up
  she goes to town instead of sliding to a chicken (Andariel was at 17%);
  mana is drunk ahead while the boss is in sight; and a boss left with her
  company in sight is rejoined on foot from the Catacombs waypoint, because
  they gather at the portal (68% of a full life in two seconds on the way
  back in, with Andariel at 25%); and a boss with a tenth of her life left
  is finished through everything but the last-ditch chicken (twice in one
  run she was at 8% when a chicken gave it all back). Tried and withdrawn: clearing the way to
  her (the elite Afflicted by the stairs ended five games in a row) and a
  hired Rogue (dead in every fight, and each hire is a new game).
- **Risk is measured, not counted.** Combat weighs each choice by the share of
  the remaining life the monsters in reach would take (`value.dps`), so one
  Fallen is shot where it stands and a pack that takes a third of what is left
  is kited. Shooters count for the chicken rules from across the screen; an
  archer pack killed a sorceress from 71% in three seconds while nothing was
  "near". The damage tables know a blow, not spells or poison: losses beyond
  them correct the estimate on the spot and are remembered for the session
  against the monster class that took most (`value.learn`). Losses and
  tables are compared over the same two seconds: asked only about the
  moment, the tables said next to nothing of a zombie she had just stepped
  back from, and in every run its first blow in the Den marked zombies four
  times as dangerous for the session, after which a sorceress out of mana
  would not close with one to club it. The Tainted, the
  Afflicted and their kin (Misshapen, Disfigured, Damned), whose lightning
  took two to four times their table in every run, start out at three
  (`value.lua`). Elite packs that would take a third
  of her life are not sought out on the way.
- **Fight what pays.** What a kill pays is experience per point of damage
  her element gets through (resistance, an elite's life and experience, the
  level penalty): for a caster, experience per mana. The measure is what
  the monsters around her level pay, averaged over the act's areas whose
  monster level is near hers; the area she is in would call everything
  usual where everything is poor. A monster that pays less than nine tenths
  of that (the maintainer's figure) is fought when it is upon her and left
  alone otherwise, on the way and
  while levelling, and an elite of that kind is gone around like a
  dangerous one. Where everything must die (the Den, a boss and her
  company) the rule does not apply. The maintainer saw her spend her mana
  on an elite pack that shrugged off most of her lightning.
- **Go around what is not worth the fight.** An elite whose pack would
  take a third of her life or which does not pay is remembered where it
  was seen, nothing standing on that ground (12 subtiles) is fought by
  choice, she walks out of it when she finds herself on it with nothing
  upon her, and travel and the search of a level plan around it while a
  way round exists that is not much longer. With no way round it is fought
  from range, on purpose, rather than walked into. A town portal out of
  such a pack and a new game after it cost the whole walk again: two elite
  packs of Misshapen inside the door of the Underground Passage took half a
  level-4 life in two seconds, and the portal led back into them.
- **Slip past what shoots.** Anything that shoots is not fought by choice
  (the maintainer's rule: dodging it cost more time than it paid), and
  what it would pay counts for nothing in an area's worth. The ground it
  reaches (18 subtiles) may be walked through but not fought on: nothing
  standing there is sought out, on her way she keeps to her route across
  it with monsters upon her (six seconds at most, then she turns on them),
  and where she is levelling she steps out of it before a fight, and what
  is upon her comes along. Planned around like a dangerous pack, that
  ground closed whole stretches of a level: in the Blood Moor she walked
  away from Quill Rats 64 times in one run. A shooter whose pack would
  take a third of her life is still gone around.
- **Don't react to what can't reach you.** Monsters behind a wall don't trigger
  kiting, and objects behind bars aren't clicked (that froze the client).
- **Travel by waypoint,** not on foot, after a restart. A waypoint she
  does not have and passes within 25 subtiles of is taken on the way,
  whatever she is doing.
- **What breaks and stands in the way is broken.** A barrel, an urn or a
  casket on the next leg of a route (within seven subtiles ahead and two
  and a half to the side) is struck before she runs into it, and so is one
  beside a step the game refused; twelve to a route at most. Two barrels in
  a Tower cellar doorway refused every step until the route gave up, four
  times over, and the descent was lost to a new game. Object collision is
  read into the path grid again as she comes near (the game spawns objects
  late), so routes go around what stands in the open; a refused step now
  logs what stands beside it.
- **Search a level in order.** Backtracking cannot be avoided altogether,
  but it is not invited: the next ground to search is the nearest, and
  ground that belongs to a large unsearched region counts as farther, so a
  side passage or a pocket is searched while she is beside it and not from
  the far end of the level (no tour is planned; it is one more flood fill).
  Monsters the fight would decline are not walked up to either. Monsters are walked to
  when they are within sight or stand on ground already searched; one
  loaded far off on unsearched ground is met when the search gets there
  (walked to at once, each took her across unsearched ground, and a
  quarter of the search's walking in the Blood Moor was legs of ninety
  steps and more, going back for what lay between).
- **Dodge onward.** With somewhere to go (a route between levels, the next
  point of a search) a dodge out of a missile's lane steps toward it: a
  subtile onward counts for twice what a subtile walked costs in the
  choice of spot. With onward only breaking ties she dodged back and forth
  under the archers of the Tower's cellars and got nowhere.
- **Squeeze only at the doorway.** Paths keep a margin from walls. Where the
  margin closes the only way (a cellar doorway), the route is found on the
  exact collision with the margin's cells made expensive, so it stays on
  open ground and squeezes through the door alone, and it leaves a
  margin-covered cell (a stair landing beside a wall) by the open ground
  next to it. The plain shortest path on the exact grid hugged the walls
  instead: the game accepts a step along a wall's foot or into a corner and
  does not walk it, and on one map every Tower descent ended at the foot of
  the second cellar's stairs.
- **When stuck, start a new game.** A minute without progress triggers a
  re-plan. Three of those, a client stuck unable to act, a failed travel, or
  four minutes without experience, gold, a kill or new ground (pacing between
  two places looks like movement) all lead to a save and exit. A new game
  resets the client. A walk home is called off when a level gained on the way
  has refilled life and mana, and out of the Den it gets longer before it
  gives up: leaving the game there undoes the clear. The third emergency
  portal from one place is a new game as well (in a boss fight only while
  the boss is unhurt): the
  portal leads back to the pack that caused it, and a lightning pack beside
  the Inner Cloister waypoint sent her home seven times in ninety seconds.

## Live testing

The bot can only be judged in the live game. Ask before launching the game if
you have not been told you may.

- Keep test harnesses in `work/` (ignored), with paths for your own machine. The
  pattern:
  - A wrapper OOG script and game script set `package.path` to the repository's
    `scripts/` folder and run the real script with `dofile`. They add a time
    limit, a stop file (when it exists, the bot saves and exits and the OOG
    script ends the process), and an exit when the goal is reported in `store`.
  - A shell script installs the build as `dbghelp.dll`, switches Charon to
    windowed mode (`openGLFullscreen` in `%APPDATA%\Charon\settings.txt`),
    starts Game.exe with the script arguments, waits for it to exit, restores
    fullscreen, and prints the new lines of `lua.log`.
  - A relaunch loop starts the next run when a game ends, until the goal is
    reported.
- Follow `lua.log`, not the screen. Add log lines for any decision you need to
  see.
- Edit Lua while the game runs; hot reload picks it up. C++ changes need a
  rebuild and a relaunch.
- The maintainer often watches and reports what they see. Treat those reports
  as bug reports: find the cause in the log and the code before you change the
  behaviour.

**Clean up after yourself.** When testing stops, kill the relaunch loop's
process itself, not just the tool task that started it. An orphaned loop
reopens the game every time it closes. Then stop the game with the stop file,
restore the settings you changed, and check that no harness processes are left.

## Known issues

- A rare crash on the sound thread (`SOUNDSMP_CheckBufferLost`, 0x515FD0,
  called from the sound update thread) has been seen twice, the second time
  on 2026-11-12 at a target of 175 FPS: an access violation at 0x515FD7
  reading a sample that was no longer there, on the sound thread, with the
  script idle ("in tick: no"). No clear link to YARB; the faster game frees
  and starts sounds seven times as often. Running the game with `-ns` (no
  sound) would sidestep it for unattended runs.
- A game halt in the client's belt handling, seen once (2026-11-12, at a
  target of 175 FPS): `HandleItemUpdateStats` (0x4c4c70), reached from server
  packet 0x9D with the put-in-belt action, found an item that was not in the
  player's inventory and stopped at line 3166 of `UNIT\Item.cpp`; the game
  exits with code -1 and writes `BlizzardError\<time>\Crash.txt` in its
  folder. She was fighting and probably drinking from the belt; the bot's
  potion requests were seconds apart. Cause unknown.
- The hire list has no safe teardown (see above).
- On one character's map the Catacombs Level 2 waypoint never appeared at its
  preset position (object class 157 at the same spot every game): the client
  held no object unit there. On other maps it is there and is activated.
  Cause unknown. The bot gives up after two seconds at the spot and remembers
  it for the session; restarts in the Catacombs then resume from the Inner
  Cloister.
- The Visual Studio project is behind CMake (see above).
