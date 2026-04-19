#version 450
layout(set = 0, binding = 0) uniform FrameUBO { mat4 viewProj; float time; } ubo;
layout(set = 1, binding = 0) uniform sampler2D texSampler;
layout(push_constant) uniform PC {
    mat4  model;
    vec4  tint;
    vec4  uvTransform;
    vec4  params;
    uint   kind;
    uint   _pad[3];
} pc;
layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    float pulse = 0.5 + 0.5 * sin(ubo.time * 2.0);
    vec3 baseColor = vec3(0.6, 0.2, 1.0); // purple placeholder
    vec4 texCol = texture(texSampler, fragUV);
    vec3 col = baseColor * pc.tint.rgb * pulse;
    float alpha = 0.8 + 0.2 * sin(ubo.time * 1.5 + fragUV.y * 6.28);
    outColor = vec4(col, clamp(alpha, 0.1, 1.0));
    if (outColor.a < 0.01) discard;
}