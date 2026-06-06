#include "recoil_profile.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace controller_native {

namespace {

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open recoil profile: " + path.u8string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::size_t find_value_start(const std::string& text, const std::string& key) {
    const std::string quoted_key = "\"" + key + "\"";
    const std::size_t key_pos = text.find(quoted_key);
    if (key_pos == std::string::npos) {
        return std::string::npos;
    }
    const std::size_t colon = text.find(':', key_pos + quoted_key.size());
    if (colon == std::string::npos) {
        return std::string::npos;
    }
    return colon + 1;
}

std::string parse_json_string(
    const std::string& text,
    const std::string& key,
    const std::string& fallback) {
    std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return fallback;
    }
    value = text.find('"', value);
    if (value == std::string::npos) {
        return fallback;
    }
    std::string out;
    for (std::size_t index = value + 1; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '"' && (index == 0 || text[index - 1] != '\\')) {
            return out;
        }
        out.push_back(ch);
    }
    return fallback;
}

int parse_json_int(const std::string& text, const std::string& key, int fallback) {
    const std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return fallback;
    }
    try {
        return std::stoi(text.substr(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

float parse_json_float(const std::string& text, const std::string& key, float fallback) {
    const std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return fallback;
    }
    try {
        return std::stof(text.substr(value));
    } catch (const std::exception&) {
        return fallback;
    }
}

std::vector<float> parse_json_number_array(const std::string& text, const std::string& key) {
    const std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return {};
    }
    const std::size_t open = text.find('[', value);
    const std::size_t close = text.find(']', open);
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return {};
    }
    const std::string array_text = text.substr(open + 1, close - open - 1);
    std::vector<float> values;
    const char* cursor = array_text.c_str();
    while (*cursor != '\0') {
        char* end = nullptr;
        const double parsed = std::strtod(cursor, &end);
        if (end != cursor) {
            values.push_back(static_cast<float>(parsed));
            cursor = end;
            continue;
        }
        ++cursor;
    }
    return values;
}

std::vector<std::string> parse_json_string_array(const std::string& text, const std::string& key) {
    const std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return {};
    }
    const std::size_t open = text.find('[', value);
    const std::size_t close = text.find(']', open);
    if (open == std::string::npos || close == std::string::npos || close <= open) {
        return {};
    }

    std::vector<std::string> values;
    bool in_string = false;
    std::string current;
    for (std::size_t index = open + 1; index < close; ++index) {
        const char ch = text[index];
        if (ch == '"' && (index == 0 || text[index - 1] != '\\')) {
            if (in_string) {
                values.push_back(current);
                current.clear();
            }
            in_string = !in_string;
            continue;
        }
        if (in_string) {
            current.push_back(ch);
        }
    }
    return values;
}

std::vector<float> despike_cumulative_samples(
    const std::vector<float>& values,
    float threshold_px,
    float ratio) {
    if (values.size() < 3) {
        return values;
    }
    const float threshold = std::max(0.0f, threshold_px);
    const float effective_ratio = std::max(1.0f, ratio);
    std::vector<float> repaired(values);
    for (std::size_t index = 1; index + 1 < values.size(); ++index) {
        const float prev_value = values[index - 1];
        const float current = values[index];
        const float next_value = values[index + 1];
        const float prev_delta = current - prev_value;
        const float next_delta = next_value - current;
        if (prev_delta == 0.0f || next_delta == 0.0f) {
            continue;
        }
        if ((prev_delta > 0.0f) == (next_delta > 0.0f)) {
            continue;
        }
        const float local_midpoint = (prev_value + next_value) * 0.5f;
        const float spike_error = current - local_midpoint;
        const float neighbor_span = std::fabs(next_value - prev_value);
        if (std::fabs(spike_error) < threshold) {
            continue;
        }
        if (std::fabs(spike_error) < effective_ratio * std::max(threshold, neighbor_span)) {
            continue;
        }
        repaired[index] = local_midpoint;
    }
    return repaired;
}

bool is_profile_candidate(const std::filesystem::path& path) {
    const std::wstring filename = path.filename().wstring();
    if (path.extension() != std::filesystem::path(L".json")) {
        return false;
    }
    if (filename.find(L".summary.") != std::wstring::npos) {
        return false;
    }
    if (path.parent_path().filename() == std::filesystem::path(L"_episodes")) {
        return false;
    }
    return true;
}

struct RecognizerStateLite {
    std::string game;
    std::string canonical_weapon_id;
    std::vector<std::string> profile_ids;
};

