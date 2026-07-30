#include "material_system.h"
#include <iostream>

// ----------------------------------------------------------------------------
// 切换到下一个材质预设
// ----------------------------------------------------------------------------
// currentPreset = (currentPreset + 1) % 7
// 循环切换: 0 -> 1 -> 2 -> ... -> 6 -> 0
// 每种预设定义了不同的 albedo/metallic/roughness, 详见 mesh_manager.cpp
// ----------------------------------------------------------------------------
void MaterialSystem::nextPreset() {
    currentPreset = (currentPreset + 1) % 7;
    std::cout << "Material preset: " << currentPreset << "\n";
}

// ----------------------------------------------------------------------------
// 切换玻璃模式
// ----------------------------------------------------------------------------
// glassEnabled = !glassEnabled
//   - ON:  球体变为透明玻璃 (opacity=0.2, ior=1.52, roughness=0.02)
//   - OFF: 球体恢复不透明 (opacity=1.0)
// 玻璃模式下会使用半透明管线 (depthWrite=FALSE), 避免深度缓冲问题
// ----------------------------------------------------------------------------
void MaterialSystem::toggleGlass() {
    glassEnabled = !glassEnabled;
    std::cout << "Glass: " << (glassEnabled ? "ON" : "OFF") << "\n";
}

// ----------------------------------------------------------------------------
// 切换自发光模式
// ----------------------------------------------------------------------------
// emissiveEnabled = !emissiveEnabled
//   - ON:  球体自发光 (emissive={2.0, 0.3, 0.05}, strength=5.0)
//   - OFF: 球体不自发光 (emissive={0, 0, 0}, strength=0)
// 自发光让球体看起来像发光体 (灯泡/火焰), 不受光照影响
// ----------------------------------------------------------------------------
void MaterialSystem::toggleEmissive() {
    emissiveEnabled = !emissiveEnabled;
    std::cout << "Emissive: " << (emissiveEnabled ? "ON" : "OFF") << "\n";
}
