#version 450

// PBR (Disney metallic-roughness subset) for the 3D mesh path. Same BRDF as
// quad_pbr.frag; the geometric normal comes from the interpolated mesh normal
// instead of screen-space derivatives. Optional normal map via a derivative
// TBN (no tangent vertex attribute). See quad_pbr.frag for the push-constant
// byte map.

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4  viewProj;
    vec4  cameraPos;   // xyz = eye world pos, w = time
    vec4  lightDir;    // xyz = light travel direction (world, normalized)
    vec4  lightColor;  // rgb = colour, w = intensity
    vec4  ambient;     // rgb = ambient colour, w = ambient intensity
} ubo;

layout(set = 1, binding = 0) uniform sampler2D baseColorTex;
layout(set = 1, binding = 1) uniform sampler2D normalTex;

layout(push_constant) uniform PushConstants {
    mat4  model;
    vec4  baseColor;
    vec4  mrp;          // x metallic, y roughness, z emissiveIntensity, w flags
    vec4  emissive;     // rgb emissive colour
    uint  kind;
    uint  _pad[3];
} pc;

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;     // inColor * baseColor (baked in vert)
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

vec3 perturbNormal(vec3 N, vec3 wp, vec2 uv, vec3 nT) {
    vec3 dp1 = dFdx(wp),  dp2 = dFdy(wp);
    vec2 du1 = dFdx(uv),  du2 = dFdy(uv);
    vec3 dp2p = cross(dp2, N), dp1p = cross(N, dp1);
    vec3 T = dp2p * du1.x + dp1p * du2.x;
    vec3 B = dp2p * du1.y + dp1p * du2.y;
    float im = inversesqrt(max(dot(T, T), dot(B, B)));
    mat3 TBN = mat3(T * im, B * im, N);
    return normalize(TBN * (nT * 2.0 - 1.0));
}

float distributionGGX(vec3 N, vec3 H, float rough) {
    float a  = rough * rough;
    float a2 = a * a;
    float ndh = max(dot(N, H), 0.0);
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-5);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float rough) {
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    float nv = max(dot(N, V), 0.0);
    float nl = max(dot(N, L), 0.0);
    float gv = nv / (nv * (1.0 - k) + k);
    float gl = nl / (nl * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosT, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}

void main() {
    vec4 texC = texture(baseColorTex, fragUV);
    vec4 base = texC * fragColor;
    if (base.a < 0.01) discard;

    vec3  albedo    = base.rgb;
    float metallic  = clamp(pc.mrp.x, 0.0, 1.0);
    float roughness = clamp(pc.mrp.y, 0.04, 1.0);

    vec3 V  = normalize(ubo.cameraPos.xyz - fragWorldPos);
    vec3 Ng = normalize(fragNormal);
    if (dot(Ng, V) < 0.0) Ng = -Ng;          // pipeline is double-sided

    vec3 N = Ng;
    if (pc.mrp.w >= 0.5) {
        vec3 nT = texture(normalTex, fragUV).xyz;   // linear
        N = perturbNormal(Ng, fragWorldPos, fragUV, nT);
    }

    vec3 L = normalize(-ubo.lightDir.xyz);
    vec3 H = normalize(V + L);

    vec3  F0  = mix(vec3(0.04), albedo, metallic);
    float NDF = distributionGGX(N, H, roughness);
    float G   = geometrySmith(N, V, L, roughness);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3  spec = (NDF * G * F) /
                 max(4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0), 1e-4);
    vec3  kd   = (vec3(1.0) - F) * (1.0 - metallic);

    float ndl      = max(dot(N, L), 0.0);
    vec3  radiance = ubo.lightColor.rgb * ubo.lightColor.w;
    vec3  lit      = (kd * albedo / PI + spec) * radiance * ndl;
    vec3  amb      = ubo.ambient.rgb * ubo.ambient.w * albedo;
    vec3  emis     = pc.emissive.rgb * pc.mrp.z;

    outColor = vec4(lit + amb + emis, base.a);
}
