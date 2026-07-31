#pragma once

// ============================================================================
// 数学工具库 - 基于 GLM (OpenGL Mathematics)
// ============================================================================
// 用 GLM 替换了之前的自定义 Vec2/Vec3/Mat4 实现。GLM 是业界标准的
// C++ 数学库, 与 GLSL 语法/布局一致, 列主序 (column-major) 存储。
//
// Vulkan 注意点:
//   - 投影矩阵的 Y 轴需要翻转 (Vulkan NDC 的 Y 向下, OpenGL 的 Y 向上)
//   - 深度范围是 [0,1] (OpenGL 是 [-1,1]), 通过 GLM_FORCE_DEPTH_ZERO_TO_ONE 启用
//   - GLM 的 mat4 默认列主序, 与 GLSL 的 mat4 布局一致, 可以直接 memcpy 到 UBO
//
// 用法:
//   - 类型: glm::vec2, glm::vec3, glm::vec4, glm::mat4
//   - 函数: glm::normalize(), glm::cross(), glm::dot(), glm::transpose()
//   - 变换: glm::perspective(), glm::lookAt(), glm::ortho()
//   - 取指针: glm::value_ptr(mat) 返回列主序的 float*, 可直接用于 memcpy 到 GPU
// ============================================================================

#define GLM_FORCE_DEPTH_ZERO_TO_ONE  // Vulkan 深度范围 [0,1] (非 OpenGL 的 [-1,1])

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>  // perspective, lookAt, ortho
#include <glm/gtc/type_ptr.hpp>          // value_ptr

// ============================================================================
// 兼容性别名 (可选, 方便后续代码平滑迁移)
// ============================================================================
// 如果需要全局替换, 可以启用以下别名。当前代码中已直接使用 glm:: 前缀。
// using Vec2 = glm::vec2;
// using Vec3 = glm::vec3;
// using Mat4 = glm::mat4;
