#version 450

// Particle vertex shader. Mirrors quad.vert but carries a per-vertex `data`
// channel (x = life01, y = seed) so custom particle fragment shaders can drive
// life-based animation without disturbing the shared QuadVertex layout.
// Particles are emitted in whatever space the active camera uses (Bandori =
// screen pixels through an ortho projection; Arcaea = world space), so a single
// viewProj transform serves every mode.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    vec4  cameraPos;   // xyz = eye, w = time
    vec4  lightDir;
    vec4  lightColor;
    vec4  ambient;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4  model;
    vec4  tint;
    vec4  uvTransform;
    vec4  params;
    uint  kind;
    uint  _pad[3];
} pc;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec2 inData;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec2 fragData;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 0.0, 1.0);
    gl_Position   = ubo.viewProj * worldPos;
    fragUV        = inUV;
    fragColor     = inColor * pc.tint;
    fragData      = inData;
}
