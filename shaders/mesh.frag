#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    // Unlit + glow: base color with rim highlight
    vec3 normal = normalize(fragNormal);
    float rim   = 1.0 - abs(dot(normal, vec3(0.0, 0.0, 1.0)));
    rim = pow(rim, 3.0);

    vec3 baseColor = fragColor.rgb;
    vec3 glow      = baseColor * rim * 2.0;
    outColor = vec4(baseColor + glow, fragColor.a);

    if (outColor.a < 0.01) discard;
}
