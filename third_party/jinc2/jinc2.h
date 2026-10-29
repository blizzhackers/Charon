#pragma once
// Hyllian's Jinc2, MIT; see LICENSE.md.
// Source: https://github.com/libretro/glsl-shaders/blob/master/windowed/shaders/jinc2.glsl
// GLSL 3.30 I/O, textureSize, clamped exact-center fetches, final gamma; vertex UV drift omitted.
// Original 16-tap weights (0.44/0.82) and 0.8 anti-ringing are unchanged.
// Snap near-integral lattice coordinates to stabilize half-texel ties after raster interpolation.
namespace CharonGL { inline constexpr const char* Jinc2Shader =
R"CHARON_JINC2(#version 330 core
/*
Hyllian's jinc windowed-jinc 2-lobe sharper with anti-ringing Shader
   
   Copyright (C) 2011-2016 Hyllian/Jararaca - sergiogdb@gmail.com

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
   THE SOFTWARE.
*/
in vec2 uv;
uniform sampler2D image;
uniform float gammaValue;
out vec4 fragment;

vec3 source(vec2 center)
{
    return texelFetch(image, clamp(ivec2(floor(center)), ivec2(0), textureSize(image, 0) - 1), 0).rgb;
}

const   float pi                = 3.1415926535897932384626433832795;
const   float wa                = 0.44*pi;
const   float wb                = 0.82*pi;

// Calculates the distance between two points
float d(vec2 pt1, vec2 pt2)
{
  vec2 v = pt2 - pt1;
  return sqrt(dot(v,v));
}

vec3 min4(vec3 a, vec3 b, vec3 c, vec3 d)
{
    return min(a, min(b, min(c, d)));
}

vec3 max4(vec3 a, vec3 b, vec3 c, vec3 d)
{
    return max(a, max(b, max(c, d)));
}

vec4 resampler(vec4 x)
{
   vec4 res;

   res.x = (x.x==0.0) ?  wa*wb  :  sin(x.x*wa)*sin(x.x*wb)/(x.x*x.x);
   res.y = (x.y==0.0) ?  wa*wb  :  sin(x.y*wa)*sin(x.y*wb)/(x.y*x.y);
   res.z = (x.z==0.0) ?  wa*wb  :  sin(x.z*wa)*sin(x.z*wb)/(x.z*x.z);
   res.w = (x.w==0.0) ?  wa*wb  :  sin(x.w*wa)*sin(x.w*wb)/(x.w*x.w);

   return res;
}

void main()
{

    vec3 color;
    vec4 weights[4];

    vec2 dx = vec2(1.0, 0.0);
    vec2 dy = vec2(0.0, 1.0);

    vec2 pc = uv*vec2(textureSize(image, 0));

    vec2 lattice = pc - vec2(0.5);
    lattice = mix(lattice, round(lattice), lessThan(abs(lattice-round(lattice)),vec2(0.0001)));
    vec2 tc = floor(lattice) + vec2(0.5);
     
    weights[0] = resampler(vec4(d(pc, tc    -dx    -dy), d(pc, tc           -dy), d(pc, tc    +dx    -dy), d(pc, tc+2.0*dx    -dy)));
    weights[1] = resampler(vec4(d(pc, tc    -dx       ), d(pc, tc              ), d(pc, tc    +dx       ), d(pc, tc+2.0*dx       )));
    weights[2] = resampler(vec4(d(pc, tc    -dx    +dy), d(pc, tc           +dy), d(pc, tc    +dx    +dy), d(pc, tc+2.0*dx    +dy)));
    weights[3] = resampler(vec4(d(pc, tc    -dx+2.0*dy), d(pc, tc       +2.0*dy), d(pc, tc    +dx+2.0*dy), d(pc, tc+2.0*dx+2.0*dy)));

    vec3 c00 = source(tc    -dx    -dy);
    vec3 c10 = source(tc           -dy);
    vec3 c20 = source(tc    +dx    -dy);
    vec3 c30 = source(tc+2.0*dx    -dy);
    vec3 c01 = source(tc    -dx       );
    vec3 c11 = source(tc              );
    vec3 c21 = source(tc    +dx       );
    vec3 c31 = source(tc+2.0*dx       );
    vec3 c02 = source(tc    -dx    +dy);
    vec3 c12 = source(tc           +dy);
    vec3 c22 = source(tc    +dx    +dy);
    vec3 c32 = source(tc+2.0*dx    +dy);
    vec3 c03 = source(tc    -dx+2.0*dy);
    vec3 c13 = source(tc       +2.0*dy);
    vec3 c23 = source(tc    +dx+2.0*dy);
    vec3 c33 = source(tc+2.0*dx+2.0*dy);

    //  Get min/max samples
    vec3 min_sample = min4(c11, c21, c12, c22);
    vec3 max_sample = max4(c11, c21, c12, c22);

    color = vec3(dot(weights[0], vec4(c00.x, c10.x, c20.x, c30.x)), dot(weights[0], vec4(c00.y, c10.y, c20.y, c30.y)), dot(weights[0], vec4(c00.z, c10.z, c20.z, c30.z)));
    color+= vec3(dot(weights[1], vec4(c01.x, c11.x, c21.x, c31.x)), dot(weights[1], vec4(c01.y, c11.y, c21.y, c31.y)), dot(weights[1], vec4(c01.z, c11.z, c21.z, c31.z)));
    color+= vec3(dot(weights[2], vec4(c02.x, c12.x, c22.x, c32.x)), dot(weights[2], vec4(c02.y, c12.y, c22.y, c32.y)), dot(weights[2], vec4(c02.z, c12.z, c22.z, c32.z)));
    color+= vec3(dot(weights[3], vec4(c03.x, c13.x, c23.x, c33.x)), dot(weights[3], vec4(c03.y, c13.y, c23.y, c33.y)), dot(weights[3], vec4(c03.z, c13.z, c23.z, c33.z)));
    color = color/(dot(weights[0], vec4(1,1,1,1)) + dot(weights[1], vec4(1,1,1,1)) + dot(weights[2], vec4(1,1,1,1)) + dot(weights[3], vec4(1,1,1,1)));

    // Anti-ringing
    vec3 aux = color;
    color = clamp(color, min_sample, max_sample);
    color = mix(aux, color, 0.8);

    fragment = vec4(pow(max(color, vec3(0.0)), vec3(1.0 / gammaValue)), 1.0);
}
)CHARON_JINC2";
}
