#include "mesh.h"

std::vector<Vertex> generateTriangle() {
    // 标准三角形，在 NDC 空间 [-0.5, 0.5]
    return {
        {{ 0.0f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.5f, 0.0f}},  // 底部中
        {{ 0.5f,  0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},  // 右上
        {{-0.5f,  0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}   // 左上
    };
}

std::vector<uint32_t> generateTriangleIndices() {
    return {0, 1, 2};
}
