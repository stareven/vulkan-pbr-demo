#include "material_system.h"
#include <iostream>

void MaterialSystem::nextPreset() {
    currentPreset = (currentPreset + 1) % 7;
    std::cout << "Material preset: " << currentPreset << "\n";
}

void MaterialSystem::toggleGlass() {
    glassEnabled = !glassEnabled;
    std::cout << "Glass: " << (glassEnabled ? "ON" : "OFF") << "\n";
}

void MaterialSystem::toggleEmissive() {
    emissiveEnabled = !emissiveEnabled;
    std::cout << "Emissive: " << (emissiveEnabled ? "ON" : "OFF") << "\n";
}
