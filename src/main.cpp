#include "pbr_app.h"

#include <iostream>

int main() {
    PBRApp app;
    try {
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
