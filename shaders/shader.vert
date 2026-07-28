#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 fragPosition;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec4 fragPosLightSpace;
layout(location = 4) flat out float fragEmissiveTarget;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrix;
    vec3 cameraPos;
} ubo;

layout(push_constant) uniform PushConstants {
    float emissiveTarget;
} pc;

void main() {
    vec4 worldPos = ubo.model * vec4(inPosition, 1.0);
    fragPosition = worldPos.xyz;
    fragNormal = mat3(transpose(inverse(ubo.model))) * inNormal;
    fragUV = inUV;
    fragPosLightSpace = ubo.lightSpaceMatrix * worldPos;
    fragEmissiveTarget = pc.emissiveTarget;
    gl_Position = ubo.proj * ubo.view * worldPos;
}
