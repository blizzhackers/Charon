#pragma once

namespace CharonGL {
inline constexpr const char* CrtShader = R"GLSL(#version 330 core
uniform sampler2D image;
uniform vec2 outputSize;
uniform vec2 outputOrigin;
uniform float gammaValue;
uniform float curvature;
out vec4 fragment;

vec3 source(ivec2 pixel) {
    vec3 color = texelFetch(image, clamp(pixel, ivec2(0), textureSize(image, 0) - 1), 0).rgb;
    return pow(color,vec3(2.2));
}

vec3 reconstruct(ivec2 pixel, vec2 weight) {
    return mix(mix(source(pixel), source(pixel + ivec2(1, 0)), weight.x),
        mix(source(pixel + ivec2(0, 1)), source(pixel + ivec2(1, 1)), weight.x), weight.y);
}

void main() {
    vec2 size = vec2(textureSize(image, 0));
    vec2 center = gl_FragCoord.xy - outputOrigin;
    vec2 outputUv = center / outputSize;
    vec2 edge = outputUv * 2.0 - 1.0;
    vec2 sampleUv = outputUv;
    vec2 sampleCenter = center;
    float coverage = 1.0;
    float verticalStretch = 1.0;
    if (curvature > 0.0) {
        // Bow the screen inward at the corners without discarding source pixels.
        vec2 stretch = 1.0 + curvature * edge.yx * edge.yx;
        vec2 warped = edge * stretch;
        vec2 boundary = (1.0 - abs(warped)) * 0.5 * outputSize / stretch;
        coverage = clamp(min(boundary.x, boundary.y) + 0.5, 0.0, 1.0);
        if (coverage <= 0.0) { fragment = vec4(0.0, 0.0, 0.0, 1.0); return; }
        sampleUv = warped * 0.5 + 0.5;
        sampleCenter = sampleUv * outputSize;
        verticalStretch = stretch.y;
    }
    // Exact pixel-center remainders keep interpolation drift out of black channels.
    vec2 numerator = sampleCenter * size - 0.5 * outputSize;
    ivec2 pixel = ivec2(floor(numerator / outputSize));
    vec2 weight = clamp((numerator - vec2(pixel) * outputSize) / outputSize, 0.0, 1.0);
    vec3 middle = reconstruct(pixel, weight);
    vec3 left = reconstruct(pixel + ivec2(-1, 0), weight);
    vec3 right = reconstruct(pixel + ivec2(1, 0), weight);
    vec3 above = reconstruct(pixel + ivec2(0, -1), weight);
    vec3 below = reconstruct(pixel + ivec2(0, 1), weight);
    // A broad horizontal beam and bright-neighbor halation model a softer tube.
    vec3 color = 0.72 * middle + 0.14 * (left + right);
    // Preserve the old encoded glow threshold while using a 2.2 working gamma.
    const float glowThreshold = 0.09707320544;
    vec3 glow = 0.25 * (max(left - glowThreshold, 0.0) + max(right - glowThreshold, 0.0) +
        max(above - glowThreshold, 0.0) + max(below - glowThreshold, 0.0));

    float scale = outputSize.y / (size.y * verticalStretch);
    float visibility = smoothstep(1.0, 1.5, scale);
    float footprint = 3.14159265359 / scale;
    // Integrate scanlines over each output pixel; fade them out when they cannot resolve.
    float attenuation = sin(footprint) / footprint;
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float depth = mix(0.82, 0.50, brightness) * visibility;
    // A quarter-row phase keeps the beam visible at exact 2x scaling, too.
    float row = (1.0 - sampleUv.y) * size.y - 0.25;
    float beam = 1.0 - 0.5 * depth * (1.0 + attenuation * cos(6.28318530718 * row));
    // Restore most of the scanline light loss while retaining a little CRT dimming.
    beam /= 1.0 - 0.40 * depth;

    // Keep the coarse, staggered phosphor mask visible even at native window size.
    int stagger = (int(center.y) / 2) % 2;
    vec3 mask = vec3(0.50);
    mask[(int(center.x) + stagger) % 3] = 1.50;
    float vignette = 1.0 - 0.10 * dot(edge, edge);
    color = (1.35 * color * beam * mask + 0.18 * glow) * vignette;
    // Match value and slope at the shoulder; preserve shadows and roll highlights toward white.
    vec3 excess = max(color-0.75,0.0);
    color = min(color,0.75)+0.25*(1.0-exp(-4.0*excess));
    color = pow(clamp(color,0.0,1.0),vec3(1.0/2.2));
    fragment = vec4(pow(color, vec3(1.0 / gammaValue)) * coverage, 1.0);
}
)GLSL";
}
