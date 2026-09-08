#pragma once
#include <cstdint>

namespace mouse_link {
// Evidence, not authentication. Absence of an injection flag/tag is never proof
// of a physical device; a device handle is not proof of physical hardware either.
struct InputEvidence {
    const char* origin;
    bool tag_match;
    bool lower_integrity;
};
constexpr std::uintptr_t kMovementTag = 0x434F444D;
constexpr std::uintptr_t kFireTag = 0x434F4446;
constexpr std::uintptr_t kProbeTag = 0x4D50524F;
inline bool matches_tag(std::uintptr_t tag, std::uintptr_t extra_tag = 0) noexcept {
    return tag == kMovementTag || tag == kFireTag || tag == kProbeTag ||
        (extra_tag != 0 && tag == extra_tag);
}
inline InputEvidence hook_evidence(unsigned flags, std::uintptr_t tag,
                                   std::uintptr_t extra_tag = 0) noexcept {
    return {(flags & 3u) ? "os_injected" : "unconfirmed",
            matches_tag(tag, extra_tag), (flags & 2u) != 0};
}
inline InputEvidence raw_evidence(bool has_device, std::uintptr_t tag,
                                  std::uintptr_t extra_tag = 0) noexcept {
    return {has_device ? "device_associated" : "unconfirmed",
            matches_tag(tag, extra_tag), false};
}
} // namespace mouse_link
