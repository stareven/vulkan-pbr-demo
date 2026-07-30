#pragma once

// ============================================================================
// 材质系统 - 材质预设和状态管理
// ============================================================================
// MaterialSystem 管理球体的材质状态:
//   - 材质预设 (preset): 7 种预定义材质 (金属/塑料/玻璃等)
//   - 玻璃模式 (glass): 切换透明/不透明
//   - 自发光模式 (emissive): 切换自发光开关
//
// 这些状态会被传递给 MeshManager, 更新到 uniform buffer,
// 最终在 shader 中用于 PBR 光照计算
//
// 按键控制:
//   - M 键: 切换材质预设 (0-6 循环)
//   - G 键: 切换玻璃模式 (ON/OFF)
//   - F 键: 切换自发光模式 (ON/OFF)
//
// 注意: 地面材质是固定的, 不受这些按键影响
// ============================================================================
class MaterialSystem {
private:
    // 当前材质预设索引 (0-6, 共 7 种)
    int currentPreset = 0;

    // 玻璃模式开关 (true = 透明, false = 不透明)
    bool glassEnabled = false;

    // 自发光模式开关 (true = 开启自发光, false = 关闭)
    bool emissiveEnabled = false;

public:
    // 默认构造: 初始状态为 preset=0, glass=OFF, emissive=OFF
    MaterialSystem() = default;

    // 切换到下一个材质预设 (0->1->2->...->6->0 循环)
    void nextPreset();

    // 切换玻璃模式 (ON<->OFF)
    void toggleGlass();

    // 切换自发光模式 (ON<->OFF)
    void toggleEmissive();

    // ---------- Getters ----------

    // 当前材质预设索引
    int getPreset() const { return currentPreset; }

    // 玻璃模式是否开启
    bool isGlassEnabled() const { return glassEnabled; }

    // 自发光模式是否开启
    bool isEmissiveEnabled() const { return emissiveEnabled; }
};
