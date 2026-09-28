#pragma once
#include <cstdint>
#include <optional>

namespace vision_native {
enum class SelectionPhase : unsigned char { Empty, Observed, Missing, CueOnly };
enum class ConfirmationPhase : unsigned char { Idle, Confirming };

// Owns identity, generation, evidence availability and pending confirmation.
// Scoring/association stay in the selector, which supplies their results.
template<class Target> class SelectionStateMachine {
public:
    void reset() { clear(); generation_ = 0; changed_ = false; }
    void begin_frame() noexcept { changed_ = false; }
    void clear() {
        phase_ = SelectionPhase::Empty; active_.reset(); misses_ = 0; cancel_confirmation();
    }
    bool has_identity() const noexcept { return phase_ != SelectionPhase::Empty; }
    const std::optional<Target>& active() const noexcept { return active_; }
    const std::optional<Target>& pending() const noexcept { return pending_; }
    std::uint64_t generation() const noexcept { return generation_; }
    bool changed() const noexcept { return changed_; }
    SelectionPhase phase() const noexcept { return phase_; }
    ConfirmationPhase confirmation_phase() const noexcept { return confirmation_; }
    void cancel_confirmation() {
        confirmation_ = ConfirmationPhase::Idle; pending_.reset(); confirmations_ = 0;
    }
    std::optional<Target> confirm(const Target& target, bool same_pending, bool immediate, int required) {
        if (immediate) { cancel_confirmation(); return target; }
        if (required <= 1) return target;
        if (confirmation_ == ConfirmationPhase::Idle || !same_pending) {
            confirmation_ = ConfirmationPhase::Confirming; pending_ = target; confirmations_ = 1;
            return std::nullopt;
        }
        pending_ = target;
        if (++confirmations_ < required) return std::nullopt;
        cancel_confirmation(); return target;
    }
    void commit(const Target& target, bool new_identity) {
        if (new_identity) { ++generation_; changed_ = true; }
        else changed_ = false;
        observe(target, false);
    }
    void observe(const Target& target, bool cue) {
        active_ = target; misses_ = 0;
        phase_ = cue ? SelectionPhase::CueOnly : SelectionPhase::Observed;
    }
    void mark_missing() noexcept { if (has_identity()) phase_ = SelectionPhase::Missing; }
    void matched() noexcept { misses_ = 0; }
    bool retain_miss(int limit) noexcept {
        mark_missing();
        if (!has_identity() || misses_ >= limit) return false;
        ++misses_; return true;
    }
    bool reject_transition(int limit) noexcept {
        mark_missing(); return !has_identity() || ++misses_ < limit;
    }
private:
    SelectionPhase phase_ = SelectionPhase::Empty;
    ConfirmationPhase confirmation_ = ConfirmationPhase::Idle;
    std::optional<Target> active_, pending_;
    std::uint64_t generation_ = 0;
    bool changed_ = false;
    int misses_ = 0, confirmations_ = 0;
};
}  // namespace vision_native
