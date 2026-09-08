#pragma once

#include <atomic>

namespace mouse_native {

enum class MouseAutoFireEdge { None, Down, Up };

// Physical events always pass unchanged. Only the hook updates these states;
// the control/escape threads may read them to request tagged synthetic edges.
class MouseAutoFireButton {
public:
    void observe_physical(bool down) noexcept {
        physical_.store(down);
        // A real press takes ownership of the already-down OS button. There
        // must be no synthetic Up between that physical Down and its Up.
        if (down) synthetic_.store(false);
    }
    bool physical_down() const noexcept { return physical_.load(); }
    bool synthetic_down() const noexcept { return synthetic_.load(); }
    MouseAutoFireEdge next_edge(bool requested) const noexcept {
        if (synthetic_down()) return requested && !physical_down()
            ? MouseAutoFireEdge::None : MouseAutoFireEdge::Up;
        return requested && !physical_down() ? MouseAutoFireEdge::Down : MouseAutoFireEdge::None;
    }
    bool accept_down(bool armed, bool requested) noexcept {
        if (!armed || !requested || physical_down()) return false;
        return !synthetic_.exchange(true);
    }
    bool accept_up() noexcept {
        const bool owned = synthetic_.exchange(false);
        return owned && !physical_down();
    }
private:
    std::atomic_bool physical_{false};
    std::atomic_bool synthetic_{false};
};

}  // namespace mouse_native
