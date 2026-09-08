#include "link_contract.h"
#include <iostream>
#include <stdexcept>
using namespace mouse_link;
void require(bool v, const char* message) { if (!v) throw std::runtime_error(message); }
void accumulate(Totals& t, const InterceptionMouseStroke& s) {
    ++t.packets; t.x += s.x; t.y += s.y;
    t.abs_x += s.x < 0 ? -static_cast<std::int64_t>(s.x) : s.x;
    t.abs_y += s.y < 0 ? -static_cast<std::int64_t>(s.y) : s.y;
}
int main() {
    try {
        for (Mode mode : {Mode::Relay, Mode::Block, Mode::Invert}) {
            Totals src{}, out{}, sink{};
            for (int i = 0; i != 1000; ++i) {
                InterceptionMouseStroke original{}, replacement{};
                original.x = (i % 2) ? -13 : 17; original.y = 4;
                original.state = (i % 2) ? INTERCEPTION_MOUSE_LEFT_BUTTON_UP : INTERCEPTION_MOUSE_LEFT_BUTTON_DOWN;
                original.rolling = -120;
                require(transform(original, mode, 0x12340001, replacement), "transform rejected relative packet");
                const int expected = mode == Mode::Relay ? original.x : mode == Mode::Invert ? -original.x : 0;
                require(replacement.x == expected, "single final output differs from required displacement");
                require(replacement.state == original.state && replacement.rolling == -120, "button/wheel corrupted");
                require(replacement.information == 0x12340001, "receiver tag missing");
                accumulate(src, original); accumulate(out, replacement); accumulate(sink, replacement);
            }
            require(evaluate(mode, src, out, sink, 0, true, true) == Evidence::Matched, "valid isolated stream rejected");
            require(evaluate(mode, src, out, sink, 1, true, true) == Evidence::Mismatch, "native leak accepted");
            require(evaluate(mode, src, out, sink, 0, false, true) != Evidence::Matched, "dead observer accepted");
            require(evaluate(mode, {}, out, sink, 0, true, true) != Evidence::Matched, "no physical input accepted");
            ++sink.abs_x;
            require(evaluate(mode, src, out, sink, 0, true, true) == Evidence::Mismatch, "duplicate/incomplete output accepted");
        }
        InterceptionMouseStroke s{}, out{};
        s.flags = INTERCEPTION_MOUSE_MOVE_ABSOLUTE;
        require(!transform(s, Mode::Invert, 1, out), "absolute input interpreted as relative");
        s.flags = 0; s.x = std::numeric_limits<int>::min();
        require(!transform(s, Mode::Invert, 1, out), "inversion overflow accepted");
        require(evaluate(Mode::Block, {10,0,0,40,40}, {}, {}, 0, true, false) != Evidence::Matched,
            "blind zero-output test accepted without a tagged output receipt");
        std::cout << "PASS: single-output packet path, metadata, overflow, exclusion oracle. Synthetic tests only.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
