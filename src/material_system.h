#pragma once

// ============================================================================
// 材质系统 - 管理材质预设和状态
// ============================================================================
class MaterialSystem {
private:
    int currentPreset = 0;
    bool glassEnabled = false;
    bool emissiveEnabled = false;

public:
    MaterialSystem() = default;

    void nextPreset();
    void toggleGlass();
    void toggleEmissive();

    int getPreset() const { return currentPreset; }
    bool isGlassEnabled() const { return glassEnabled; }
    bool isEmissiveEnabled() const { return emissiveEnabled; }
};
