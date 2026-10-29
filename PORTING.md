# Charon native OpenGL renderer

## Scope and claim

The target is a Charon-owned OpenGL backend selected through Diablo II's 3dfx
renderer mode. Charon supplies the renderer callbacks directly. The backend must
preserve palette changes, colored lighting, draw modes, shadows, perspective,
menus, and gameplay drawing before its presentation filters are considered done.

Current claim: implemented backend with synthetic GPU validation; unverified
in-game recovery. The standalone sprite RLE decoder is boundary-verified for
the synthetic cases below. The OpenGL DLL is installed for user testing;
comprehensive live rendering comparisons remain pending.

User decisions:

- Preserve the original 640 x 480 and 800 x 600 logical canvases.
- Present the completed frame windowed or borderless fullscreen with aspect ratio
  preserved and black borders where required. Filter the entire frame, including UI and cursor.
- Offer Nearest, Bilinear + CAS, Jinc2, xBRZ, ScaleFX, CRT, CRT Curved, and LCD. Linear and bilinear are the same
  2D sampling choice; trilinear mipmap filtering adds no benefit to simple upscale
  presentation and is deferred.
- Keep logical canvas dimensions separate from output dimensions so a later
  widescreen change does not require rewriting presentation. Widescreen itself
  is outside this first implementation.
- Native terrain always uses point sampling. This intentionally overrides old
  Glide's per-floor bilinear requests; smoothing is selected over the completed
  frame. The original terrain geometry, UV insets, and lighting are retained.
- The game's gamma slider midpoint/default (155) maps to display gamma 1.0.
  Divide its callback value by 155 rather than the original Glide divisor 100;
  this intentionally removes the default brightness lift to match the user's
  D3D/GDI baseline. Slider values below/above midpoint still darken/brighten.

## Inputs and platform

- Original executable: `D:\Diablo II\Game.exe`, read only.
- SHA-256: `631066c1649c4ea9ffe48bf97e24c00bca1f7a6759c21150f1a79982589adaaf`.
- Revision: project and Redivivus identify Windows Diablo II 1.14d; the file hash
  above identifies the exact local input. No independent dump-list check yet.
- Original and host: little-endian 32-bit x86, native Windows C++/MSVC.
- GPU: NVIDIA GeForce RTX 4060. A compiled 32-bit Win32/WGL probe successfully
  created an OpenGL 3.3 core context, reported GLSL 3.30 and NVIDIA driver 610.88,
  and resolved the required shader, buffer, vertex-array, framebuffer, blending,
  and mipmap entry points. Maximum texture dimension reported: 32768.
  This verifies the requested 3.3 context, not the driver's maximum GL version.
- Recovery source: Redivivus Windows database, checked against machine code.
  Decompiled code and declared callback types remain hypotheses until checked.
- Original game files and saves remain outside version control. Test fixtures
  must use synthetic data or locally generated, ignored reference artifacts.

## Recovered selection seam

- `D2GFX_Initialize`: `0x004f52e0`.
- Selector load: `0x004f53ac` reads `[ESI * 4 + 0x0072da80]`.
- Mode 4 selector slot: `0x0072da90`.
- Original 3dfx renderer table: `0x0072f178`, 216 bytes / 54 function pointers.
- Active renderer pointer: `0x007c8cc0`; initialized by the store at `0x004f53eb`
  before SetOption/Initialize are called.
- `D2GFX_SetupRenderFunctions` at `0x004f8dc0` initializes the common line buffer;
  it does not install the renderer selector.
- The original 3dfx Initialize callback at `0x00508f60` loads Glide, and
  InitPerspective at `0x0050a010` also calls Glide. Neither can remain on the new
  backend's initialization path.
- OpenGL is the sole Charon renderer. The Always D3D implementation and its
  DirectDraw/D3D patches and settings have been removed.
- Force mode 4 before `D2GFX_PreSetupBeforeCreatingWindow` at `0x004fa520`, which
  also stores a mode copy used by D2Win. Changing only the active pointer after
  initialization leaves mode-specific state inconsistent.
- Preserve common class/window creation and Charon's RegisterClassA hook. Defer
  the GL context until the CreateWindow callback receives the actual HWND; do not
  initialize a driver under DllMain's loader lock.
