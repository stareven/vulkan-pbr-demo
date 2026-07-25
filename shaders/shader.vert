#version 450

// 最小化测试：直接输出三角形，不使用任何 UBO

void main() {
    // 硬编码三角形顶点
    vec2 positions[3] = vec2[](
        vec2(0.0, -0.5),
        vec2(0.5, 0.5),
        vec2(-0.5, 0.5)
    );
    
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
