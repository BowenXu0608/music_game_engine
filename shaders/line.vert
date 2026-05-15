#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    vec4  cameraPos;
    vec4  lightDir;
    vec4  lightColor;
    vec4  ambient;
} ubo;

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec4 fragColor;

void main() {
    gl_Position = ubo.viewProj * vec4(inPos, 0.0, 1.0);
    fragColor   = inColor;
}
