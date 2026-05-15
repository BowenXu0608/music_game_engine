#version 450

// Dedicated PBR quad vertex stage. quad.vert applies `pc.uvTransform` to the
// UV and multiplies by `pc.tint`, but for the PBR push reinterpretation those
// byte slots are `mrp` / `baseColor` — applying them as a UV transform would
// corrupt sampling. UV is already baked per-vertex by QuadBatch::drawQuad, so
// this stage passes inUV straight through and emits world position for the
// fragment-side BRDF / derivative TBN.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    vec4  cameraPos;
    vec4  lightDir;
    vec4  lightColor;
    vec4  ambient;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4  model;
    vec4  baseColor;
    vec4  mrp;
    vec4  emissive;
    uint  kind;
    uint  _pad[3];
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec3 fragWorldPos;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 0.0, 1.0);
    gl_Position  = ubo.viewProj * worldPos;
    fragUV       = inUV;                       // already baked per-vertex
    fragColor    = inColor * pc.baseColor;
    fragWorldPos = worldPos.xyz;
}
