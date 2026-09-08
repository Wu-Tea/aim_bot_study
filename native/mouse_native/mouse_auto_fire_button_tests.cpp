#include "mouse_native/mouse_auto_fire_button.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
using Button = mouse_native::MouseAutoFireButton;
using Edge = mouse_native::MouseAutoFireEdge;

void test_pulses_are_edges_not_repeated_clicks() {
    Button button;
    require(button.next_edge(true) == Edge::Down, "pulse start needs one Down");
    require(button.accept_down(true, true), "authorized Down must pass");
    require(!button.physical_down(), "synthetic Down cannot become manual fire input");
    for (int i = 0; i < 100; ++i)
        require(button.next_edge(true) == Edge::None, "held pulse cannot repeat Down");
    require(button.next_edge(false) == Edge::Up && button.accept_up(), "pulse end needs one Up");
    require(button.next_edge(false) == Edge::None && !button.accept_up(), "idle cannot emit stray Up");
}

void test_manual_press_takes_ownership_without_an_up() {
    Button button;
    require(button.accept_down(true, true), "auto pulse must start");
    button.observe_physical(true); // This real Down is always forwarded by the hook.
    require(button.physical_down() && !button.synthetic_down(), "manual Down takes ownership");
    require(button.next_edge(false) == Edge::None && !button.accept_up(),
        "auto stop/emergency must not release the physical hold");
    require(!button.accept_down(true, true), "auto cannot duplicate a held physical Down");
    button.observe_physical(false); // Forward the real Up unchanged too.
    require(button.next_edge(false) == Edge::None, "manual release cannot trigger a synthetic click");
    require(button.accept_down(true, true), "a later authorized pulse may resume after manual release");
    require(button.accept_up(), "the resumed pulse must release normally");
}

void test_queued_edges_recheck_current_manual_and_lifecycle_state() {
    Button button;
    require(button.next_edge(true) == Edge::Down, "control tick requests a Down");
    button.observe_physical(true); // Physical event arrives before the queued injection.
    require(!button.accept_down(true, true), "queued Down must yield to a newer physical press");
    button.observe_physical(false);
    require(!button.accept_down(false, true), "disarm rejects queued Down");
    require(!button.accept_down(true, false), "revoked fire authority rejects queued Down");
    require(button.accept_down(true, true), "new pulse starts");
    require(button.next_edge(false) == Edge::Up, "control tick requests an Up");
    button.observe_physical(true);
    require(!button.accept_up(), "queued Up must not interrupt a newer physical hold");
}
}

int main() {
    try {
        test_pulses_are_edges_not_repeated_clicks();
        test_manual_press_takes_ownership_without_an_up();
        test_queued_edges_recheck_current_manual_and_lifecycle_state();
        std::cout << "[MouseAutoFireButtonTests] PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[MouseAutoFireButtonTests] FAIL: " << error.what() << '\n';
        return 1;
    }
}
