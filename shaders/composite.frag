#version 450

layout(set = 0, binding = 0) uniform sampler2D sceneTex;
layout(set = 0, binding = 1) uniform sampler2D bloomTex;

layout(push_constant) uniform PC {
    float bloomStrength;
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 scene = texture(sceneTex, fragUV).rgb;
    vec3 bloom = texture(bloomTex, fragUV).rgb;
    outColor = vec4(scene + bloom * pc.bloomStrength, 1.0);
}
