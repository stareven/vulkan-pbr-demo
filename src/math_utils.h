#pragma once

#include <cmath>
#include <algorithm>

// ============================================================================
// 数学工具库 - 自定义的向量/矩阵实现
// ============================================================================
// 这个文件提供轻量级的 2D/3D 数学库, 专为 Vulkan 渲染定制:
//   - Vec2: 二维向量 (纹理坐标等)
//   - Vec3: 三维向量 (位置/方向/颜色)
//   - Mat4: 4x4 矩阵 (变换矩阵)
//
// Vulkan 特殊性:
//   - 投影矩阵的 Y 轴翻转 (Vulkan NDC 的 Y 向下, OpenGL 的 Y 向上)
//   - 深度范围是 [0,1] (OpenGL 是 [-1,1])
//   - 列主序存储 (column-major), 与 GLSL 的 mat4 一致
//
// 这些实现是教学用途, 够用但不完整, 生产环境应使用 glm 之类的成熟库
// ============================================================================

// ----------------------------------------------------------------------------
// 二维向量: 用于纹理坐标 (UV)
// ----------------------------------------------------------------------------
struct Vec2 {
    float x, y;
};

// ----------------------------------------------------------------------------
// 三维向量: 用于位置/方向/法向量/颜色
// ----------------------------------------------------------------------------
struct Vec3 {
    float x, y, z;

    // 构造函数: 默认 (0,0,0)
    Vec3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}

    // 向量加法: (x1+x2, y1+y2, z1+z2)
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }

    // 向量减法: (x1-x2, y1-y2, z1-z2)
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }

    // 标量乘法: (x*s, y*s, z*s)
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    // 点积: x1*x2 + y1*y2 + z1*z2
    //   - 几何意义: |a|*|b|*cos(θ), 用于计算夹角/投影
    //   - 光照计算中用于 Lambert 漫反射: N·L (法向量·光源方向)
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }

    // 叉积: 返回垂直于两个输入向量的新向量
    //   - 几何意义: |a×b| = |a|*|b|*sin(θ), 方向由右手定则确定
    //   - 用途: 构造正交基 (如 lookAt 中的 right/up 向量)
    //   - 公式: (y1*z2 - z1*y2, z1*x2 - x1*z2, x1*y2 - y1*x2)
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }

    // 向量长度 (模): √(x² + y² + z²)
    float length() const { return std::sqrt(x * x + y * y + z * z); }

    // 单位化: 返回方向相同但长度为 1 的向量
    //   - 法向量必须是单位向量, 否则光照计算会出错
    //   - 若长度接近 0 则返回零向量, 避免除零
    Vec3 normalize() const {
        float l = length();
        return l > 0 ? Vec3{x / l, y / l, z / l} : Vec3{};
    }
};

// ----------------------------------------------------------------------------
// 4x4 矩阵: 用于 3D 变换 (平移/旋转/缩放/投影)
// ----------------------------------------------------------------------------
// Vulkan 中矩阵的关键概念:
//   - 列主序 (column-major): m[列][行], 与 GLSL 的 mat4 布局一致
//   - 变换顺序: v' = P * V * M * v (投影 * 视图 * 模型 * 顶点)
//   - MVP 矩阵: Model-View-Projection, 把模型空间顶点变换到屏幕空间
struct Mat4 {
    float m[4][4];  // m[列][行], 列主序存储

    // 单位矩阵: 对角线为 1, 其余为 0
    //   - 几何意义: 不做任何变换
    //   - 用途: 初始化/重置变换
    static Mat4 identity() {
        Mat4 r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }

