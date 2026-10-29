#pragma once
// AMD FidelityFX CAS, MIT; see LICENSE.md.
// Source: https://github.com/GPUOpen-Effects/FidelityFX-CAS/blob/master/ffx-cas/ffx_cas.h
// GLSL 3.30 translation of CasFilter(noScaling=true), CAS_GO_SLOWER + CAS_SLOW,
// without CAS_BETTER_DIAGONALS: exact per-channel reciprocals/sqrt, five cross samples.
// Input must already be bilinearly scaled to the output viewport size (1:1 sampling).
// Fixed sharpness 0.2; upstream UNORM gamma-2.0 linearization/inverse, then game gamma.
// Clamped fetches and a black-channel NaN guard keep edge/zero inputs defined.
namespace CharonGL { inline constexpr const char* CasShader =
R"CHARON_CAS(#version 330 core
/*
Copyright (c) 2017-2019 Advanced Micro Devices, Inc. All rights reserved.
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy,
modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/
in vec2 uv;
uniform sampler2D image;
uniform float gammaValue;
out vec4 fragment;

vec3 source(ivec2 coordinate)
{
    vec3 color = texelFetch(image, clamp(coordinate, ivec2(0), textureSize(image, 0) - 1), 0).rgb;
    return color * color;
}

void main()
{
    ivec2 coordinate = ivec2(floor(uv * vec2(textureSize(image, 0))));
    vec3 b = source(coordinate + ivec2(0, -1));
    vec3 d = source(coordinate + ivec2(-1, 0));
    vec3 e = source(coordinate);
    vec3 f = source(coordinate + ivec2(1, 0));
    vec3 h = source(coordinate + ivec2(0, 1));

    vec3 mn = min(min(min(d, e), f), min(b, h));
    vec3 mx = max(max(max(d, e), f), max(b, h));
    vec3 amplitude = sqrt(clamp(min(mn, vec3(1.0) - mx) / max(mx, vec3(1e-6)), 0.0, 1.0));
    const float peak = -1.0 / (8.0 - 3.0 * 0.2);
    vec3 weight = amplitude * peak;
    vec3 color = clamp((b * weight + d * weight + f * weight + h * weight + e) / (vec3(1.0) + 4.0 * weight), 0.0, 1.0);
    fragment = vec4(pow(sqrt(color), vec3(1.0 / gammaValue)), 1.0);
}
)CHARON_CAS";
}
