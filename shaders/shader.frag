#version 450

#define PI 3.14159265359

layout(location = 0) in vec3 fragPosition;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec4 fragPosLightSpace;
layout(location = 4) flat in float fragEmissiveTarget;

layout(location = 0) out vec4 outColor;

struct Light {
    vec3 position;
    vec3 color;
    float intensity;
};

layout(set = 1, binding = 0) uniform MaterialUBO {
    vec3 albedo;
    float metallic;
    float roughness;
    float ao;
    float ior;
    float opacity;
    Light lights[4];
    vec3 ambientLight;
    vec3 cameraPos;
    vec3 emissive;
    float emissiveStrength;
} mat;

layout(set = 2, binding = 0) uniform sampler2D shadowMap;

float calculateShadow(vec4 posLightSpace, vec3 N, vec3 L) {
    // 透视除法（正交投影 w=1）
    vec3 projCoords = posLightSpace.xyz / posLightSpace.w;
    // xy 从 NDC [-1,1] 转到 [0,1]，z 已经是 [0,1]（Vulkan）
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    // 视锥外不产生阴影
    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x > 1.0 || projCoords.x < 0.0 ||
        projCoords.y > 1.0 || projCoords.y < 0.0)
        return 0.0;

    float currentDepth = projCoords.z;
    // 斜率相关的 bias 减少阴影 acne
    float bias = max(0.005 * (1.0 - dot(N, L)), 0.0015);

    // 3x3 PCF 软阴影
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(2048.0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float closestDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias) > closestDepth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    return a2 / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(mat.cameraPos - fragPosition);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, mat.albedo, mat.metallic);

    vec3 Lo = vec3(0.0);

    for (int i = 0; i < 4; i++) {
        vec3 L = normalize(mat.lights[i].position - fragPosition);
        vec3 H = normalize(V + L);

        float distance = length(mat.lights[i].position - fragPosition);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = mat.lights[i].color * mat.lights[i].intensity * attenuation;

        float NdotL = max(dot(N, L), 0.0);

        float NDF = DistributionGGX(N, H, mat.roughness);
        float G = GeometrySmith(N, V, L, mat.roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - mat.metallic;

        vec3 diffuse = kD * mat.albedo / PI;

        // 仅主光源 (i=0) 投射阴影
        float shadowFactor = 1.0;
        if (i == 0) {
            float shadow = calculateShadow(fragPosLightSpace, N, L);
            shadowFactor = 1.0 - shadow;
        }
        Lo += (diffuse + specular) * radiance * NdotL * shadowFactor;
    }

    vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - mat.metallic;

    vec3 ambient = (kD * mat.albedo) * mat.ambientLight * mat.ao;
    vec3 color = ambient + Lo;

    color += mat.emissive * mat.emissiveStrength * fragEmissiveTarget;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    outColor = vec4(color, mat.opacity);
}
