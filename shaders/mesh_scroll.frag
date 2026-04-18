#version 450

// Scroll: animates the sampled UV over time.
//   params = [uSpeed, vSpeed, uTile, vTile]

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
    vec2 tile = vec2(pc.params.z > 0.0 ? pc.params.z : 1.0,
                     pc.params.w > 0.0 ? pc.params.w : 1.0);
    vec2 uv   = fragUV * tile + ubo.time * pc.params.xy;
    vec4 tex  = texture(texSampler, uv);
    outColor = tex * fragColor;
    if (outColor.a < 0.01) discard;
}