    // 透视投影矩阵: 把视锥体内的 3D 点变换到 NDC (标准化设备坐标)
    //   - fov: 垂直视角 (弧度), 越大视野越广
    //   - aspect: 宽高比 (width/height)
    //   - near/far: 近/远裁剪面距离
    // Vulkan 特殊性:
    //   - Y 轴翻转: r.m[1][1] = -f, 因为 Vulkan NDC 的 Y 向下 (OpenGL 的 Y 向上)
    //   - 深度范围 [0,1]: r.m[2][2] 和 r.m[2][3] 的公式与 OpenGL 不同
    //     (OpenGL 是 [-1,1], 公式是 (far+near)/(near-far))
    static Mat4 perspective(float fov, float aspect, float near, float far) {
        Mat4 r{};
        float f = 1.0f / std::tan(fov * 0.5f);  // fov 的余切
        r.m[0][0] = f / aspect;  // X 缩放
        r.m[1][1] = -f;          // Y 缩放 (负号翻转 Y 轴)
        r.m[2][2] = far / (near - far);  // Z 变换 (Vulkan [0,1] 深度)
        r.m[2][3] = (far * near) / (near - far);  // Z 平移
        r.m[3][2] = -1.0f;  // 透视除法因子
        return r;
    }

    // LookAt 视图矩阵: 把世界坐标变换到相机空间
    //   - eye: 相机位置
    //   - center: 观察目标位置
    //   - up: 上方向向量 (通常是 (0,1,0))
    // 构造过程:
    //   1. f = forward 方向 (eye -> center, 单位化)
    //   2. r = right 方向 (f × up, 单位化)
    //   3. u = up 方向 (r × f, 已经是单位向量)
    //   4. 构造旋转矩阵 + 平移 (-eye)
    // 结果: 把世界空间的点变换到以相机为原点的坐标空间
    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalize();  // 前方向
        Vec3 r = f.cross(up).normalize();     // 右方向
        Vec3 u = r.cross(f);                  // 上方向 (已单位化)
        Mat4 res = identity();
        // 旋转部分: [r.x, u.x, -f.x]
        //           [r.y, u.y, -f.y]
        //           [r.z, u.z, -f.z]
        res.m[0][0] = r.x;  res.m[0][1] = r.y;  res.m[0][2] = r.z;  res.m[0][3] = -r.dot(eye);
        res.m[1][0] = u.x;  res.m[1][1] = u.y;  res.m[1][2] = u.z;  res.m[1][3] = -u.dot(eye);
        res.m[2][0] = -f.x; res.m[2][1] = -f.y; res.m[2][2] = -f.z; res.m[2][3] = f.dot(eye);
        return res;
    }

    // 正交投影矩阵: 把长方体区域变换到 NDC
    //   - left/right/bottom/top: 视口的边界
    //   - near/far: 近/远裁剪面
    // 与透视投影的区别:
    //   - 正交投影没有近大远小效果, 平行线保持平行
    //   - 用途: 阴影贴图渲染 (光源视角)、2D UI、调试可视化
    static Mat4 ortho(float left, float right, float bottom, float top, float near, float far) {
        Mat4 r{};
        r.m[0][0] = 2.0f / (right - left);      // X 缩放
        r.m[1][1] = 2.0f / (top - bottom);      // Y 缩放
        r.m[2][2] = 1.0f / (near - far);        // Z 缩放
        r.m[3][0] = -(right + left) / (right - left);  // X 平移
        r.m[3][1] = -(top + bottom) / (top - bottom);  // Y 平移
        r.m[3][2] = near / (near - far);        // Z 平移
        r.m[3][3] = 1.0f;
        return r;
    }

    // 转置矩阵: 行列互换
    //   - 用途: 法线变换 (法线矩阵 = 模型矩阵的逆转置)
    //   - 公式: (A^T)[i][j] = A[j][i]
    Mat4 transposed() const {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                r.m[i][j] = m[j][i];
        return r;
    }

    // 矩阵乘法: C = A * B
    //   - 几何意义: 连续应用两个变换 (先 B 后 A)
    //   - 公式: C[i][j] = Σ A[i][k] * B[k][j]
    //   - 注意: 矩阵乘法不满足交换律 (A*B ≠ B*A)
    Mat4 operator*(const Mat4& o) const {
        Mat4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += m[i][k] * o.m[k][j];
        return r;
    }
};
