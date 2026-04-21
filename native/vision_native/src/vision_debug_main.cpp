#include "vision_native/vision_engine.h"

#include <exception>
#include <iostream>
#include <string>

namespace {

int parse_int_arg(const char* value, const char* name) {
    try {
        return std::stoi(value);
    } catch (...) {
        throw std::runtime_error(std::string("invalid value for ") + name + ": " + value);
    }
}

} // namespace

int main(int argc, char** argv) {
    int width = 640;
    int height = 512;
    int frames = 8;
    int timeout_ms = 10;
    bool aim = true;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--width" && (i + 1) < argc) {
            width = parse_int_arg(argv[++i], "--width");
        } else if (arg == "--height" && (i + 1) < argc) {
            height = parse_int_arg(argv[++i], "--height");
        } else if (arg == "--frames" && (i + 1) < argc) {
            frames = parse_int_arg(argv[++i], "--frames");
        } else if (arg == "--timeout-ms" && (i + 1) < argc) {
            timeout_ms = parse_int_arg(argv[++i], "--timeout-ms");
        } else if (arg == "--aim") {
            aim = true;
        } else if (arg == "--no-aim") {
            aim = false;
        } else {
            std::cerr << "usage: vision_native_debug.exe [--width N] [--height N] [--frames N] [--timeout-ms N] [--aim|--no-aim]\n";
            return 2;
        }
    }

    try {
        vision_native::VisionEngine engine(width, height, 0, -1, timeout_ms);
        engine.set_aiming(aim);

        std::cout << "vision_native_debug start"
                  << " width=" << width
                  << " height=" << height
                  << " frames=" << frames
                  << " aim=" << (aim ? 1 : 0) << "\n";

        for (int i = 0; i < frames; ++i) {
            const vision_native::VisionResult result = engine.poll_once();
            std::cout << "[VisionResult]"
                      << " frame_id=" << result.frame_id
                      << " has_target=" << (result.has_target ? 1 : 0)
                      << " auto_fire=" << (result.auto_fire ? 1 : 0)
                      << " dx=" << result.dx
                      << " dy=" << result.dy
                      << " wait_ms=" << result.wait_ms
                      << " infer_ms=" << result.infer_ms
                      << " post_ms=" << result.post_ms
                      << " age_ms=" << result.age_ms
                      << " boxes_seen=" << result.boxes_seen
                      << " target_source=" << result.target_source
                      << "\n";
        }
        return 0;
    } catch (const std::exception& exc) {
        std::cerr << "vision_native_debug failed: " << exc.what() << "\n";
        return 1;
    }
}
