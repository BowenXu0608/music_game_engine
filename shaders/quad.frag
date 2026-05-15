#version 450

// NOTE: Superseded by quad_unlit.frag. Kept for backward-compat with any
// leftover references. Identical to quad_unlit.frag.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    vec4  cameraPos;   // xyz = eye, w = time
    vec4  lightDir;    // xyz = directional light dir (world)
    vec4  lightColor;  // rgb = color, w = intensity
    vec4  ambient;     // rgb = ambient color, w = intensity
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

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragUV);
    outColor = texColor * fragColor;
    if (outColor.a < 0.01) discard;
}
