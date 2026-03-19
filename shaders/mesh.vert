#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    float time;
} ubo;

layout(push_constant) uniform PushConstants {
    mat4 model;
    vec4 tint;
} pc;

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inColor;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out vec3 fragNormal;

void main() {
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position = ubo.viewProj * worldPos;
    fragUV     = inUV;
    fragColor  = inColor * pc.tint;
    fragNormal = mat3(transpose(inverse(pc.model))) * inNormal;
}
