# Charon

This is an experiment and utility for Diablo 2. It is intended for single player and TCP/IP game use, so connections to Battle.net are disabled. It's main features include:

    Native OpenGL rendering with windowed/fullscreen switching and screen filters.
    Ladder Runewords, Cube Recipes, and Unique Items in Single Player and TCP/IP
    Uber Tristram and Diablo Clone in Single Player and TCP/IP
    Gently Improved Drop Rates with Respect to Single Player
    Gently Improved Rune Drop Rates with Respect to Single Player
    Map Reveal (more information now)
    Display Monsters, Missiles, and Items (superior and above) on Automap
    Updated Item Tooltips and Item Level Display
    /players X uncap (16-20 is reasonable, but very difficult)
    Experience Bonuses and Scaling
    Repeatable Socket, Respec, and Imbue Quests
    Repeatable Cow Portal (even if you killed the Cow King)
    Disabled Weather and Screen Shaking
    Regenerate Single Player Maps (always a new map!)
    All Items Drop Pre-Identified
    Single Player FPS Uncap (40 FPS is a good spot)
    Multiple Simultaneous Windows
    Fixed Unique Monster Color Bug
    Loading additional mpq via command line parameter: -mpq "filename.mpq"
    YARB, a Lua scripting framework for menus and gameplay (see below)

Almost all features are optional now! Press F11 while Diablo 2 is running to enable/disable these options!

Charon uses its native OpenGL renderer regardless of video command line flags such as `-w`.
Use **Display Mode** in the F11 settings window or **Alt+Enter** to switch between
windowed and borderless fullscreen. The selection is saved; the initial default is
fullscreen. **OpenGL Filter** offers Nearest, Bilinear + CAS, Jinc2, xBRZ, ScaleFX, CRT, CRT Curved, and LCD.
ScaleFX reconstructs pixel-art edges at 3x resolution before scaling to the window.
CRT adds pronounced scanlines, a coarse phosphor mask, horizontal color bleed,
highlight glow, and darker screen edges with brightness compensation. CRT Curved
adds screen bowing while keeping the full frame visible and mouse input aligned.
Both CRT options use a matched 2.2 transfer curve, neutral color balance, and soft
highlight rolloff while retaining the established midtone brightness.
LCD emulates a 1024x768 desktop panel with scaler softness, a six-bit + FRC color
ramp, top-to-bottom TN gamma shift, faint RGB pixel structure, and uneven backlighting.
Its transition-dependent settling is modestly faster than the initial LCD preset.
It uses a fixed nominal 4 ms modern LED-backlit LCD baseline to approximate the
extra response, with no per-monitor calibration. Trails follow elapsed time and
reset when switching modes; FRC phases are averaged to reduce low-frame-rate shimmer.

# Instructions

- Download Charon.zip from the releases tab.
- Unzip Charon.zip to your Diablo 2 directory (Game.exe should be here).
- Create a shortcut to Game.exe
- Edit the shortcut and add this at the very end of the 'target' line:  -loaddll Charon.dll

Now you can run the game using the new shortcut and it will load Charon automatically!

Note: Do not try to run Charon with any other Diablo 2 modifications. It will most likely crash!

# YARB

YARB (Yet Another Reason Bot) is Charon's scripting framework. It embeds LuaJIT and
runs two scripts named on the command line: one for the menus, for the whole session,
and one that loads fresh for each game:

    Game.exe -loaddll Charon.dll -oogscript scripts\oog\create_character.lua -gamescript scripts\game\clear_blood_moor.lua

Scripts never sleep or block. Charon calls `tick` 25 times a second, and longer
behaviour is a state machine that advances one step per tick. Scripts read only
what the game client knows and send only the requests the client itself sends, so
they behave the same in single player and in TCP/IP games. The expensive work (path
finding, level exploration, picking a safe spot in a fight) runs in C++. Scripts
reload when their files change, so you can tweak a running bot without restarting
the game.

The included scripts:

- `oog/create_character.lua` and `game/clear_blood_moor.lua` create a character and
  clear the Blood Moor: a small example to start from.
- `oog/campaign.lua` and `game/act1.lua` play one sorceress through Act 1, speedrun
  style, across as many games as it takes. In testing it has taken a new character
  all the way to Andariel's death.

The reusable parts (movement, combat, looting, town services, stashing) live in
`scripts/lib`. See [scripts/README.md](scripts/README.md) for the scripting API.

YARB is a prototype. The API will change, the bot logic is rough around the edges,
and plenty of the game is still unscripted. You are free to build on it: fork it,
write your own bots, or improve the framework itself. We highly recommend vibe
coding with Claude or GPT. Most of YARB was built that way, and
[AGENTS.md](AGENTS.md) tells an AI agent how to work in this codebase: the rules,
the build, how to add bindings, and how to test a bot in the live game.

LuaJIT's FFI is off by default because it gives scripts unchecked memory access;
pass `-luaffi` to allow `require 'ffi'`.

# Compiling

To use Charon you'll need to compile it with Visual Studio 2019. Once you've compiled Charon you must load Charon.dll into the game using [DLL Loader](https://github.com/Nishimura-Katsuo/DLLLoader). Loading it with traditional injection techniques may work, but will most likely crash or not function properly, since this is written to operate completely on Diablo 2's main thread for stability.

A lot of hard work is being put in by us at [Blizzhackers](https://github.com/blizzhackers) to bring you this tool with love.

Release optimization is intentionally disabled: the assembly hooks and indirect game
callbacks rely on code paths that compiler/linker elimination can remove. Keep compiler
optimization, whole-program optimization, reference elimination, and identical-code
folding disabled for the DLL. Standalone renderer tests may use optimization.

CI intentionally builds and packages the DLL; regression tests run locally. With CMake
available in a Visual Studio developer shell, run:

```powershell
cmake -S . -B work/local-build -A Win32 -DCHARON_BUILD_OPENGL_TESTS=ON -DCHARON_BUILD_LOCAL_TESTS=ON
cmake --build work/local-build --config Debug --parallel
ctest --test-dir work/local-build -C Debug --output-on-failure
cmake --build work/local-build --config Release --parallel
ctest --test-dir work/local-build -C Release --output-on-failure
```

Renderer checks require a working OpenGL 3.3 driver. Charon initializes on the game's
main thread before archives and rendering start and stays loaded until process exit.

>I see him there at the oars of his little boat in the lake, the ferryman of the dead, Charon, with his hand upon the oar and he calls me now. ~Alcestis (from Alcestis by Euripides)
