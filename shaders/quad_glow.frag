#version 450

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    float time;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4  model;
    vec4  tint;          // unused — per-vertex tint is live via fragColor
    vec4  uvTransform;   // unused — UV transform is baked per-vertex
    vec4  params;        // [intensity, falloff, hdrCap, _]
    uint  kind;
    uint  _pad[3];
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;  // per-vertex tint

layout(location = 0) out vec4 outColor;

void main() {
    vec4  texColor  = texture(texSampler, fragUV);
    vec4  base      = texColor * fragColor;
    float intensity = pc.params.x;
    float hdrCap    = pc.params.z > 0.0 ? pc.params.z : 8.0;

    // Additive emissive boost using the per-vertex tint color. Left HDR (capped)
    // so the post-process bloom chain can pick it up.
    vec3 emissive = fragColor.rgb * intensity;
    vec3 rgb = min(base.rgb + emissive, vec3(hdrCap));
    outColor = vec4(rgb, base.a);
    if (outColor.a < 0.01) discard;
}