- The end-of-frame calls are two phases: table slot 7 finishes the frame; slot 8
  presents it. Charon's existing Drawing hook invokes slot 7 and resumes original
  code that invokes slot 8. Present exactly once.

## Rendering pipeline

Fixed D2 code/data bindings in the native renderer, sprites, tiles, and video paths
use `REMOTEFUNC`, `REMOTEREF`, and `REMOTEPTR`. Codec IAT entries are references to
function pointers, preserving indirection and the video/sprite image-base adjustment.
Renderer selection and instruction patches use `MemoryPatch` with short intent comments.

Use a modern shader-based backend with a framebuffer at the logical canvas size.
Draw world, UI, text, and cursor into that framebuffer in the engine's original
order. Resolve palette indices, tint, RGB lighting, transparency, and draw-mode
blending while drawing. Then apply the selected presentation filter and draw into
an aspect-fitted viewport in the actual client area. Clear outside the viewport
to black. Input coordinates must use the inverse of that same viewport mapping.

Canvas dimensions, output dimensions, and presentation viewport are separate
values. They must not replace the game's simulation/visibility dimensions merely
because the display is larger. The future widescreen change belongs at the
logical-canvas/engine boundary.

Sprite sampling and the deliberate point-sampled terrain behavior precede the
Nearest, Bilinear + CAS, Jinc2, xBRZ, ScaleFX, CRT, CRT Curved, and LCD options on the completed frame.
Reconstruction/sharpening runs as GPU shaders, separate from engine lighting.

Jinc2 and Bilinear + CAS replace MMPX; standalone Bilinear was subsequently
removed. The current filter order is Nearest (0), Bilinear + CAS (1), Jinc2 (2),
xBRZ (3), ScaleFX (4), CRT (5), CRT Curved (6), and LCD (7). LoadSettings upgrades version-0
and version-1 saved selections directly to these indices; the next normal save
persists the selection and openGLFilterVersion: 2 together. The removed experimental
last slot in version 1 maps to Nearest. Gameplay and video share the same selection.
The MMPX shader, CPU reference, and license have been removed.

ScaleFX uses Sp00kyFox's MIT-licensed five-pass implementation from libretro,
pinned with source and license in third_party/scalefx. The first two canvas-sized
passes retain RGBA32F metric data; the next two store RGBA8 edge tags. Classification
also reads the original metric pass, and reconstruction reads the untouched scene
into a 3x RGBA8 target. Final linear sampling fits that target to the viewport and
applies game gamma once. Buffers are allocated on demand and released on canvas
resize or context destruction. An independent upstream GPU pipeline checks the
port, including edge fixtures, fractional/native/downscaled outputs, borders,
gamma, switching, and context recreation. All 24 upstream comparison cases pass,
including 800 x 600 to 1440 x 1080. Auxiliary texture unit 1 is unbound afterward
to prevent scene-framebuffer feedback during later solid/RGBA draws; checks assert
this state directly. A 120-frame standalone fixture at those dimensions measured
0.552 ms mean and 0.654 ms p95 on RTX 4060 / NVIDIA 610.88, with GPU completion
included and game drawing, upload/readback, and SwapBuffers excluded. Both DLL
configurations build and all six renderer test suites pass. Live appearance and
other GPU drivers remain unverified.

LCD is index 7; existing version-2 selections are unchanged. Its fixed virtual
1024 x 768 panel is independent of the logical game canvas and real monitor.
The scene is sampled with bilinear scaling into two RGBA32F panel-history targets;
final presentation interpolates the linear-light state and encodes gamma 2.2.
Game gamma is applied once, before the panel's color transfer. A six-bit + FRC
ramp uses a four-phase virtual 60 Hz cycle, spatial Bayer offsets, and staggered
RGB phases. Low/high voltages are shaped separately before averaging transmission.
The duty cycle is integrated across each elapsed frame to reduce low-FPS aliasing;
activation uses a full-cycle average. Phase is reduced in CPU double precision
before entering GLSL, so long uptimes do not lose the subframe phase.

