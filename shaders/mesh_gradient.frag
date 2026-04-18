#version 450

// Gradient: two-colour blend across the mesh surface.
//   top    = per-vertex tint (fragColor)
//   params = [bottomR, bottomG, bottomB, mode]   mode: 0 = vertical, 1 = radial
// Uses fragUV for the blend coordinate, so meshes that want a gradient must
// author UVs that run 0→1 along the desired direction.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    float time;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4  model;
    vec4  tint;
    vec4  uvTransform;
    vec4  params;
    uint  kind;
    uint  _pad[3];
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec4  tex = texture(texSampler, fragUV);
    vec3  top = fragColor.rgb;
    vec3  bot = pc.params.rgb;
    float t;
    if (pc.params.w > 0.5) {
        t = clamp(distance(fragUV, vec2(0.5)) / 0.707, 0.0, 1.0);
    } else {
        t = clamp(fragUV.y, 0.0, 1.0);
    }
    vec3 rgb = mix(top, bot, t);
    outColor = vec4(rgb, fragColor.a) * tex;
    if (outColor.a < 0.01) discard;
}
