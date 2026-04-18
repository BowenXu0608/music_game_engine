#version 450

// Glow: rim-glow lighting (based on facing normal) + additive emissive boost.
//   params = [intensity, falloff, hdrCap, _]
// Left HDR (capped) so the bloom post-process can pick it up.

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
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 tex  = texture(texSampler, fragUV);
    vec4 base = tex * fragColor;

    float intensity = pc.params.x;
    float falloff   = pc.params.y > 0.0 ? pc.params.y : 3.0;
    float hdrCap    = pc.params.z > 0.0 ? pc.params.z : 8.0;

    // Rim glow driven by facing normal — surfaces pointing at the camera get
    // a flat base, surfaces grazing the view get a bright edge.
    vec3  n   = normalize(fragNormal);
    float rim = 1.0 - abs(dot(n, vec3(0.0, 0.0, 1.0)));
    rim = pow(rim, falloff);

    vec3 rimGlow  = base.rgb * rim * 2.0;
    vec3 emissive = fragColor.rgb * intensity;
    vec3 rgb = min(base.rgb + rimGlow + emissive, vec3(hdrCap));
    outColor = vec4(rgb, base.a);
    if (outColor.a < 0.01) discard;
}
