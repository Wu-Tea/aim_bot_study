#pragma once

#include "runtime_config.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace controller_native {

struct RecoilWeaponIdentityRecord {
    std::string canonical_weapon_id;
    std::string game;
    std::string display_name;
    std::vector<std::string> alias_names;
    std::vector<std::string> blueprint_names;
    std::vector<std::string> signature_refs;
};

struct RecoilSignatureMatch {
    std::string signature_id;
    std::string canonical_weapon_id;
    float score = 0.0f;
};

struct RecoilWeaponRecognitionEvent {
    std::string game;
    std::string canonical_weapon_id;
    float confidence = 0.0f;
    std::string source;
    std::string timestamp;
    bool degraded = false;
    std::string matched_name;
    std::vector<std::string> profile_ids;
};

class NativeRecoilWeaponRecognizer {
public:
    explicit NativeRecoilWeaponRecognizer(const GamepadRecoilConfig& config);

    std::optional<RecoilWeaponRecognitionEvent> process_text_candidates(
        const std::vector<std::string>& text_candidates,
        const std::string& timestamp,
        bool switch_suspected = false);

    std::optional<RecoilWeaponRecognitionEvent> process_signals(
        const std::vector<std::string>& text_candidates,
        const std::vector<RecoilSignatureMatch>& ranked_image_matches,
        const std::string& timestamp,
        bool switch_suspected = false);

    bool write_latest_state(const RecoilWeaponRecognitionEvent& event) const;

    const std::vector<RecoilWeaponIdentityRecord>& identity_records() const;
    std::vector<std::string> profile_ids_for(const std::string& canonical_weapon_id) const;

private:
    struct RuntimeState {
        std::string confirmed_weapon_id;
        bool switch_suspected = false;
        int text_window_remaining = 0;
    };

    GamepadRecoilConfig config_;
    std::vector<RecoilWeaponIdentityRecord> identity_records_;
    RuntimeState runtime_state_;
};

class NativeRecoilHudOcr {
public:
    explicit NativeRecoilHudOcr(const GamepadRecoilConfig& config);

    bool available() const;
    std::vector<std::string> read_text_candidates();

private:
    GamepadRecoilConfig config_;
    bool available_ = false;
};

class NativeRecoilWeaponRuntimeRecognizer {
public:
    explicit NativeRecoilWeaponRuntimeRecognizer(const GamepadRecoilConfig& config);

    bool available() const;
    bool poll_once();
    const std::optional<RecoilWeaponRecognitionEvent>& last_event() const;

private:
    NativeRecoilWeaponRecognizer recognizer_;
    NativeRecoilHudOcr ocr_;
    bool available_ = false;
    std::string last_dedup_key_;
    std::optional<RecoilWeaponRecognitionEvent> last_event_;
};

class RecoilWeaponSwitchCaptureScheduler {
public:
    bool update_y_button(
        bool y_down,
        std::chrono::steady_clock::time_point now);
    bool consume_due_capture(std::chrono::steady_clock::time_point now);
    void clear_pending();
    std::size_t pending_count() const;

private:
    bool last_y_down_ = false;
    std::vector<std::chrono::steady_clock::time_point> pending_captures_;
};

std::string recoil_weapon_utc_timestamp();

}  // namespace controller_native
