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
    // Oscillator in [0,1]. Change speed/shape as the user asks.
    float pulse = 0.5 + 0.5 * sin(ubo.time * 2.0);
    // Named color goes HERE as a literal vec3. Replace with the user's color.
    vec3 baseColor = vec3(0.0, 1.0, 1.0); // cyan placeholder
    // Optional: sample the texture. Keep this line even if you don't use texCol.
    vec4 texCol = texture(texSampler, fragUV);
    // Final color. Must multiply by pc.tint.rgb so the tint slider works.
    vec3 col = baseColor * pc.tint.rgb * pulse;
    float alpha = 0.8 + 0.2 * sin(ubo.time * 2.0 + fragUV.y * 6.28);
    outColor = vec4(col, clamp(alpha, 0.1, 1.0));
    if (outColor.a < 0.01) discard;
}