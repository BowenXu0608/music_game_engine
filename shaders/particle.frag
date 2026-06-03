#version 450

// Built-in particle fragment shader: soft radial-gradient sprite, additive.
// Computes a round glow from the quad UVs so no texture file is needed — the
// white texture is still bound to satisfy the pipeline layout (set 1).
//
// Custom particle shaders (the `Custom` effect kind) replace ONLY this stage.
// Their contract: inputs are `fragUV` (0..1 across the quad), `fragColor`
// (rgba, already tinted), and `fragData` (x = life01 1->0, y = seed); they
// sample `texSampler` (white fallback) at set 1, write `outColor`, and render
// under additive blending. See particle.vert for the vertex interface.

layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec2 fragData;

layout(location = 0) out vec4 outColor;

void main() {
    vec2  d = fragUV * 2.0 - 1.0;     // -1..1 from quad center
    float r = dot(d, d);
    float a = clamp(1.0 - r, 0.0, 1.0);
    a *= a;                            // soft edge falloff
    outColor = vec4(fragColor.rgb, fragColor.a * a);
    if (outColor.a < 0.01) discard;
}
