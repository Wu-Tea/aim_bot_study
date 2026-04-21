#include "vision_native/tensorrt_inspector.h"

#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: vision_native_smoke.exe <engine_path>\n";
        return 2;
    }

    try {
        const auto info = vision_native::inspect_engine(argv[1]);
        std::cout << vision_native::engine_info_to_json(info);
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "vision_native_smoke failed: " << exc.what() << "\n";
        return 1;
    }
}