The TN-like transfer uses channel exponents 2.05/2.20/2.35 with top-to-bottom shifts
of +/-0.17/0.20/0.23, mildly restricted gamut and raised blacks. Presentation adds
faint RGB apertures and row gaps, analytically averaged over the destination-pixel
footprint. Static low-amplitude backlight gain/leak varies across the virtual panel.
These features preserve the logical canvas and mouse geometry. The response profile
is 20% shorter than the initial preset: nominal rising 22.4-38.4 ms and falling
28.8-51.2 ms, with darker destinations settling more slowly. The fixed modern
LED-backlit LCD baseline remains 4 ms; subtracting it in time-constant space
approximates added mean settling lag, not exact total 10-90% response calibration.
No user monitor characteristics are read. Uniform interval FRC averaging is an
artistic approximation to a physical panel's exponentially weighted optical response;
FRC phase makes exact cross-frame-rate endpoint equality inappropriate.

Finish renders a pending response from the last successfully presented history.
Only a successful Present commits its texture and captured Finish timestamp;
unshown or repeated Finish calls cannot accumulate an invisible ghost. A pending
frame is abandoned on BeginFrame. Activation, filter/canvas/output/gamma changes,
invalid clocks, and pauses above 250 ms prime history with the current target.
SwapBuffers wait naturally contributes to the following Finish-to-Finish interval;
variable delays between Finish and Present remain a timing approximation. Context
teardown clears target names even when making a lost context current fails. The
deterministic test clock is compiled into the LCD test target only.

All six renderer suites pass. LCD has 170 GPU/double-reference checks with an
independent probe-based oracle, plus TN/FRC/aperture negative controls, static ramps,
moving-edge and phase-mean/flicker bounds, virtual-panel bilinear reconstruction,
source preservation and texture-binding checks. A mixed-gradient 800 x 600 to
1440 x 1080 run measured 0.196 ms mean and 0.257 ms p95 for 120 warmed Finish calls,
including GPU completion; upload, readback and Present were outside timing. A D2
screenshot was rendered through the actual filter and visually inspected. Live
motion appearance, other drivers and measured physical response matching remain
unverified. No input delay, frame queue, or forced real-display refresh is introduced.

GPU checks capture their plain bilinear control through test-local RGBA8 canvas
scaling, rather than retaining a standalone Bilinear filter in production. The
reference preserves the framebuffer texture orientation and fullscreen triangle
diagonal so CAS still compares against the same hardware quantization.

