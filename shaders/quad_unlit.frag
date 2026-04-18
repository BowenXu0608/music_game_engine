#version 450

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

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, fragUV);
    outColor = texColor * fragColor;
    if (outColor.a < 0.01) discard;
}
