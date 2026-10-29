#pragma once

namespace CharonGL {
// Generic older desktop LCD. The fixed 4 ms modern-panel baseline is not a display calibration.
inline constexpr const char* LcdResponseShader = R"GLSL(#version 330 core
uniform sampler2D image;
uniform sampler2D history;
uniform float gammaValue;
uniform float frameSeconds;
uniform float frcPhaseEnd;
uniform int resetHistory;
out vec4 fragment;

vec3 dutyIntegral(vec3 t, vec3 steps) {
    return floor(t/4.0)*steps+clamp(mod(t,4.0),vec3(0.0),steps);
}

vec3 panelTarget(vec3 source, vec2 uv, ivec2 p) {
    vec3 drive = pow(clamp(source,0.0,1.0),vec3(1.0/gammaValue));
    vec3 quantized = drive*63.0;
    vec3 low = floor(quantized)/63.0;
    vec3 high = min(low+1.0/63.0,vec3(1.0));
    vec3 steps = clamp(floor(fract(quantized)*4.0+0.5),0.0,4.0);
    float spatialPhase = (p.y%2 == 0) ? float((p.x%2)*2) : float(3-p.x%2*2);
    vec3 end = vec3(frcPhaseEnd+spatialPhase)+vec3(0.0,1.0,2.0);
    float elapsed = max(frameSeconds,0.0)*60.0;
    vec3 highFraction = steps/4.0;
    if (resetHistory == 0) {
        if (elapsed > 0.0) {
            highFraction = clamp((dutyIntegral(end,steps)-dutyIntegral(end-elapsed,steps))/elapsed,0.0,1.0);
        } else {
            highFraction = vec3(lessThan(mod(end,4.0),steps));
        }
    }
    vec3 exponent = vec3(2.05,2.20,2.35)+(2.0*uv.y-1.0)*vec3(0.17,0.20,0.23);
    // FRC alternates optical transmission, so shape each voltage before averaging.
    vec3 shaped = mix(pow(low,exponent),pow(high,exponent),highFraction);
    vec3 limited = vec3(dot(shaped,vec3(0.94,0.045,0.015)),
        dot(shaped,vec3(0.025,0.95,0.025)),dot(shaped,vec3(0.02,0.055,0.925)));
    return vec3(0.002,0.0022,0.0028)+0.96*limited;
}

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec2 uv = (vec2(p)+0.5)/vec2(textureSize(history,0));
    vec3 goal = panelTarget(texture(image,uv).rgb,uv,p);
    vec3 state = goal;
    if (resetHistory == 0) {
        vec3 previous = texelFetch(history,p,0).rgb;
        vec3 dark = (1.0-goal)*(1.0-goal);
        vec3 responseMs = mix(28.8+22.4*dark,22.4+16.0*dark,greaterThanEqual(goal,previous));
        // Subtract the nominal modern panel's mean settling lag; this is an approximation.
        vec3 tau = (responseMs-4.0)/(1000.0*log(9.0));
        state = frameSeconds > 0.0 ? goal+(previous-goal)*exp(-frameSeconds/tau) : previous;
    }
    fragment = vec4(state,1.0);
}
)GLSL";

inline constexpr const char* LcdOutputShader = R"GLSL(#version 330 core
uniform sampler2D image;
uniform vec2 outputSize;
uniform vec2 outputOrigin;
out vec4 fragment;

float apertureIntegral(float x, float start, float end) {
    return floor(x)*(end-start)+clamp(fract(x)-start,0.0,end-start);
}

float apertureCoverage(float center, float footprint, float start, float end) {
    return (apertureIntegral(center+0.5*footprint,start,end)
        -apertureIntegral(center-0.5*footprint,start,end))/footprint/(end-start);
}

void main() {
    vec2 uv = (gl_FragCoord.xy-outputOrigin)/outputSize;
    vec2 panelSize = vec2(textureSize(image,0));
    vec2 panelPosition = uv*panelSize;
    vec2 footprint = panelSize/outputSize;
    vec3 apertures = vec3(apertureCoverage(panelPosition.x,footprint.x,0.025,0.308333),
        apertureCoverage(panelPosition.x,footprint.x,0.358333,0.641667),
        apertureCoverage(panelPosition.x,footprint.x,0.691667,0.975));
    float rows = apertureCoverage(panelPosition.y,footprint.y,0.035,0.965);
    vec3 transmission = texture(image,uv).rgb*mix(vec3(1.0),apertures,0.055)*mix(1.0,rows,0.10);
    vec2 n = 2.0*uv-1.0;
    float edge = pow(abs(n.x),8.0)+0.75*pow(abs(n.y),10.0);
    vec2 patchPosition = (uv-vec2(0.18,0.20))*vec2(2.8,4.0);
    float patch = exp(-dot(patchPosition,patchPosition));
    float gain = 1.0-0.018*(n.x*n.x+0.6*n.y*n.y)+0.012*patch;
    float leak = 0.0007*edge+0.00025*patch;
    vec3 light = transmission*gain+vec3(1.0,0.98,0.92)*leak;
    fragment = vec4(pow(clamp(light,0.0,1.0),vec3(1.0/2.2)),1.0);
}
)GLSL";
}
