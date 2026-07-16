// ============================================================================
// PBR Vertex Shader - 顶点着色器
// ============================================================================
// 传递 PBR 所需的顶点数据到片段着色器
// ============================================================================

#version 450

// 顶点输入 (实际项目中从 Vertex Buffer 读取)
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

// Uniform: MVP 矩阵
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec3 cameraPos;
} ubo;

// 输出到片段着色器
layout(location = 0) out vec3 fragPosition;   // 世界空间位置
layout(location = 1) out vec3 fragNormal;     // 世界空间法线
layout(location = 2) out vec2 fragUV;         // UV 坐标

void main() {
    // 世界空间位置
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragPosition = worldPos.xyz;
    
    // 法线变换到世界空间 (使用法线矩阵 = transpose(inverse(model)) 的上 3x3)
    // 简化版: 假设模型矩阵没有非均匀缩放
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    
    fragUV = inUV;
    
    // MVP 变换
    gl_Position = ubo.proj * ubo.view * worldPos;
}