Jinc2 uses [Hyllian's MIT-licensed 16-tap shader](https://github.com/libretro/glsl-shaders/blob/master/windowed/shaders/jinc2.glsl),
retaining its 0.44/0.82 kernel and 0.8 anti-ringing strength. It reconstructs
straight from the logical canvas to the aspect-fitted output. The shader omits
upstream vertex-coordinate drift and stabilizes near-integral texel-lattice
coordinates within 0.0001 texel before choosing the finite stencil. This prevents
raster-interpolated UV rounding from switching stencil sides at exact half-texel
ties; a synthetic diagonal/random-frame comparison exposed the original issue.
Weights use the original unsnapped coordinates. Fetches clamp at image borders.

Bilinear + CAS first scales into a viewport-sized RGBA8 intermediate, then runs
[AMD's MIT-licensed CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS/blob/master/ffx-cas/ffx_cas.h)
sharpen-only algorithm with fixed mild sharpness 0.2. The translation selects
exact arithmetic/per-channel weights (CAS_GO_SLOWER + CAS_SLOW), omits optional
diagonal samples, and guards all-black channels against division by zero. The
upstream UNORM path squares perceptual input before sharpening and takes the
square root afterward; this prevents over-sharpening from filtering nonlinear RGB.
`third_party/jinc2/` and `third_party/cas/` retain provenance and license notices,
including notices embedded in the distributed shader strings. Both filters use
OpenGL 3.3 fragment shaders and apply gamma once, after reconstruction/sharpening.
xBRZ's existing 4x intermediate and final linear sampling remain unchanged.

CRT is index 5 and CRT Curved is index 6; saved selections are migrated as above. Their
shared GLSL 3.30 shader reconstructs with matched gamma-2.2 decoding/encoding, adds
pronounced luminance-dependent scanlines, a coarse staggered phosphor mask,
horizontal beam softness, bright-neighbor halation, and a vignette, then applies
the game gamma once. Both share one shader program and one pass over the scene
texture, with no extra intermediate buffer. The flat option has no geometric
distortion. Neither option uses overscan or animation; the entire canvas, UI, and
cursor stay visible. Scanlines are integrated over the
output-pixel footprint and fade near native size to reduce aliasing. The stronger
phosphor mask remains visible at native size. A quarter-row beam phase preserves
visible scanlines at exact 2x. This replaces the initial subtle CRT treatment at
the user's request for a typical monitor of middling quality, with more visible
mask texture, beam spread, and glow. It requires twenty clamped texture fetches
per output pixel instead of the original four; it adds no render target.
Scanline energy is divided by `1 - 0.40 * depth`, providing partial light-loss
compensation while retaining the gaps and strong mask contrast. The original
full compensation (`1 - 0.50 * depth`) was toned down at the user's request. This
compensates CRT output only; it does not change the game's gamma setting.

The CRT transfer refinement replaces square/square-root processing with a pure
2.2 power curve. The glow threshold moves from 0.12 to pow(0.12,1.1), preserving
its encoded activation level. Highlight handling is identity through linear 0.75,
then follows 0.75 + 0.25 * (1 - exp(-4 * (x - 0.75))). This shoulder matches value
and slope at the join and approaches white smoothly. The same per-channel mapping
adds no white-balance tint. The established 1.35 gain and scanline compensation
remain unchanged. Game gamma and curved-edge coverage still apply once at output.

All six renderer suites and both DLL configurations pass. The original 38 CRT
reference cases remain, with 512 flat/curved GPU ramp draws and six additional
highlight references. Gray fields retain the old mean within 1% in measured cases
(3% regression limit), and complete RGB-mask triplets remain neutral within one
output byte. Formerly clipped source levels 224/240/255 now retain distinct
248/252/254 highlight values in the selected phosphor test. Both modes retain
opaque black and shadow steps; geometry, mouse mapping and curvature are unchanged.
A mixed-gradient 800 x 600 to 1440 x 1080 timing measured 0.228 ms mean flat and
0.240 ms curved, including GPU completion but excluding upload, readback and
SwapBuffers. D2 screenshot captures from both modes were visually checked.
Live gameplay and other GPUs remain unverified.

CRT Curved maps normalized output coordinates `p` to source coordinates
`q = p * (1 + 0.05 * p.yx * p.yx)`. Curvature strength is half the original
0.10 at the user's request. All source edges map inside the viewport;
the remaining corner area is opaque black, with a one-pixel antialiased outline.
Scanlines follow the warped source rows and use the local vertical scale for
footprint filtering. The shader's curvature uniform resets explicitly when
switching back to flat CRT. Mouse messages apply the same warp and cursor warps
use its inverse (eight simultaneous fixed-point steps and nearest output pixel),
so menus, UI, and gameplay stay aligned. Flat mapping retains its original integer
arithmetic. Geometry fixtures cover 686 native/upscaled/downscaled points, corners,
centers, letterboxing, invalid sizes, and clamping.

CRT GPU checks compare 28 flat and 10 curved source/frame/gamma cases, plus context-recreation cases,
with an independent double-precision reference, within one output byte. They cover
native/downscaled/fractional/2x/3x sizes, both letterbox orientations, 1xN/Nx1,
640 x 480 and 800 x 600 inputs, black preservation, RGB mask/scanline visibility,
determinism, switching through xBRZ/CAS, and an unchanged logical canvas. Exact
pixel-center remainder arithmetic prevents interpolation noise being lifted out
of black channels by high gamma. An altered reference is rejected by the comparator.
A native white-impulse check isolates glow into an otherwise black vertical
neighbor. A separate 800 x 600 to 1440 x 1080 gray-field check requires an RGB
phosphor spread above 40 bytes and a scanline row-average swing above 20 bytes,
so the old barely-visible treatment cannot pass. Synthetic visual output was
inspected at both native 800 x 600 and fullscreen 1440 x 1080 sizes; live CRT
game acceptance is not yet checked.
Brightness fixtures at 1.8x compare central gray levels 64, 96, and 128 against
bilinear, requiring averages within 12% while retaining the phosphor/scanline
contrast. Curved fixtures also check a bowed border/interior stripe and flat to
curved to flat uniform resets against independent source-array references.

New GPU checks compare 96 filter/frame combinations with independent double
precision reference formulas at fractional output sizes, including clamped
borders, 1xN/Nx1 images, diagonals, gradients, and random RGB frames. Jinc2 uses
the documented bottom-up lattice convention. CAS uses a captured bilinear
baseline to fix the GPU's actual 8-bit interpolation/quantization before applying
the independent CPU sharpening formula. Comparisons allow one output-byte level
for floating-point/8-bit rounding, and reject deliberately altered pixels.
29 Jinc2 and 25 CAS cases differ from plain bilinear, rejecting an inactive-pass
implementation. Letterboxes, filter switching with the reused intermediate,
neutral/brighter/darker gamma, both logical canvas sizes, and context recreation
are covered separately. Live appearance and other GPU drivers remain unverified.
All six test suites and both DLL configurations pass after independent review.
The expanded GPU/reference suite also passes under MSVC AddressSanitizer. Review
caught the need for CAS linearization before deployment; a dark-peak reference
check now distinguishes the correct output 90 from nonlinear sharpening's 95.

ESRGAN is deferred by the user. No neural inference runtime or model weights are
included in this change.

## Contracts already recovered

- Ground callback `0x0050d6a0` consumes a 6 x 6 grid of 36 screen vertices. Each
  light record is 12 bytes: intensity, red, green, blue, then signed 32-bit X/Y.
  Preserve the supplied geometry and per-vertex RGB, including the original
  strip order. A single textured quad loses the lighting and deformation.
- The original lighting table is `floor(intensity * channel / 255)` for each RGB
  channel. Packed light values are `0xBBGGRRII`. Global-light color and shadow
  intensity must also remain colored rather than being reduced to grayscale.
- Palette lookup and chromakey comparison precede light modulation. Sprite
  transparency is not safely described as "always discard index zero": optional
  per-sprite remaps, literal zero pixels, skipped RLE pixels, and draw mode 7
  interact. Preserve the original order.
- Sprite frame buffers are loaded/reassigned and can be recycled. Pointer-only
  texture-cache keys are unsafe. Use content identity or explicit invalidation
  with established lifetime/ownership rules.
- Tile residency can evict other tiles through callback global `0x0088cb1c`.
  Register the native fastcall invalidator via CPU leaf `0x005fe000` before
  calling the original CPU residency loader. Remove the callback safely before
  unloading Charon.
- CPU sprite decoder `0x006014c0`, CPU floor decoder `0x0050f980`, and perspective
  math can be reused only within their recovered contracts. Original Glide
  texture-cache loaders and upload routines must be replaced.
- Perspective callbacks at `0x0050dd50`, `0x0050dd60`, `0x0050de10`, and
  `0x0050df00` are call-free CPU routines. They require initialized LUT/global
  state; original InitPerspective cannot be retained to initialize it.
- Original draw modes include constant-alpha blending, additive blending, and
  multiplication. Some paths inherit prior alpha state. The callback named
  DrawImageFast is an outline/silhouette operation, not an ordinary fast blit.
- Rectangle and line color arguments are palette indices. Sprite clip bounds
  are not laid out as a Win32 RECT. Match concrete callbacks, not similarly named
  or same-arity declarations.

## Recovery in progress

All callback contracts must account for concrete target addresses, argument
registers and stack bytes, return values, caller-visible writes, ownership,
callback re-entry, and CPU helper dependencies. Unknown fields are not permission
to substitute no-op callbacks.

Separate investigations cover lifecycle/perspective, sprites and their decoded
buffers/cache lifetimes, and terrain/colored lighting. Detailed transient queries
are stored outside the repository under `%TEMP%\charon-opengl-*`; this document
keeps the decisions and evidence needed to continue.

Authored recovery reports (static analysis, not runtime verification):

- `%TEMP%\charon-opengl-lifecycle\lifecycle-1793256102059\RECOVERY.md`
- `%TEMP%\charon-opengl-lifecycle\lifecycle-1793256102059\contracts.json`
- `%TEMP%\charon-opengl-sprites\sprite-contract.md`
- `%TEMP%\charon-opengl-tiles\CONTRACTS.md`

The terrain scratch directory also contains a literal recovered wall-corner data
table. Keep that payload local and out of version control; use the user's loaded
original data or derive native geometry from the established format instead.

## Build and checks

From a Visual Studio 2022 developer shell, configure with:

```powershell
cmake -S . -B ../Charon-build-opengl -G "Visual Studio 17 2022" -A Win32 -T v143 `
  -DCHARON_BUILD_OPENGL_TESTS=ON '-DCHARON_ORIGINAL_GAME=D:\Diablo II\Game.exe'
cmake --build ../Charon-build-opengl --config Debug --parallel 4
ctest --test-dir ../Charon-build-opengl -C Debug --output-on-failure
cmake --build ../Charon-build-opengl --config Release --parallel 4
ctest --test-dir ../Charon-build-opengl -C Release --output-on-failure
```

The original comparison is optional and accepts only the exact SHA-256 above.
It opens the executable read-only, maps its sections without invoking entry,
imports, TLS, or initialization, and applies PE32 relocation fixups to private
copy-on-write pages. Only the original RLE routine and its original memcpy
descendant execute. No game files, settings, or saves are changed.

Validation on 2026-10-29:

- Full Debug/Release x86 DLL builds passed with MSVC v143 (19.44.35223),
  Windows SDK 10.0.26100.0. The configured v142 toolset remains untested.
- Four CTest suites passed in both configurations: native GPU, sprite codec,
  terrain format/geometry/callback-state, and original sprite RLE comparison.
- GPU checks execute real OpenGL 3.3 shaders for palette/chromakey, RGB tint,
  alpha/additive/multiply blending, aspect-preserving output, all three filters,
  both logical canvas sizes, deferred deletion, and context recreation. These
  are synthetic properties, not original-renderer image comparisons.
- Sprite RLE comparison passed 2,048 synthetic cases (1,024 seeded streams,
  both orientations), with A/A repeatability, candidate execution and A/B output
  equality, output-buffer guards, and rejected altered-output controls. Inputs
  cover dimensions 1..256 and valid literal/skip runs. This verifies decoded
  index bytes at this boundary, not the sprite drawing/cache callbacks or
  incidental registers, timing, or whole-game behavior.
- Sprite, terrain, and device synthetic suites also passed separately compiled
  with MSVC AddressSanitizer (`/fsanitize=address /MT /Zi /Od`). Debug synthetic
  CTest targets use `/RTC1`. MSVC has no UBSan; no UBSan run is claimed.
- The original executable's SHA-256 remains unchanged.
- Independent static review found and corrected three integration differences:
  repeated CreateWindow now succeeds without recreating an existing context,
  GetScreenSize retains the original logical dimensions across GPU teardown,
  and the mode-selection patch preserves the startup error handler at 0x405cc2.
  A local check extracted the production selection helper/patch expression and
  ran the production MemoryPatch emitter against a synthetic allocated page;
  mode/config, exact 15-byte extent, untouched handler region, call/jump targets,
  and restoration passed with `/RTC1`. The check is at
  `D:\Charon-build-opengl\selection-check.cpp`. Live failure/activation paths
  remain untested. Both DLL configurations rebuilt after these corrections.

## Outstanding integration evidence

Colored-lighting check: the user confirmed visible blue waypoint illumination in
a darker area on the installed OpenGL build. Read-only archive inspection also
confirmed authored blue waypoint RGB, and static review found no lost RGB in the
native terrain/sprite path. No lighting change was needed. This is a live user
observation, not a controlled original-Glide/OpenGL image comparison.

Floor-edge blur correction: with Nearest presentation, the user reported sharp
tile centers but blurry edges, unlike Charon's D3D and GDI. Original edge-floor
drawing passes filtering sentinel 1 at `0x004de6a1`; floor layer 1 can also request
it from LevelsTxt FloorFilter (`0x004de879`, `0x004de8b0..c0`). The final ground
callback argument is this sentinel, not a dereferenced tile pointer. Glide
`0x0050d6e6..f6` selects bilinear when it is nonzero. The native callback formerly
copied this behavior, so presentation Nearest could not remove the blur already
in the logical canvas. Ground now ignores the legacy filter sentinel in both
quality paths. Original unconditional UV initialization `0x0050d540` is unchanged.

Regression evidence: the real ground callback, with a synthetic cached floor,
failed its point-sampling assertion before the fix and passes afterward for
both sentinel values and both quality paths. A GPU control reproduces the dark
intermediate border colors from bilinear sampling against transparent black;
point sampling retains the exact source/background colors. All four suites and
both DLL configurations pass. In-game confirmation of this correction is pending.

- Capability probe: `%TEMP%\charon-opengl-probe\probe.cpp`, compiled with MSVC
  v143 for x86 (`/std:c++17 /EHsc /W4 /MT /O2`, linked with opengl32/gdi32/user32).
  Running `probe.exe` passed the 32-bit context, version/profile, texture-size,
  function-resolution, and GL-error checks. Its window remained hidden and it
  never loaded the game. This is a capability check, not a rendering comparison.
- No full original-only renderer reference runs have been recorded yet.
- Before claiming rendering equivalence: record repeatable original-only cases,
  establish repeatability, reject a deliberately altered expectation, and compare
  output and relevant state against the candidate with evidence it actually ran.
- Before claiming integration: verify the live callback seams and combined menu,
  gameplay, palette/lighting, resolution, fullscreen, and shutdown scenarios.
- Shader appearance and fullscreen presentation are intentional changes; compare
  unfiltered logical-canvas results separately from filtered output.
- Original cutscenes, software backbuffer consumers, mouse coordinate conversion,
  and coexistence with the existing drawing hooks still require investigation.
- Bink surface 2's four-byte BGRX conversion and success return were checked
  independently against the user's codec DLL (SHA-256
  `0475cd223e29bfa06c5672a2ad0e2c04f0ff0b0f17b6173595db33ff11638cda`).
  Movie timing/lifetime and live codec callbacks remain untested. Meaningful
  SetGamma return semantics, statistics layout, and scratch-register/stale-stack
  observability remain unresolved.
- Terrain cache handles same-thread residency eviction and queues GPU deletion.
  It deliberately rejects CPU reloads after an eviction on a foreign thread;
  whether stock asynchronous loaders can reach that path is not established.

## First live acceptance pass

The backend always selects mode 4. **Display Mode** in F11 or **Alt+Enter** switches
between windowed and borderless fullscreen immediately and saves the selection.
Fullscreen is the default; returning to windowed restores the previous placement.
Common window creation stays windowed; native fullscreen changes only the physical
window. The saved mode is applied at the first scene or movie presentation, after
common window creation completes. The cursor warp branch at `0x4687b1` uses logical coordinates in both modes,
and the stock resolution-change `SetWindowPos` at `0x4f5b8b` is suppressed so canvas
changes do not resize or reposition the output window.

The PlugY fix from commit `014b6fa` now runs in OpenGL's menu and gameplay loops.
It releases 640 x 480 or 800 x 600 clips when the client is larger, with a
foreground/minimized guard. A lock can retain the old origin or logical size after
a window/canvas transition. Correct client-area clips stay intact;
logical dimensions remain 640 x 480 or 800 x 600. Hidden Win32 window checks cover
repeated fullscreen/windowed transitions, placement restoration, and actual GPU
presentation without changing the logical canvas or recreating the context; clip detection
fixtures cover both canvas sizes, valid locks, unrelated locks, and negative monitor
coordinates. Live PlugY and Alt+Enter checks remain pending.

**OpenGL Filter** cycles Nearest,
Bilinear + CAS, Jinc2, xBRZ, ScaleFX, CRT, CRT Curved, and LCD over the completed frame without a restart. The built DLLs
are `Debug/Charon.dll` and `Release/Charon.dll` in this source checkout.

Before claiming a working in-game replacement, use an isolated game/test-save
directory and check startup movies, menus/loading animation, both resolutions,
colored lights and spell effects, transparent walls/shadows, perspective,
automap/tooltips/Charon overlays, mouse alignment at the edges and black borders,
Alt+Tab, filters, save/exit/rejoin, and renderer shutdown/recreation. Compare the
unfiltered logical canvas against original-only runs separately from intentional
display/filter changes. The backend is deployed for user testing; the complete
live acceptance pass remains pending.
