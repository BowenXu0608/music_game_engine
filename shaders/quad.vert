#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    float time;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4  model;        // 64 B
    vec4  tint;         // 16 B
    vec4  uvTransform;  // 16 B — xy=offset, zw=scale
    vec4  params;       // 16 B — per-kind
    uint  kind;         //  4 B
    uint  _pad[3];      // 12 B → total 128 B
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 0.0, 1.0);
    gl_Position = ubo.viewProj * worldPos;
    fragUV    = inUV * pc.uvTransform.zw + pc.uvTransform.xy;
    fragColor = inColor * pc.tint;
}
