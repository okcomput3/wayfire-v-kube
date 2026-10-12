#pragma once
// Alternative grade of the original volumetric shader. Original remains unchanged.
static const char *alternative_fragment_shader = R"(
#version 100
precision highp float;

uniform float u_time;
uniform vec2 u_resolution;
uniform vec2 u_uv_origin;
uniform vec2 u_uv_scale;
varying vec2 v_uv;

#define iterations 4
#define formuparam2 0.89
#define volsteps 10
#define stepsize 0.190
#define zoom 3.900
#define tile 0.450
#define speed2 0.010
#define brightness 0.2
#define darkmatter 0.400
#define distfading 0.560
#define saturation 0.650
#define transverseSpeed 1.1
#define cloud 0.2

float field(in vec3 p, float strength, vec3 field_offset) {
    float accum = 0.;
    float prev = 0.;
    float tw = 0.;

    for (int i = 0; i < 6; ++i) {
        float mag = max(dot(p, p), 0.00001);
        p = abs(p) / mag + field_offset;
        float w = exp(-float(i) / 7.0);
        accum += w * exp(-strength * pow(abs(mag - prev), 2.3));
        tw += w;
        prev = mag;
    }
    return max(0.0, 5.0 * accum / tw - 0.7);
}

void main() {
    vec2 iResolution = u_resolution;
    float iTime = u_time * 0.65;

    vec2 fragCoord = (u_uv_origin + v_uv * u_uv_scale) * iResolution;
    vec2 uv2 = 2.0 * fragCoord.xy / iResolution.xy - 1.0;
    vec2 uvs = uv2 * iResolution.xy / max(iResolution.x, iResolution.y);

    float time2 = iTime;
    float speed = 0.005 * cos(time2 * 0.02 + 3.1415926 / 4.0);
    float formuparam = formuparam2;

    vec2 uv = uvs;
    float a_xz = 0.9;
    float a_yz = -0.6;
    float a_xy = 0.9 + iTime * 0.04;

    mat2 rot_xz = mat2(cos(a_xz), sin(a_xz), -sin(a_xz), cos(a_xz));
    mat2 rot_yz = mat2(cos(a_yz), sin(a_yz), -sin(a_yz), cos(a_yz));
    mat2 rot_xy = mat2(cos(a_xy), sin(a_xy), -sin(a_xy), cos(a_xy));

    vec3 dir = vec3(uv * zoom, 1.0);
    vec3 from = vec3(0.0, 0.0, 0.0);

    from.x -= 2.5;
    from.y -= 2.5;

    vec3 forward = vec3(0.0, 0.0, 1.0);

    from.x += transverseSpeed * cos(0.01 * iTime) + 0.001 * iTime;
    from.y += transverseSpeed * sin(0.01 * iTime) + 0.001 * iTime;
    from.z += 0.003 * iTime;

    dir.xy *= rot_xy;
    forward.xy *= rot_xy;
    dir.xz *= rot_xz;
    forward.xz *= rot_xz;
    dir.yz *= rot_yz;
    forward.yz *= rot_yz;

    from.xy *= -rot_xy;
    from.xz *= rot_xz;
    from.yz *= rot_yz;

    float zooom = (time2 - 3311.0) * speed;
    from += forward * zooom;
    float sampleShift = mod(zooom, stepsize);
    float zoffset = -sampleShift;
    sampleShift /= stepsize;

    float s = 0.24;
    float s3 = s + stepsize / 2.0;
    vec3 v = vec3(0.0);
    float t3 = 0.0;

    float field_strength = 7.0 + 0.03 * log(1.e-6 + fract(sin(u_time) * 4373.11));
    vec3 field_offset = vec3(-0.5, -0.8 + 0.1 * sin(u_time * 0.2 + 2.0),
        -1.1 + 0.3 * cos(u_time * 0.15));
    vec3 backCol2 = vec3(0.0);
    for (int r = 0; r < volsteps; r++) {
        vec3 p2 = from + (s + zoffset) * dir;
        vec3 p3 = (from + (s3 + zoffset) * dir) * (1.9 / zoom);

        p2 = abs(vec3(tile) - mod(p2, vec3(tile * 2.0)));
        p3 = abs(vec3(tile) - mod(p3, vec3(tile * 2.0)));

        t3 = field(p3, field_strength, field_offset);

        float pa, a = pa = 0.0;
        for (int i = 0; i < iterations; i++) {
            p2 = abs(p2) / max(dot(p2, p2), 0.00001) - formuparam;
            // With four iterations only the last two lengths contribute.
            if (i == 2) pa = length(p2);
            if (i == 3) a = abs(length(p2) - pa);
        }

        a *= a * a;
        float s1 = s + zoffset;
        float fade = pow(distfading, max(0.0, float(r) - sampleShift));

        v += fade;

        if (r == 0)
            fade *= (1.0 - sampleShift);
        if (r == volsteps - 1)
            fade *= sampleShift;

        v += vec3(s1, s1 * s1, s1 * s1 * s1 * s1) * a * brightness * fade;
        // Retain the original volumetric field, with cool cloud interiors
        // and gold emission concentrated along its bright fractal ridges.
        vec3 blue = vec3(0.025, 0.16, 0.48) * t3 * t3
            * (1.0 - 0.85 * smoothstep(0.8, 1.8, t3));
        vec3 gold = vec3(1.0, 0.43, 0.09) * pow(max(t3-0.45,0.0),3.0);
        vec3 hot = vec3(1.0,0.83,0.48) * pow(max(t3-1.05,0.0),4.0);
        backCol2 += (blue + gold*1.1 + hot*.35) * fade;

        s += stepsize;
        s3 += stepsize;
    }

    v = mix(vec3(length(v)), v, saturation);
    vec4 forCol2 = vec4(v * 0.01, 1.0);
    backCol2 *= cloud;

    vec3 color=forCol2.rgb + backCol2*.85;
    // Darken low-density space without losing the original moving star field.
    color=max(color-vec3(.016,.012,.008),vec3(0));
    color=vec3(1)-exp(-color*1.35);
    gl_FragColor=vec4(color,1.0);
}
)";
