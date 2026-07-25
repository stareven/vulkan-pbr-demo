#pragma once

#include <cmath>
#include <algorithm>

struct Vec2 {
    float x, y;
};

struct Vec3 {
    float x, y, z;
    
    Vec3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
    
    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
    
    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3 normalize() const {
        float l = length();
        return l > 0 ? Vec3{x / l, y / l, z / l} : Vec3{};
    }
};

struct Mat4 {
    float m[4][4];
    
    static Mat4 identity() {
        Mat4 r{};
        r.m[0][0] = r.m[1][1] = r.m[2][2] = r.m[3][3] = 1.0f;
        return r;
    }
    
    static Mat4 perspective(float fov, float aspect, float near, float far) {
        Mat4 r{};
        float f = 1.0f / std::tan(fov * 0.5f);
        r.m[0][0] = f / aspect;
        r.m[1][1] = f;
        r.m[2][2] = far / (near - far);
        r.m[2][3] = -1.0f;
        r.m[3][2] = (far * near) / (near - far);
        return r;
    }
    
    static Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
        Vec3 f = (center - eye).normalize();
        Vec3 r = f.cross(up).normalize();
        Vec3 u = r.cross(f);
        Mat4 res = identity();
        res.m[0][0] = r.x;  res.m[0][1] = r.y;  res.m[0][2] = r.z;  res.m[0][3] = -r.dot(eye);
        res.m[1][0] = u.x;  res.m[1][1] = u.y;  res.m[1][2] = u.z;  res.m[1][3] = -u.dot(eye);
        res.m[2][0] = -f.x; res.m[2][1] = -f.y; res.m[2][2] = -f.z; res.m[2][3] = f.dot(eye);
        return res;
    }
    
    static Mat4 translation(Vec3 t) {
        Mat4 r = identity();
        r.m[0][3] = t.x;
        r.m[1][3] = t.y;
        r.m[2][3] = t.z;
        return r;
    }
    
    static Mat4 scale(float s) {
        Mat4 r = identity();
        r.m[0][0] = r.m[1][1] = r.m[2][2] = s;
        return r;
    }
};
