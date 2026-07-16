// ============================================================================
// PBR Fragment Shader - 基于物理的渲染
// ============================================================================
// 实现 Cook-Torrance BRDF:
//   - GGX 法线分布函数 (Normal Distribution Function)
//   - Smith-Schlick 几何函数 (Geometry Function)
//   - Fresnel-Schlick 菲涅尔近似
// ============================================================================

#version 450

#define PI 3.14159265359

// 从顶点着色器接收
layout(location = 0) in vec3 fragPosition;   // 世界空间位置
layout(location = 1) in vec3 fragNormal;     // 世界空间法线
layout(location = 2) in vec2 fragUV;         // UV 坐标

// 输出颜色
layout(location = 0) out vec4 outColor;

// 光源定义
struct Light {
    vec3 position;
    vec3 color;
    float intensity;
};

// Uniform: PBR 材质参数 + 光源
layout(set = 1, binding = 0) uniform MaterialUBO {
    vec3 albedo;        // 基础颜色
    float metallic;     // 金属度 [0, 1]
    float roughness;    // 粗糙度 [0, 1]
    float ao;           // 环境遮蔽
    float ior;          // 折射率 (玻璃 ~1.5)
    float opacity;      // 透明度 [0,1]
    Light lights[4];    // 最多 4 个光源
    vec3 ambientLight;  // 环境光
    vec3 cameraPos;     // 相机世界坐标
} mat;

// ============================================================================
// PBR 核心函数
// ============================================================================

// GGX 法线分布函数 (Trowbridge-Reitz)
// 描述微表面法线分布, roughness 越大分布越散
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;
    
    return a2 / denom;
}

// Smith-Schlick 几何函数 (与粗糙度相关的遮挡/阴影)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith 方法: 结合观察方向和光线方向的几何函数
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    
    return ggx1 * ggx2;
}

// Fresnel-Schlick 近似
// 描述不同观察角度下的反射率
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ============================================================================
// 主函数
// ============================================================================

void main() {
    // 归一化法线和观察方向
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(mat.cameraPos - fragPosition);
    
    // 金属表面的基础反射率 (F0)
    // 电介质 (非金属): ~0.04
    // 金属: 由 albedo 决定
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, mat.albedo, mat.metallic);
    
    // 反射方程: 遍历所有光源
    vec3 Lo = vec3(0.0);  // 出射辐射度
    
    for (int i = 0; i < 4; i++) {
        // 光照计算
        vec3 L = normalize(mat.lights[i].position - fragPosition);
        vec3 H = normalize(V + L);  // 半程向量
        
        // 距离衰减
        float distance = length(mat.lights[i].position - fragPosition);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = mat.lights[i].color * mat.lights[i].intensity * attenuation;
        
        // 余弦项 (Lambert)
        float NdotL = max(dot(N, L), 0.0);
        
        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, mat.roughness);
        float G = GeometrySmith(N, V, L, mat.roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
        // 镜面反射分量
        vec3 numerator = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = numerator / denominator;
        
        // 漫反射分量 (能量守恒)
        // kS = 反射比例, kD = 折射(漫反射)比例
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - mat.metallic;  // 金属没有漫反射
        
        vec3 diffuse = kD * mat.albedo / PI;
        
        // 累积辐射度
        Lo += (diffuse + specular) * radiance * NdotL;
    }
    
    // 环境光
    vec3 F = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kS = F;
    vec3 kD = 1.0 - kS;
    kD *= 1.0 - mat.metallic;
    
    vec3 ambient = (kD * mat.albedo) * mat.ambientLight * mat.ao;
    vec3 color = ambient + Lo;
    
    // HDR 色调映射 (Reinhard)
    color = color / (color + vec3(1.0));
    
    // Gamma 校正 (线性 -> sRGB)
    color = pow(color, vec3(1.0 / 2.2));
    
    // ==========================================================================
    // 玻璃效果: 当 opacity < 1.0 时启用折射/反射计算
    // ==========================================================================
    if (mat.opacity < 1.0) {
        // Fresnel 反射 (电介质, 基于 IOR)
        float f0 = pow((1.0 - mat.ior) / (1.0 + mat.ior), 2.0);
        float fresnel = f0 + (1.0 - f0) * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        
        // 折射方向 (斯涅尔定律)
        vec3 refracted = refract(-V, N, 1.0 / mat.ior);
        
        // 简单环境近似: 基于折射方向的渐变色
        vec3 envColor = mix(vec3(0.1, 0.1, 0.15), vec3(0.4, 0.5, 0.6), 
                            clamp(0.5 + 0.5 * refracted.y, 0.0, 1.0));
        
        // 玻璃颜色 = 环境折射色 + 高光
        color = mix(color, envColor, fresnel * 0.7);
        
        // 边缘高光 (掠射角更亮)
        float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0);
        color += vec3(0.3, 0.35, 0.4) * rim;
    }
    
    outColor = vec4(color, mat.opacity);
}
