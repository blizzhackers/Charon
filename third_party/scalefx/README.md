# ScaleFX

The standard five-pass ScaleFX preset by Sp00kyFox, vendored from
[libretro/glsl-shaders](https://github.com/libretro/glsl-shaders/tree/cba01a48b0974a1232bea68d999a7c21bc4c233e/scalefx)
at commit `cba01a48b0974a1232bea68d999a7c21bc4c233e` (resolved through the GitHub
commits API). `upstream/` preserves the preset and all five shader files exactly
as downloaded. Each shader carries its MIT license, also copied to `LICENSE`.

`scalefx.h` exposes `CharonGL::ScaleFxShaders[5]`. Its strings contain the complete
combined upstream vertex/fragment bodies, including copyright and permission
notices. The sole change is removing the initial `#version 130` directive.
Compile each body twice, prepending `#version 330 core` followed by either
`#define VERTEX` or `#define FRAGMENT`, with newlines after both directives.
The upstream compatibility macros select core-compatible inputs, outputs and
texture calls. No algorithm changes are applied.

## Renderer contract

- Bind `VertexCoord` to attribute 0, `TexCoord` to 1, and `COLOR` to 2. Positions
  and texture coordinates are declared as `vec4`; OpenGL supplies the missing
  components when a vertex array provides two coordinates. Set `MVPMatrix` to
  identity for a clip-space fullscreen quad.
- Use nearest sampling and clamp-to-edge on every sampled texture. Disable
  blending and sRGB conversion while writing intermediate data.
- Passes 0 through 3 render at the native canvas size. Outputs 0 and 1 require
  floating-point storage (RGBA32F in Charon); outputs 2 and 3 use RGBA8. All four
  channels are algorithm data, including alpha; do not blend or discard them.
- Pass 4 renders to RGBA8 at three times the native width and height.
- Each pass samples `Texture` from the preceding pass, except pass 0, which
  samples the original scene. Pass 2 additionally samples **pass 0's output**
  through `PassPrev2Texture`; keep that floating-point target alive. Pass 4
  additionally samples the **original scene** through `PassPrev5Texture`.
  Both extra samplers may use texture unit 1; `Texture` may use unit 0.
- Set `InputSize` and `TextureSize` to native canvas dimensions in every pass;
  set `OutputSize` to the current target size. Pass 4 declares
  `PassPrev5TextureSize` and `PassPrev5InputSize` for the original scene size.
  Some declared uniforms and varyings are unused and can be optimized away.
- Leave `PARAMETER_UNIFORM` undefined to retain the standard upstream defaults:
  `SFX_CLR = 0.5`, `SFX_SAA = 1.0`, and `SFX_SCN = 1.0`. `#pragma parameter`
  directives describe these parameters for libretro; Charon uses the defaults.

## Original file SHA-256

```text
9f8494ce092c3d7a1d6ac3d018e7db42d79455c17c2eeaca7c26539c965db06d  upstream/scalefx.glslp
37e534d5e600b5bac5d66f05e78f8688cc4631432d7887502e58e106a15ca40c  upstream/shaders/scalefx-pass0.glsl
5e5f27731d7dc918f1cee610218aead459e62284dc33e22e22ab65f82f03e18c  upstream/shaders/scalefx-pass1.glsl
7899f17fb6c156075d02a2cbf84462c184191229a9e795c6abbc023d64b3600a  upstream/shaders/scalefx-pass2.glsl
0f7290476c4e8e4330ef81ba10a72a6547fc0f1ffa2d18c94cadcc8f1998b9ec  upstream/shaders/scalefx-pass3.glsl
b6ad74bc8211399149166e77264d03eea582772d7f08b2dea9fbc9440d3f84c5  upstream/shaders/scalefx-pass4.glsl
```
