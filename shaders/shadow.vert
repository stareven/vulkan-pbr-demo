#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform LightSpaceUBO {
    mat4 lightSpaceMatrix;
} ubo;

void main() {
    // C++ 端已转置，GLSL 直接读取
    gl_Position = ubo.lightSpaceMatrix * vec4(inPosition, 1.0);
}
