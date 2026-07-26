#pragma once
#include "types.h"
#include <vector>

std::vector<Vertex> generateSphere(int rings = 32, int sectors = 64);
std::vector<uint32_t> generateSphereIndices(int rings, int sectors);

// 地面平面（用于接收阴影）
std::vector<Vertex> generatePlane(float size = 10.0f, float y = -1.5f);
std::vector<uint32_t> generatePlaneIndices();