std::optional<RecognizerStateLite> load_recognizer_state_lite(
    const std::filesystem::path& recognizer_state_path) {
    if (recognizer_state_path.empty() || !std::filesystem::exists(recognizer_state_path)) {
        return std::nullopt;
    }
    try {
        const std::string text = read_file(recognizer_state_path);
        RecognizerStateLite state;
        state.game = parse_json_string(text, "game", "");
        state.canonical_weapon_id = parse_json_string(text, "canonical_weapon_id", "");
        state.profile_ids = parse_json_string_array(text, "profile_ids");
        if (state.game.empty() || state.canonical_weapon_id.empty()) {
            return std::nullopt;
        }
        return state;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool has_profile_hint(const std::set<std::string>& hints, const RecoilProfile& profile) {
    return hints.empty() || hints.find(profile.profile_id) != hints.end();
}

bool profile_matches_context(
    const RecoilProfile& profile,
    const RecognizerStateLite& state,
    const std::string& aim_mode) {
    return profile.game == state.game &&
        profile.canonical_weapon_id == state.canonical_weapon_id &&
        profile.stance == "standing" &&
        profile.aim_mode == aim_mode;
}

}  // namespace

bool RecoilProfile::empty() const {
    return samples_x.empty() || samples_y.empty();
}

std::size_t RecoilProfile::sample_count() const {
    return std::min(samples_x.size(), samples_y.size());
}

RecoilProfile load_recoil_profile(const std::filesystem::path& path) {
    const std::string text = read_file(path);
    RecoilProfile profile;
    profile.profile_id = parse_json_string(text, "profile_id", "");
    if (profile.profile_id.empty()) {
        profile.profile_id = path.stem().u8string();
    }
    profile.canonical_weapon_id = parse_json_string(text, "canonical_weapon_id", "");
    profile.game = parse_json_string(text, "game", "");
    profile.stance = parse_json_string(text, "stance", "standing");
    profile.aim_mode = parse_json_string(text, "aim_mode", "");
    profile.confidence = parse_json_float(text, "confidence", 0.0f);
    profile.sample_interval_ms = std::max(1, parse_json_int(text, "sample_interval_ms", 16));
    profile.duration_ms = std::max(0, parse_json_int(text, "duration_ms", 0));
    profile.initial_delay_ms = std::max(0, parse_json_int(text, "initial_delay_ms", 0));
    profile.samples_x = parse_json_number_array(text, "samples_x");
    profile.samples_y = parse_json_number_array(text, "samples_y");
    const std::size_t count = profile.sample_count();
    profile.samples_x.resize(count);
    profile.samples_y.resize(count);
    return profile;
}

std::optional<RecoilProfile> load_first_recoil_profile(const std::filesystem::path& directory) {
    if (!std::filesystem::exists(directory)) {
        return std::nullopt;
    }
    std::optional<std::filesystem::path> fallback;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || !is_profile_candidate(entry.path())) {
            continue;
        }
        const std::wstring filename = entry.path().filename().wstring();
        if (filename.find(L"-current.json") != std::wstring::npos) {
            return load_recoil_profile(entry.path());
        }
        if (!fallback.has_value()) {
            fallback = entry.path();
        }
    }
    if (!fallback.has_value()) {
        return std::nullopt;
    }
    return load_recoil_profile(*fallback);
}

std::optional<RecoilProfile> load_matching_recoil_profile(
    const std::filesystem::path& directory,
    const std::filesystem::path& recognizer_state_path,
    const std::string& aim_mode) {
    if (!std::filesystem::exists(directory) || aim_mode.empty()) {
        return std::nullopt;
    }
    const std::optional<RecognizerStateLite> state =
        load_recognizer_state_lite(recognizer_state_path);
    if (!state.has_value()) {
        return std::nullopt;
    }

    std::vector<RecoilProfile> profiles;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || !is_profile_candidate(entry.path())) {
            continue;
        }
        try {
            RecoilProfile profile = load_recoil_profile(entry.path());
            if (!profile.empty()) {
                profiles.push_back(std::move(profile));
            }
        } catch (const std::exception&) {
        }
    }

    const std::set<std::string> hints(state->profile_ids.begin(), state->profile_ids.end());
    std::vector<RecoilProfile> hinted_profiles;
    for (const RecoilProfile& profile : profiles) {
        if (has_profile_hint(hints, profile)) {
            hinted_profiles.push_back(profile);
        }
    }
    const std::vector<RecoilProfile>& candidates =
        hints.empty() || hinted_profiles.empty() ? profiles : hinted_profiles;

    std::vector<RecoilProfile> matches;
    for (const RecoilProfile& profile : candidates) {
        if (profile_matches_context(profile, *state, aim_mode)) {
            matches.push_back(profile);
        }
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    std::sort(matches.begin(), matches.end(), [](const RecoilProfile& lhs, const RecoilProfile& rhs) {
        if (lhs.confidence != rhs.confidence) {
            return lhs.confidence > rhs.confidence;
        }
        return lhs.profile_id < rhs.profile_id;
    });
    return matches.front();
}

RecoilProfile build_recoil_playback_profile(
    const RecoilProfile& profile,
    const GamepadRecoilConfig& config) {
    if (!config.profile_despike_enabled || profile.sample_count() < 3) {
        return profile;
    }
    RecoilProfile playback = profile;
    playback.samples_x = despike_cumulative_samples(
        profile.samples_x,
        config.profile_despike_threshold_px,
        config.profile_despike_ratio);
    playback.samples_y = despike_cumulative_samples(
        profile.samples_y,
        config.profile_despike_threshold_px,
        config.profile_despike_ratio);
    return playback;
}

}  // namespace controller_native
