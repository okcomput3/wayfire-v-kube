#pragma once

static const char* cube_vertex_2_0 =
R"(#version 100
attribute highp vec3 position;
attribute highp vec2 uvPosition;

varying highp vec2 uvpos;
varying highp vec3 lightingPosition;

uniform mat4 VP;
uniform mat4 model;

void main() {
    lightingPosition = (model * vec4(position, 1.0)).xyz;
    gl_Position = VP * vec4(lightingPosition, 1.0);
    uvpos = uvPosition;
})";

static const char* cube_fragment_2_0 =
R"(#version 100
varying highp vec2 uvpos;
varying highp vec3 lightingPosition;
uniform sampler2D smp;
uniform mediump float surface_opacity;
uniform bool depth_only;
uniform bool translucent_only;


uniform bool dynamic_lighting;
uniform highp float lighting_time;
uniform highp float lighting_row;
uniform highp float lighting_radius;
uniform mediump float lighting_strength;

// Three independently phased point lights orbit within this row's volume.
// Apply only to RGB: coverage, transparency, blur masks and depth stay intact.
highp vec3 interiorLights(highp vec3 world) {
    if (!dynamic_lighting) return vec3(0.0);
    highp vec3 p = world - vec3(0.0, lighting_row, 0.0);
    highp float radius = max(lighting_radius, 0.1);
    highp vec3 result = vec3(0.0);
    for (int i = 0; i < 3; ++i) {
        highp float phase = float(i) * 2.0943951 + lighting_row * 0.73;
        highp float angle = lighting_time * (0.8 + float(i) * 0.13) + phase;
        highp vec3 point = vec3(radius * 0.58 * cos(angle),
            0.30 * sin(lighting_time * 0.71 + phase), radius * 0.58 * sin(angle));
        highp vec3 delta = (p - point) / vec3(radius, 0.5, radius);
        highp float attenuation = 1.0 / (1.0 + 5.0 * dot(delta, delta));
        highp vec3 hue = 0.5 + 0.5 * cos(lighting_time * 0.35 + phase + vec3(0.0, 2.0943951, 4.1887902));
        result += hue * attenuation;
    }
    return result * lighting_strength;
}

void main() {
    mediump vec4 color = texture2D(smp, uvpos);
    color *= surface_opacity;
    if (depth_only && color.a < 0.999) discard;
    if (translucent_only && color.a >= 0.999) discard;
    // Transparent workspace pixels must not occlude other cube faces.
    if (color.a <= 0.0) {
        discard;
    }
    color.rgb = min(vec3(color.a), color.rgb + color.a * interiorLights(lightingPosition));
    gl_FragColor = color;
})";
