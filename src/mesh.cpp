#include "mesh.h"
#include <cmath>

std::vector<Vertex> generateSphere(int rings, int sectors) {
    std::vector<Vertex> verts;
    const float R = 1.0f;
    for (int r = 0; r <= rings; ++r) {
        float phi = M_PI * float(r) / rings;
        for (int s = 0; s <= sectors; ++s) {
            float theta = 2.0f * M_PI * float(s) / sectors;
            float x = std::sin(phi) * std::cos(theta);
            float y = std::cos(phi);
            float z = std::sin(phi) * std::sin(theta);
            Vec3 p{x * R, y * R, z * R};
            Vec3 n = p.normalize();
            Vec2 uv{float(s) / sectors, float(r) / rings};
            verts.push_back({p, n, uv});
        }
    }
    return verts;
}

std::vector<uint32_t> generateSphereIndices(int rings, int sectors) {
    std::vector<uint32_t> idx;
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            uint32_t a = r * (sectors + 1) + s;
            uint32_t b = a + sectors + 1;
            idx.push_back(a);     idx.push_back(b);     idx.push_back(a + 1);
            idx.push_back(a + 1); idx.push_back(b);     idx.push_back(b + 1);
        }
    }
    return idx;
}

std::vector<Vertex> generatePlane(float size, float y) {
    float h = size * 0.5f;
    return {
        {{-h, y, -h}, {0, 1, 0}, {0, 0}},  // 0
        {{ h, y, -h}, {0, 1, 0}, {1, 0}},  // 1
        {{ h, y,  h}, {0, 1, 0}, {1, 1}},  // 2
        {{-h, y, -h}, {0, 1, 0}, {0, 0}},  // 3
        {{ h, y,  h}, {0, 1, 0}, {1, 1}},  // 4
        {{-h, y,  h}, {0, 1, 0}, {0, 1}},  // 5
    };
}

std::vector<uint32_t> generatePlaneIndices() {
    return {0, 1, 2, 3, 4, 5};
}
