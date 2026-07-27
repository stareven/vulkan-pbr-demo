#pragma once

#include "math_utils.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// 相机 - 独立的视角状态管理(纯逻辑,无 Vulkan 依赖)
// ============================================================================
class Camera {
public:
    Camera() = default;

    // 鼠标拖拽:更新 yaw/pitch
    void handleMouseDrag(double dx, double dy) {
        yaw   += static_cast<float>(dx) * 0.005f;
        pitch -= static_cast<float>(dy) * 0.005f;
        pitch = std::clamp(pitch, -1.5f, 1.5f);
    }

    // 基于当前朝向的位移
    void moveForward(float distance) { position = position + forward() * distance; }
    void moveRight(float distance)   { position = position + right() * distance; }
    void moveUp(float distance)      { position.y += distance; }

    // 观察矩阵
    Mat4 getViewMatrix() const {
        return Mat4::lookAt(position, position + forward(), {0, 1, 0});
    }

    // 访问器
    const Vec3& getPosition() const { return position; }
    float getYaw()   const { return yaw; }
    float getPitch() const { return pitch; }

    // 便于直接修改位置(WASD 之外的特殊用例)
    void setPosition(const Vec3& p) { position = p; }

private:
    Vec3 forward() const {
        return {std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::cos(yaw)};
    }
    Vec3 right() const {
        return forward().cross({0, 1, 0}).normalize();
    }

    Vec3 position{0, 2, 5};
    float yaw   = 3.14159265f;   // π
    float pitch = -0.5f;
};
