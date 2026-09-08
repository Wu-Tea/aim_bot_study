#include "virtual_mouse_packets.h"
#include <climits>
#include <iostream>
#include <stdexcept>
using namespace virtual_mouse;
void require(bool ok) { if (!ok) throw std::runtime_error("packet contract failed"); }
int main() {
    try {
        for (auto mode : {Mode::Pass, Mode::Block, Mode::Invert}) {
            for (int x : {INT_MIN, -100000, -1, 0, 1, 100000, INT_MAX}) {
                Pending p; require(translate({0x401, 8, -240, x, 71421}, mode, 0, p));
                std::int64_t sx = 0, sy = 0; int sw = 0;
                while (!p.empty()) {
                    auto r = p.next(32257); require(r.buttons == 1);
                    require(r.x >= -32257 && r.x <= 32257 && r.y >= -32257 && r.y <= 32257);
                    sx += r.x; sy += r.y; sw += r.wheel;
                }
                const int sign = mode == Mode::Pass ? 1 : mode == Mode::Block ? 0 : -1;
                require(sx == static_cast<std::int64_t>(x) * sign && sy == 71421LL * sign && sw == -2);
            }
        }
        std::uint8_t held = 0;
        for (unsigned i = 0; i < 5; ++i) {
            Pending p; require(translate({1u << (i * 2)}, Mode::Pass, held, p));
            held = p.buttons; require(held == (1u << (i + 1)) - 1);
        }
        for (unsigned i = 0; i < 5; ++i) {
            Pending p; require(translate({2u << (i * 2)}, Mode::Pass, held, p));
            held = p.buttons;
        }
        require(held == 0);
        Pending p; p.x = 999;
        require(!translate({3, 0, 0, 1, 2}, Mode::Pass, 0, p) && p.x == 999);
        require(!translate({0x400, 0, 30}, Mode::Pass, 0, p));
        require(!translate({0, 1, 0, 7, 9}, Mode::Pass, 0, p));
        require(!translate({0x1000}, Mode::Pass, 0, p));
        require(translate({0xc00, 0, 32760}, Mode::Pass, 0, p));
        int w = 0, h = 0, n = 0;
        while (!p.empty()) { auto r = p.next(32257); w += r.wheel; h += r.hwheel; ++n; }
        require(w == 273 && h == 273 && n == 3);
        const auto wire = encode({0x15, -1, 258, -2, 3});
        const std::array<std::uint8_t, 10> expected{0x40, 8, 3, 0x15, 0xff, 0xff, 2, 1, 0xfe, 3};
        require(std::equal(expected.begin(), expected.end(), wire.begin()));
        require(std::all_of(wire.begin() + 10, wire.end(), [](auto b) { return b == 0; }));
        std::cout << "PASS: count conservation, overflow, five buttons, wheels, unsupported sources, HID wire layout\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
