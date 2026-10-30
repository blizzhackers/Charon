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
features/Scripting.cpp      command line, 25 Hz tick driver, /lua chat commands, log file, crash tracer
framework/script_host.cpp   one LuaJIT state per script, error isolation, hot reload, shared store
framework/script_game.cpp   `game` (reads client state) and `charon`
framework/script_actions.cpp `act` (moves, casts, interactions, item and gold handling)
framework/script_map.cpp    `map` (layout, path finding, exploration, line of sight, safe spots)
framework/script_town.cpp   `town` (vendors, hirelings, respec)
framework/script_oog.cpp    `oog` (menus and character select)
framework/pathfinding.cpp   grid path finding, free of game dependencies and unit-tested
```

- `-oogscript` runs for the whole session. `-gamescript` loads when a game
  starts and unloads when it ends. Each script gets its own Lua state.
- `tick(frame, ms)` runs at the game's 25 Hz logic rate. A tick over 8 ms logs
  a warning.
- An error in a script is logged with a stack trace, and that script stops; the
  game keeps running. In game, `/lua reload` and `/lua stop` control the game
  script.
- A script reloads when its file, or any module it loaded with `require`,
  changes on disk. Files loaded with `dofile` are not tracked: touch the
  top-level script to reload them.
- `print` goes to the in-game message log and to `%APPDATA%\Charon\lua.log`.
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
| `builds.lua` | per-class skill and stat order, buffs, and `attack`: the skill choice in a fight |
| `combat.lua` | targets, line-of-sight filter, kiting, repositioning after missed casts, potions, point spending |
| `value.lua`, `leveling.lua` | what monsters and areas are worth to the build; which area to farm |
| `travel.lua`, `move.lua`, `explore.lua` | routes between levels, path following with door handling, level sweeps |
| `services.lua`, `stash.lua` | town visits: heal, sell, buy, belt, stash |
| `loot.lua`, `containers.lua` | what to pick up and sell; chests and other objects to open |

### Gameplay knowledge baked in

These came from watching the bot play, and most came from the maintainer's
feedback. Keep them unless you have a reason backed by testing.

- **Build.** The sorceress follows the speedrun build: Charged Bolt takes most
  points, and Frost Nova and Static Field get one point each (Frost Nova or
  Frozen Armor, never both). Stats go into Vitality. No Telekinesis early. Don't spread points;
  Charon's respec is repeatable, and the bot respecs when its points drift from
  the build.
- **Skill choice is for damage now,** not mana efficiency. Compare damage per
  cast. Frost Nova only when unchilled, chillable monsters are close enough to
  matter. Static Field when its percentage damage across the monsters in range
  beats the best bolt. Never fall back to a one-point skill as the main attack.
- **Gold like a goblin.** Pick up gold, staves, wands and scepters of any
  quality (their staff mods sell well, unless the item type is class-only),
  small magic items, and runes. Open chests, urns, corpses and the like when
  they can be reached. Farm the Forgotten Tower (Countess) for gold, experience
  and runes.
- **Stash the gold.** A death drops the gold carried, but vendors can spend
  stash gold, so the bot deposits everything after selling.
- **Survival.** Drink a healing potion at 70% life; use mana potions, then
  rejuvenations, for mana. Never drink in town; visit the healer instead. Chicken
  (save and exit) at 15–25% life depending on the crowd, or after a sudden
  burst of damage.
- **Don't react to what can't reach you.** Monsters behind a wall don't trigger
  kiting, and objects behind bars aren't clicked (that froze the client).
- **Travel by waypoint,** not on foot, after a restart.
- **When stuck, start a new game.** A minute without progress triggers a
  re-plan. Three of those, a client stuck unable to act, or a failed travel all
  lead to a save and exit. A new game resets the client.

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

- A rare crash on the sound thread (`SOUNDSMP_CheckBufferLost`, called from the
  sound update thread) was seen once, with no clear link to YARB. The crash
  tracer now logs enough to chase it if it happens again.
- The hire list has no safe teardown (see above).
- The Visual Studio project is behind CMake (see above).
