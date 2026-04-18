#version 450

// Unlit 3D surface: texture * per-vertex tint.
// No rim glow — use Glow kind if you want the lit look.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    float time;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4  model;
    vec4  tint;          // unused — tint is baked per-vertex via mesh.vert
    vec4  uvTransform;   // unused — UV baked per-vertex
    vec4  params;
    uint  kind;
    uint  _pad[3];
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex = texture(texSampler, fragUV);
    outColor = tex * fragColor;
    if (outColor.a < 0.01) discard;
}
