#pragma once

#include "math_utils.h"
#include <cmath>
#include <algorithm>

// ============================================================================
// 相机 - 3D 视角控制
// ============================================================================
// Camera 类管理 3D 相机的状态:
//   - 位置 (position): 相机在世界空间中的坐标
//   - 朝向 (yaw/pitch): 水平/垂直旋转角度, 用于计算前方向向量
//   - 视图矩阵 (view matrix): 把世界坐标变换到相机空间
//
// 控制方式:
//   - 鼠标拖拽: 改变 yaw/pitch, 旋转视角
//   - WASD 键: 前后左右移动 (基于当前朝向)
//   - QE 键: 上下移动
//
// 数学原理:
//   - yaw: 水平旋转角 (弧度), 0=正前方, π/2=右方, -π/2=左方
//   - pitch: 垂直旋转角 (弧度), 0=水平, π/2=正上方, -π/2=正下方
//   - forward: 前方向向量, 由 yaw/pitch 计算:
//       x = cos(pitch) * sin(yaw)
//       y = sin(pitch)
//       z = cos(pitch) * cos(yaw)
//   - right: 右方向向量 = normalize(cross(worldUp, forward))
//     (与 GLM lookAt 一致: worldUp × forward)
//
// GLM 迁移说明:
//   - 用 glm::lookAt 替代自定义 Mat4::lookAt
//   - 用 glm::vec3 替代自定义 Vec3
//   - 用 glm::mat4 替代自定义 Mat4
//   - glm::lookAt 返回列主序矩阵, 可直接传给 GPU, 无需转置
// ============================================================================
class Camera {
public:
    // 默认构造: 初始位置 (0, 2, 5), 朝向 (yaw=π, pitch=-0.5)
    Camera() = default;

    // 鼠标拖拽: 更新 yaw/pitch
    //   - dx: 水平位移 (像素), 乘以灵敏度 0.005 得到弧度增量
    //   - dy: 垂直位移 (像素), 乘以灵敏度 0.005 得到弧度增量 (注意: 向上拖是负 dy)
    //   - pitch 限制在 [-1.5, 1.5] 弧度 (约 ±86°), 避免万向锁
    void handleMouseDrag(double dx, double dy) {
        yaw   += static_cast<float>(dx) * 0.005f;
        pitch -= static_cast<float>(dy) * 0.005f;
        pitch = std::clamp(pitch, -1.5f, 1.5f);
    }

    // 基于当前朝向的位移:
    //   - moveForward: 沿 forward 方向前进/后退
    //   - moveRight: 沿 right 方向左右移动
    //   - moveUp: 沿世界 Y 轴上下移动 (不依赖相机朝向)
    void moveForward(float distance) { position = position + forward() * distance; }
    void moveRight(float distance)   { position = position + right() * distance; }
    void moveUp(float distance)      { position.y += distance; }

    // 获取视图矩阵:
    //   - 调用 glm::lookAt, 传入相机位置/目标点/上方向
    //   - 目标点 = position + forward (相机前方 1 单位处)
    //   - 上方向 = (0, 1, 0) (世界 Y 轴)
    //   - 返回: 把世界坐标变换到相机空间的矩阵 (列主序, 可直接传给 GPU)
    glm::mat4 getViewMatrix() const {
        return glm::lookAt(position, position + forward(), glm::vec3(0, 1, 0));
    }

    // ---------- Getters ----------

    // 相机位置 (世界坐标)
    const glm::vec3& getPosition() const { return position; }

    // 水平旋转角 (弧度)
    float getYaw()   const { return yaw; }

    // 垂直旋转角 (弧度)
    float getPitch() const { return pitch; }

    // 直接设置位置 (WASD 之外的特殊用例, 比如重置相机)
    void setPosition(const glm::vec3& p) { position = p; }

private:
    // 计算前方向向量 (单位向量):
    //   - 由 yaw (水平角) 和 pitch (垂直角) 计算
    //   - 公式: (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    //   - 这是球坐标到直角坐标的转换
    glm::vec3 forward() const {
        return {std::cos(pitch) * std::sin(yaw),
                std::sin(pitch),
                std::cos(pitch) * std::cos(yaw)};
    }

    // 计算右方向向量 (单位向量):
    //   - right = normalize(cross(worldUp, forward))
    //   - 与 glm::lookAt 使用相同的叉积顺序
    glm::vec3 right() const {
        return glm::normalize(glm::cross(glm::vec3(0, 1, 0), forward()));
    }

    // 相机位置 (世界坐标), 默认 (0, 2, 5)
    glm::vec3 position{0, 2, 5};

    // 水平旋转角 (弧度), 默认 π (面向 -Z 方向)
    float yaw   = 3.14159265f;   // π

    // 垂直旋转角 (弧度), 默认 -0.5 (略微俯视)
    float pitch = -0.5f;
};
