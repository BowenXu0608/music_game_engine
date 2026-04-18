#version 450

// Pulse: amplifies brightness around a recent hit time, decaying over time.
//   params = [lastHitTime, decay, peakMult, _]
// At time == lastHitTime, brightness is multiplied by (1 + peakMult).
// At time == lastHitTime + decay, brightness is ~= 1.0.

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
    vec4  base       = texture(texSampler, fragUV) * fragColor;
    float lastHit    = pc.params.x;
    float decay      = max(pc.params.y, 1e-3);
    float peakMult   = pc.params.z;
    float elapsed    = max(ubo.time - lastHit, 0.0);
    float pulseBoost = peakMult * exp(-elapsed / decay);
    outColor = vec4(base.rgb * (1.0 + pulseBoost), base.a);
    if (outColor.a < 0.01) discard;
}
