#include "weapon_recognizer.h"

#include "recoil_profile.h"

#if defined(_WIN32)
#include <Windows.h>
#include <Unknwn.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace controller_native {

namespace {

constexpr float kConfidenceThreshold = 0.80f;
constexpr float kImageMarginThreshold = 0.08f;
constexpr float kAgreementBonus = 0.05f;
constexpr float kDegradedConfidence = 0.55f;
constexpr int kCod20TextWindowFrames = 36;
constexpr int kCod21TextWindowFrames = 90;
constexpr int kCod22TextWindowFrames = 24;
constexpr int kSwitchCapturePrimaryDelayMs = 600;
constexpr int kSwitchCaptureBackupDelayMs = 760;

struct NormalizedRoi {
    float left = 0.0f;
    float top = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct ResolvedTextSignal {
    std::string canonical_weapon_id;
    float confidence = 0.0f;
    std::string matched_name;
};

struct ResolvedImageSignal {
    std::string canonical_weapon_id;
    float confidence = 0.0f;
};

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to read weapon recognizer file: " + path.u8string());
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("failed to write weapon recognizer state: " + path.u8string());
    }
    output << text;
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string ascii_casefold(std::string value) {
    for (char& ch : value) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 0x80) {
            ch = static_cast<char>(std::tolower(uch));
        }
    }
    return value;
}

std::string compact_name(const std::string& value) {
    std::string out;
    const std::string stripped = trim(value);
    for (char ch : stripped) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (uch < 0x80 && std::isspace(uch)) {
            continue;
        }
        out.push_back(ch);
    }
    return ascii_casefold(out);
}

bool contains_substring(const std::string& haystack, const std::string& needle) {
    return !haystack.empty() && !needle.empty() && haystack.find(needle) != std::string::npos;
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
    const std::string& fallback = "") {
    std::size_t value = find_value_start(text, key);
    if (value == std::string::npos) {
        return fallback;
    }
    value = text.find('"', value);
    if (value == std::string::npos) {
        return fallback;
    }
    std::string out;
    bool escaped = false;
    for (std::size_t index = value + 1; index < text.size(); ++index) {
        const char ch = text[index];
        if (escaped) {
            out.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\') {
            escaped = true;
            continue;
        }
        if (ch == '"') {
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
    bool escaped = false;
    std::string current;
    for (std::size_t index = open + 1; index < close; ++index) {
        const char ch = text[index];
        if (escaped) {
            current.push_back(ch);
            escaped = false;
            continue;
        }
        if (ch == '\\' && in_string) {
            escaped = true;
            continue;
        }
        if (ch == '"') {
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

std::string json_escape(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            out << ch;
            break;
        }
    }
    return out.str();
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

bool looks_like_identity_payload(const std::string& text) {
    return find_value_start(text, "canonical_weapon_id") != std::string::npos &&
        find_value_start(text, "game") != std::string::npos &&
        find_value_start(text, "display_name") != std::string::npos;
}

std::vector<RecoilWeaponIdentityRecord> load_identity_records(
    const std::filesystem::path& directory,
    const std::string& game) {
    std::vector<RecoilWeaponIdentityRecord> records;
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return records;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        try {
            const std::string text = read_file(entry.path());
            if (!looks_like_identity_payload(text)) {
                continue;
            }
            RecoilWeaponIdentityRecord record;
            record.canonical_weapon_id = parse_json_string(text, "canonical_weapon_id");
            record.game = parse_json_string(text, "game");
            record.display_name = parse_json_string(text, "display_name");
            record.alias_names = parse_json_string_array(text, "alias_names");
            record.blueprint_names = parse_json_string_array(text, "blueprint_names");
            record.signature_refs = parse_json_string_array(text, "signature_refs");
            if (
                record.game == game &&
                !record.canonical_weapon_id.empty() &&
                !record.display_name.empty()) {
                records.push_back(std::move(record));
            }
        } catch (const std::exception&) {
        }
    }
    std::sort(records.begin(), records.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.canonical_weapon_id < rhs.canonical_weapon_id;
    });
    return records;
}

std::string build_canonical_weapon_id(const std::string& game, const std::string& display_name) {
    return game + "-" + trim(display_name);
}

std::filesystem::path identity_path_for(
    const std::filesystem::path& directory,
    const std::string& game,
    const std::string& canonical_weapon_id) {
    return directory / std::filesystem::u8path("identity-" + game + "-" + canonical_weapon_id + ".json");
}

RecoilWeaponIdentityRecord create_identity_record(
    const GamepadRecoilConfig& config,
    const std::string& display_name,
    const std::string& timestamp) {
    RecoilWeaponIdentityRecord record;
    record.game = config.recognizer_game;
    record.display_name = trim(display_name);
    record.canonical_weapon_id = build_canonical_weapon_id(record.game, record.display_name);

    std::ostringstream out;
    out
        << "{\n"
        << "  \"canonical_weapon_id\": \"" << json_escape(record.canonical_weapon_id) << "\",\n"
        << "  \"created_at\": \"" << json_escape(timestamp) << "\",\n"
        << "  \"display_name\": \"" << json_escape(record.display_name) << "\",\n"
        << "  \"game\": \"" << json_escape(record.game) << "\",\n"
        << "  \"updated_at\": \"" << json_escape(timestamp) << "\"\n"
        << "}\n";
    write_file(identity_path_for(config.weapon_directory, record.game, record.canonical_weapon_id), out.str());
    return record;
}

std::vector<std::string> identity_names(const RecoilWeaponIdentityRecord& record) {
    std::vector<std::string> names = {record.canonical_weapon_id, record.display_name};
    names.insert(names.end(), record.alias_names.begin(), record.alias_names.end());
    names.insert(names.end(), record.blueprint_names.begin(), record.blueprint_names.end());
    return names;
}

bool identity_exact_match(const std::string& candidate, const RecoilWeaponIdentityRecord& record) {
    const std::string normalized_candidate = compact_name(candidate);
    if (normalized_candidate.empty()) {
        return false;
    }
    for (const std::string& name : identity_names(record)) {
        if (normalized_candidate == compact_name(name)) {
            return true;
        }
    }
    return false;
}

float sequence_ratio(const std::string& left, const std::string& right) {
    if (left.empty() || right.empty()) {
        return 0.0f;
    }
    std::vector<std::size_t> previous(right.size() + 1, 0);
    std::vector<std::size_t> current(right.size() + 1, 0);
    for (std::size_t i = 1; i <= left.size(); ++i) {
        for (std::size_t j = 1; j <= right.size(); ++j) {
            if (left[i - 1] == right[j - 1]) {
                current[j] = previous[j - 1] + 1;
            } else {
                current[j] = std::max(previous[j], current[j - 1]);
            }
        }
        std::swap(previous, current);
        std::fill(current.begin(), current.end(), 0);
    }
    const float common = static_cast<float>(previous[right.size()]);
    return (2.0f * common) / static_cast<float>(left.size() + right.size());
}

float score_identity_match(const std::string& normalized_candidate, const RecoilWeaponIdentityRecord& record) {
    float best = 0.0f;
    for (const std::string& name : identity_names(record)) {
        const std::string normalized_name = compact_name(name);
        if (normalized_name.empty()) {
            continue;
        }
        float score = sequence_ratio(normalized_candidate, normalized_name);
        if (contains_substring(normalized_candidate, normalized_name) ||
            contains_substring(normalized_name, normalized_candidate)) {
            score += 0.25f;
        }
        best = std::max(best, std::min(1.0f, score));
    }
    return best;
}

std::optional<std::string> resolve_name(
    const std::string& candidate,
    const std::vector<RecoilWeaponIdentityRecord>& records,
    const std::set<std::string>& preferred_weapon_ids) {
    const std::string normalized_candidate = compact_name(candidate);
    if (normalized_candidate.empty()) {
        return std::nullopt;
    }

    for (const RecoilWeaponIdentityRecord& record : records) {
        if (identity_exact_match(candidate, record)) {
            return record.canonical_weapon_id;
        }
    }

    std::vector<std::pair<float, std::string>> scored;
    for (const RecoilWeaponIdentityRecord& record : records) {
        const float score = score_identity_match(normalized_candidate, record);
        if (score > 0.0f) {
            scored.push_back({score, record.canonical_weapon_id});
        }
    }
    if (scored.empty()) {
        return std::nullopt;
    }
    std::sort(scored.begin(), scored.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.first != rhs.first) {
            return lhs.first > rhs.first;
        }
        return lhs.second < rhs.second;
    });
    const float top_score = scored.front().first;
    const float second_score = scored.size() > 1 ? scored[1].first : 0.0f;
    if (top_score >= 0.78f && (top_score - second_score) >= 0.08f) {
        return scored.front().second;
    }
    std::vector<std::string> preferred_close_matches;
    for (const auto& [score, weapon_id] : scored) {
        if (score < 0.78f || (top_score - score) > 0.08f) {
            continue;
        }
        if (preferred_weapon_ids.find(weapon_id) != preferred_weapon_ids.end()) {
            preferred_close_matches.push_back(weapon_id);
        }
    }
    std::sort(preferred_close_matches.begin(), preferred_close_matches.end());
    preferred_close_matches.erase(
        std::unique(preferred_close_matches.begin(), preferred_close_matches.end()),
        preferred_close_matches.end());
    if (preferred_close_matches.size() == 1) {
        return preferred_close_matches.front();
    }
    return std::nullopt;
}

float clamp_confidence(float value) {
    return std::min(1.0f, std::max(0.0f, value));
}

float text_confidence_for_game(const std::string& game, bool text_window_active) {
    if (game == "cod22") {
        return text_window_active ? 0.88f : 0.92f;
    }
    if (game == "cod21") {
        return text_window_active ? 0.93f : 0.70f;
    }
    if (game == "cod20") {
        return 0.84f;
    }
    return 0.75f;
}

float carry_forward_confidence_for_game(const std::string& game) {
    if (game == "cod20") {
        return 0.78f;
    }
    if (game == "cod21") {
        return 0.86f;
    }
    if (game == "cod22") {
        return 0.76f;
    }
    return 0.82f;
}

float image_penalty_for_game(const std::string& game) {
    return game == "cod21" ? 0.12f : 0.0f;
}

int text_window_frames_for_game(const std::string& game) {
    if (game == "cod20") {
        return kCod20TextWindowFrames;
    }
    if (game == "cod21") {
        return kCod21TextWindowFrames;
    }
    if (game == "cod22") {
        return kCod22TextWindowFrames;
    }
    return 0;
}

std::vector<std::string> normalize_ocr_lines(const std::vector<std::string>& lines) {
    std::set<std::string> seen;
    std::vector<std::string> normalized;
    for (const std::string& line : lines) {
        const std::string text = trim(line);
        if (text.empty()) {
            continue;
        }
        std::istringstream parts(text);
        std::string token;
        std::vector<std::string> kept;
        while (parts >> token) {
            std::string compact;
            bool has_non_ascii = false;
            int digit_count = 0;
            int alpha_count = 0;
            for (const char ch : token) {
                const unsigned char uch = static_cast<unsigned char>(ch);
                if (uch < 0x80 && std::isspace(uch)) {
                    continue;
                }
                compact.push_back(ch);
                if (uch >= 0x80) {
                    has_non_ascii = true;
                } else if (std::isdigit(uch)) {
                    ++digit_count;
                } else if (std::isalpha(uch)) {
                    ++alpha_count;
                }
            }
            const bool noise =
                compact.empty() ||
                (!has_non_ascii && digit_count > 0 && alpha_count == 0) ||
                (!has_non_ascii && digit_count >= 3 && digit_count > alpha_count);
            if (kept.empty() && noise) {
                kept.clear();
                break;
            }
            if (!kept.empty() && noise) {
                break;
            }
            kept.push_back(token);
        }
        if (kept.empty()) {
            continue;
        }
        std::ostringstream normalized_text;
        for (std::size_t index = 0; index < kept.size(); ++index) {
            if (index > 0) {
                normalized_text << ' ';
            }
            normalized_text << kept[index];
        }
        const std::string cleaned = normalized_text.str();
        const std::string dedup_key = ascii_casefold(cleaned);
        if (seen.find(dedup_key) != seen.end()) {
            continue;
        }
        seen.insert(dedup_key);
        normalized.push_back(cleaned);
    }
    return normalized;
}

std::vector<std::string> augment_text_candidates(const std::vector<std::string>& lines) {
    std::vector<std::string> candidates = lines;
    if (lines.size() >= 2) {
        for (std::size_t index = 0; index + 1 < lines.size(); ++index) {
            candidates.push_back(lines[index] + lines[index + 1]);
            candidates.push_back(lines[index] + " " + lines[index + 1]);
            const std::string second = trim(lines[index + 1]);
            const std::size_t split = second.find(' ');
            const std::string first_token = split == std::string::npos ? second : second.substr(0, split);
            if (!first_token.empty()) {
                candidates.push_back(lines[index] + first_token);
                candidates.push_back(lines[index] + " " + first_token);
            }
        }
        std::ostringstream joined_compact;
        std::ostringstream joined_spaced;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            joined_compact << lines[index];
            if (index > 0) {
                joined_spaced << ' ';
            }
            joined_spaced << lines[index];
        }
        candidates.push_back(joined_compact.str());
        candidates.push_back(joined_spaced.str());
    }
    return normalize_ocr_lines(candidates);
}

std::optional<ResolvedTextSignal> pick_text_signal(
    const std::vector<std::string>& text_candidates,
    const std::vector<RecoilWeaponIdentityRecord>& records,
    const std::string& game,
    bool text_window_active,
    const std::set<std::string>& preferred_weapon_ids) {
    if (game == "cod20" && !text_window_active) {
        return std::nullopt;
    }
    for (const std::string& candidate : augment_text_candidates(normalize_ocr_lines(text_candidates))) {
        const std::optional<std::string> canonical_weapon_id =
            resolve_name(candidate, records, preferred_weapon_ids);
        if (!canonical_weapon_id.has_value()) {
            continue;
        }
        return ResolvedTextSignal{
            *canonical_weapon_id,
            text_confidence_for_game(game, text_window_active),
            candidate,
        };
    }
    return std::nullopt;
}

std::set<std::string> profiled_weapon_ids_for_game(
    const std::filesystem::path& directory,
    const std::string& game) {
    std::set<std::string> weapon_ids;
    if (!std::filesystem::exists(directory) || !std::filesystem::is_directory(directory)) {
        return weapon_ids;
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (!entry.is_regular_file() || !is_profile_candidate(entry.path())) {
            continue;
        }
        try {
            const RecoilProfile profile = load_recoil_profile(entry.path());
            if (profile.game == game && !profile.canonical_weapon_id.empty()) {
                weapon_ids.insert(profile.canonical_weapon_id);
            }
        } catch (const std::exception&) {
        }
    }
    return weapon_ids;
}

std::optional<ResolvedImageSignal> pick_image_signal(
    const std::vector<RecoilSignatureMatch>& ranked_image_matches,
    const std::string& game,
    const std::set<std::string>& valid_weapon_ids) {
    std::vector<RecoilSignatureMatch> matches;
    for (const RecoilSignatureMatch& match : ranked_image_matches) {
        if (valid_weapon_ids.find(match.canonical_weapon_id) != valid_weapon_ids.end()) {
            matches.push_back(match);
        }
    }
    if (matches.empty()) {
        return std::nullopt;
    }
    const RecoilSignatureMatch& top = matches.front();
    const float second_score = matches.size() > 1 ? matches[1].score : 0.0f;
    float confidence = top.score - image_penalty_for_game(game);
    if (matches.size() > 1 && (top.score - second_score) < kImageMarginThreshold) {
        confidence -= 0.15f;
    }
    return ResolvedImageSignal{top.canonical_weapon_id, clamp_confidence(confidence)};
}

RecoilWeaponRecognitionEvent make_event(
    const std::string& game,
    const std::string& canonical_weapon_id,
    float confidence,
    const std::string& source,
    const std::string& timestamp,
    bool degraded,
    const std::string& matched_name,
    std::vector<std::string> profile_ids) {
    return RecoilWeaponRecognitionEvent{
        game,
        canonical_weapon_id,
        clamp_confidence(confidence),
        source,
        timestamp,
        degraded,
        matched_name,
        std::move(profile_ids),
    };
}

std::optional<RecoilWeaponRecognitionEvent> carry_forward(
    const std::string& previous_weapon_id,
    const std::string& game,
    const std::string& timestamp,
    bool degraded,
    std::vector<std::string> profile_ids) {
    if (previous_weapon_id.empty()) {
        return std::nullopt;
    }
    const float confidence = degraded ? kDegradedConfidence : carry_forward_confidence_for_game(game);
    return make_event(
        game,
        previous_weapon_id,
        confidence,
        "carry_forward",
        timestamp,
        degraded,
        "",
        std::move(profile_ids));
}

RecoilWeaponRecognitionEvent make_unresolved_switch_event(
    const std::string& game,
    const std::string& timestamp,
    const std::vector<std::string>& text_candidates) {
    std::string matched_name;
    const std::vector<std::string> normalized = normalize_ocr_lines(text_candidates);
    if (!normalized.empty()) {
        matched_name = normalized.front();
    }
    return make_event(
        game,
        game + "-unknown",
        0.0f,
        "switch_unresolved",
        timestamp,
        true,
        matched_name,
        {});
}

std::string dedup_key_for(const RecoilWeaponRecognitionEvent& event) {
    std::ostringstream out;
    out << event.game << '\n'
        << event.canonical_weapon_id << '\n'
        << event.source << '\n'
        << event.degraded << '\n'
        << event.matched_name << '\n';
    for (const std::string& profile_id : event.profile_ids) {
        out << profile_id << '\n';
    }
    return out.str();
}

std::string event_to_json(const RecoilWeaponRecognitionEvent& event) {
    const bool has_profile = !event.profile_ids.empty();
    std::ostringstream out;
    out << "{\n";
    out << "  \"active_profile_ids\": [";
    for (std::size_t index = 0; index < event.profile_ids.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << "\"" << json_escape(event.profile_ids[index]) << "\"";
    }
    out << "],\n";
    out << "  \"active_slot_index\": 0,\n";
    out << "  \"canonical_weapon_id\": \"" << json_escape(event.canonical_weapon_id) << "\",\n";
    out << "  \"compensation_enabled\": true,\n";
    out << "  \"confidence\": " << std::fixed << std::setprecision(2) << event.confidence << ",\n";
    out << "  \"degraded\": " << (event.degraded ? "true" : "false") << ",\n";
    out << "  \"fallback_active\": " << (has_profile ? "false" : "true") << ",\n";
    out << "  \"game\": \"" << json_escape(event.game) << "\",\n";
    out << "  \"matched_name\": ";
    if (event.matched_name.empty()) {
        out << "null,\n";
    } else {
        out << "\"" << json_escape(event.matched_name) << "\",\n";
    }
    out << "  \"mode\": \"recoil\",\n";
    out << "  \"profile_candidates\": [],\n";
    out << "  \"profile_ids\": [";
    for (std::size_t index = 0; index < event.profile_ids.size(); ++index) {
        if (index > 0) {
            out << ", ";
        }
        out << "\"" << json_escape(event.profile_ids[index]) << "\"";
    }
    out << "],\n";
    out << "  \"profile_status\": \"" << (has_profile ? "ready" : "no_profile") << "\",\n";
    out << "  \"source\": \"" << json_escape(event.source) << "\",\n";
    out << "  \"timestamp\": \"" << json_escape(event.timestamp) << "\",\n";
    out << "  \"type\": \"current_weapon\"\n";
    out << "}\n";
    return out.str();
}

NormalizedRoi weapon_name_roi_for_game(const std::string& game) {
    if (game == "cod20") {
        return NormalizedRoi{0.882f, 0.826f, 0.098f, 0.040f};
    }
    if (game == "cod21") {
        return NormalizedRoi{0.886f, 0.856f, 0.102f, 0.032f};
    }
    return NormalizedRoi{0.734f, 0.778f, 0.158f, 0.078f};
}

#if defined(_WIN32)

struct __declspec(uuid("905a0fef-bc53-11df-8c49-001e4fc686da")) NativeIBufferByteAccess : ::IUnknown {
    virtual HRESULT __stdcall Buffer(unsigned char** value) = 0;
};

struct BgraImage {
    int width = 0;
    int height = 0;
    std::vector<unsigned char> pixels;
};

BgraImage capture_primary_screen_roi(const NormalizedRoi& roi) {
    const int screen_width = GetSystemMetrics(SM_CXSCREEN);
    const int screen_height = GetSystemMetrics(SM_CYSCREEN);
    const int left = std::max(0, std::min(screen_width - 1, static_cast<int>(std::lround(roi.left * screen_width))));
    const int top = std::max(0, std::min(screen_height - 1, static_cast<int>(std::lround(roi.top * screen_height))));
    const int right = std::max(left + 1, std::min(screen_width, static_cast<int>(std::lround((roi.left + roi.width) * screen_width))));
    const int bottom = std::max(top + 1, std::min(screen_height, static_cast<int>(std::lround((roi.top + roi.height) * screen_height))));
    const int width = right - left;
    const int height = bottom - top;

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return {};
    }
    HDC memory_dc = CreateCompatibleDC(screen_dc);
    if (memory_dc == nullptr) {
        ReleaseDC(nullptr, screen_dc);
        return {};
    }

    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        DeleteDC(memory_dc);
        ReleaseDC(nullptr, screen_dc);
        return {};
    }

    HGDIOBJ previous = SelectObject(memory_dc, bitmap);
    const BOOL copied = BitBlt(memory_dc, 0, 0, width, height, screen_dc, left, top, SRCCOPY);
    BgraImage image;
    if (copied) {
        image.width = width;
        image.height = height;
        image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u);
        std::memcpy(image.pixels.data(), bits, image.pixels.size());
    }
    SelectObject(memory_dc, previous);
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    return image;
}

BgraImage upscale_nearest(const BgraImage& image, int scale) {
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty() || scale <= 1) {
        return image;
    }
    BgraImage out;
    out.width = image.width * scale;
    out.height = image.height * scale;
    out.pixels.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4u);
    for (int y = 0; y < out.height; ++y) {
        const int source_y = y / scale;
        for (int x = 0; x < out.width; ++x) {
            const int source_x = x / scale;
            const std::size_t source =
                (static_cast<std::size_t>(source_y) * image.width + static_cast<std::size_t>(source_x)) * 4u;
            const std::size_t target =
                (static_cast<std::size_t>(y) * out.width + static_cast<std::size_t>(x)) * 4u;
            std::memcpy(out.pixels.data() + target, image.pixels.data() + source, 4u);
        }
    }
    return out;
}

winrt::Windows::Media::Ocr::OcrEngine create_ocr_engine() {
    using winrt::Windows::Globalization::Language;
    using winrt::Windows::Media::Ocr::OcrEngine;
    for (const wchar_t* language_name : {L"zh-Hans", L"zh-Hant", L"en-US"}) {
        Language language(language_name);
        if (OcrEngine::IsLanguageSupported(language)) {
            auto engine = OcrEngine::TryCreateFromLanguage(language);
            if (engine != nullptr) {
                return engine;
            }
        }
    }
    return OcrEngine::TryCreateFromUserProfileLanguages();
}

std::vector<std::string> run_windows_ocr(const BgraImage& image) {
    using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
    using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
    using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
    using winrt::Windows::Storage::Streams::Buffer;

    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return {};
    }
    Buffer buffer(static_cast<uint32_t>(image.pixels.size()));
    buffer.Length(static_cast<uint32_t>(image.pixels.size()));
    unsigned char* raw = nullptr;
    buffer.as<NativeIBufferByteAccess>()->Buffer(&raw);
    if (raw == nullptr) {
        return {};
    }
    std::memcpy(raw, image.pixels.data(), image.pixels.size());

    SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
        buffer,
        BitmapPixelFormat::Bgra8,
        image.width,
        image.height,
        BitmapAlphaMode::Ignore);

    auto engine = create_ocr_engine();
    if (engine == nullptr) {
        return {};
    }
    auto result = engine.RecognizeAsync(bitmap).get();
    std::vector<std::string> lines;
    for (const auto& line : result.Lines()) {
        const std::string text = winrt::to_string(line.Text());
        if (!trim(text).empty()) {
            lines.push_back(text);
        }
        for (const auto& word : line.Words()) {
            const std::string word_text = winrt::to_string(word.Text());
            if (!trim(word_text).empty()) {
                lines.push_back(word_text);
            }
        }
    }
    return lines;
}

#endif

}  // namespace

NativeRecoilWeaponRecognizer::NativeRecoilWeaponRecognizer(const GamepadRecoilConfig& config)
    : config_(config),
      identity_records_(load_identity_records(config.weapon_directory, config.recognizer_game)) {}

std::optional<RecoilWeaponRecognitionEvent> NativeRecoilWeaponRecognizer::process_text_candidates(
    const std::vector<std::string>& text_candidates,
    const std::string& timestamp,
    bool switch_suspected) {
    return process_signals(text_candidates, {}, timestamp, switch_suspected);
}

std::optional<RecoilWeaponRecognitionEvent> NativeRecoilWeaponRecognizer::process_signals(
    const std::vector<std::string>& text_candidates,
    const std::vector<RecoilSignatureMatch>& ranked_image_matches,
    const std::string& timestamp,
    bool switch_suspected) {
    if (identity_records_.empty()) {
        if (switch_suspected) {
            runtime_state_.confirmed_weapon_id.clear();
            return make_unresolved_switch_event(config_.recognizer_game, timestamp, text_candidates);
        }
        return std::nullopt;
    }

    runtime_state_.switch_suspected = switch_suspected;
    runtime_state_.text_window_remaining = switch_suspected
        ? text_window_frames_for_game(config_.recognizer_game)
        : std::max(0, runtime_state_.text_window_remaining - 1);
    const bool text_window_active =
        runtime_state_.switch_suspected || runtime_state_.text_window_remaining > 0;

    std::set<std::string> valid_weapon_ids;
    for (const RecoilWeaponIdentityRecord& record : identity_records_) {
        if (record.game == config_.recognizer_game) {
            valid_weapon_ids.insert(record.canonical_weapon_id);
        }
    }
    if (!runtime_state_.confirmed_weapon_id.empty() &&
        valid_weapon_ids.find(runtime_state_.confirmed_weapon_id) == valid_weapon_ids.end()) {
        runtime_state_.confirmed_weapon_id.clear();
    }

    std::optional<ResolvedImageSignal> image_signal =
        pick_image_signal(ranked_image_matches, config_.recognizer_game, valid_weapon_ids);
    const std::set<std::string> preferred_weapon_ids =
        profiled_weapon_ids_for_game(config_.profile_directory, config_.recognizer_game);
    std::optional<ResolvedTextSignal> text_signal =
        pick_text_signal(
            text_candidates,
            identity_records_,
            config_.recognizer_game,
            text_window_active,
            preferred_weapon_ids);

    const std::string previous_weapon_id = runtime_state_.confirmed_weapon_id;

    if (
        image_signal.has_value() &&
        text_signal.has_value() &&
        image_signal->canonical_weapon_id != text_signal->canonical_weapon_id) {
        if (switch_suspected) {
            runtime_state_.confirmed_weapon_id.clear();
            return make_unresolved_switch_event(config_.recognizer_game, timestamp, text_candidates);
        }
        return carry_forward(
            previous_weapon_id,
            config_.recognizer_game,
            timestamp,
            true,
            profile_ids_for(previous_weapon_id));
    }

    std::optional<RecoilWeaponRecognitionEvent> event;
    if (image_signal.has_value() && text_signal.has_value()) {
        const float confidence =
            clamp_confidence(std::max(image_signal->confidence, text_signal->confidence) + kAgreementBonus);
        if (confidence >= kConfidenceThreshold) {
            event = make_event(
                config_.recognizer_game,
                image_signal->canonical_weapon_id,
                confidence,
                "image+text",
                timestamp,
                false,
                text_signal->matched_name,
                profile_ids_for(image_signal->canonical_weapon_id));
        }
    } else if (text_signal.has_value() && text_signal->confidence >= kConfidenceThreshold) {
        event = make_event(
            config_.recognizer_game,
            text_signal->canonical_weapon_id,
            text_signal->confidence,
            "switch_text",
            timestamp,
            false,
            text_signal->matched_name,
            profile_ids_for(text_signal->canonical_weapon_id));
    } else if (image_signal.has_value() && image_signal->confidence >= kConfidenceThreshold) {
        event = make_event(
            config_.recognizer_game,
            image_signal->canonical_weapon_id,
            image_signal->confidence,
            "image",
            timestamp,
            false,
            "",
            profile_ids_for(image_signal->canonical_weapon_id));
    }

    if (!event.has_value() && !previous_weapon_id.empty()) {
        if (switch_suspected) {
            event = make_unresolved_switch_event(config_.recognizer_game, timestamp, text_candidates);
        } else {
            event = carry_forward(
                previous_weapon_id,
                config_.recognizer_game,
                timestamp,
                true,
                profile_ids_for(previous_weapon_id));
        }
    } else if (!event.has_value() && switch_suspected) {
        event = make_unresolved_switch_event(config_.recognizer_game, timestamp, text_candidates);
    }

    if (event.has_value()) {
        if (event->source == "switch_unresolved") {
            runtime_state_.confirmed_weapon_id.clear();
        } else {
            runtime_state_.confirmed_weapon_id = event->canonical_weapon_id;
        }
    }
    return event;
}

bool NativeRecoilWeaponRecognizer::write_latest_state(const RecoilWeaponRecognitionEvent& event) const {
    if (config_.recognizer_state_path.empty()) {
        return false;
    }
    write_file(config_.recognizer_state_path, event_to_json(event));
    return true;
}

const std::vector<RecoilWeaponIdentityRecord>& NativeRecoilWeaponRecognizer::identity_records() const {
    return identity_records_;
}

std::vector<std::string> NativeRecoilWeaponRecognizer::profile_ids_for(
    const std::string& canonical_weapon_id) const {
    std::vector<std::string> profile_ids;
    if (canonical_weapon_id.empty() ||
        !std::filesystem::exists(config_.profile_directory) ||
        !std::filesystem::is_directory(config_.profile_directory)) {
        return profile_ids;
    }
    for (const auto& entry : std::filesystem::directory_iterator(config_.profile_directory)) {
        if (!entry.is_regular_file() || !is_profile_candidate(entry.path())) {
            continue;
        }
        try {
            const RecoilProfile profile = load_recoil_profile(entry.path());
            if (
                profile.game == config_.recognizer_game &&
                profile.canonical_weapon_id == canonical_weapon_id &&
                profile.stance == "standing") {
                profile_ids.push_back(profile.profile_id);
            }
        } catch (const std::exception&) {
        }
    }
    std::sort(profile_ids.begin(), profile_ids.end());
    profile_ids.erase(std::unique(profile_ids.begin(), profile_ids.end()), profile_ids.end());
    return profile_ids;
}

NativeRecoilHudOcr::NativeRecoilHudOcr(const GamepadRecoilConfig& config)
    : config_(config) {
#if defined(_WIN32)
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
    }
    try {
        available_ = create_ocr_engine() != nullptr;
    } catch (const std::exception&) {
        available_ = false;
    } catch (const winrt::hresult_error&) {
        available_ = false;
    }
#else
    available_ = false;
#endif
}

bool NativeRecoilHudOcr::available() const {
    return available_;
}

std::vector<std::string> NativeRecoilHudOcr::read_text_candidates() {
#if defined(_WIN32)
    if (!available_) {
        return {};
    }
    try {
        const NormalizedRoi roi = weapon_name_roi_for_game(config_.recognizer_game);
        const BgraImage captured = capture_primary_screen_roi(roi);
        std::vector<std::string> lines = run_windows_ocr(captured);
        const BgraImage upscaled = upscale_nearest(captured, 2);
        const std::vector<std::string> upscaled_lines = run_windows_ocr(upscaled);
        lines.insert(lines.end(), upscaled_lines.begin(), upscaled_lines.end());
        return augment_text_candidates(normalize_ocr_lines(lines));
    } catch (const std::exception&) {
        return {};
    } catch (const winrt::hresult_error&) {
        return {};
    }
#else
    return {};
#endif
}

NativeRecoilWeaponRuntimeRecognizer::NativeRecoilWeaponRuntimeRecognizer(
    const GamepadRecoilConfig& config)
    : recognizer_(config),
      ocr_(config),
      available_(config.enabled &&
          config.native_recognizer_enabled &&
          !config.recognizer_state_path.empty() &&
          ocr_.available()) {}

bool NativeRecoilWeaponRuntimeRecognizer::available() const {
    return available_;
}

bool NativeRecoilWeaponRuntimeRecognizer::poll_once() {
    if (!available_) {
        return false;
    }
    const std::vector<std::string> text_candidates = ocr_.read_text_candidates();
    std::optional<RecoilWeaponRecognitionEvent> event =
        recognizer_.process_text_candidates(text_candidates, recoil_weapon_utc_timestamp(), true);
    if (!event.has_value()) {
        return false;
    }
    const std::string key = dedup_key_for(*event);
    if (key == last_dedup_key_) {
        return false;
    }
    recognizer_.write_latest_state(*event);
    last_dedup_key_ = key;
    last_event_ = *event;
    return true;
}

const std::optional<RecoilWeaponRecognitionEvent>& NativeRecoilWeaponRuntimeRecognizer::last_event() const {
    return last_event_;
}

bool RecoilWeaponSwitchCaptureScheduler::update_y_button(
    bool y_down,
    std::chrono::steady_clock::time_point now) {
    bool scheduled = false;
    if (y_down && !last_y_down_) {
        pending_captures_.clear();
        pending_captures_.push_back(now + std::chrono::milliseconds(kSwitchCapturePrimaryDelayMs));
        pending_captures_.push_back(now + std::chrono::milliseconds(kSwitchCaptureBackupDelayMs));
        scheduled = true;
    }
    last_y_down_ = y_down;
    return scheduled;
}

bool RecoilWeaponSwitchCaptureScheduler::consume_due_capture(
    std::chrono::steady_clock::time_point now) {
    if (pending_captures_.empty() || pending_captures_.front() > now) {
        return false;
    }
    pending_captures_.erase(pending_captures_.begin());
    return true;
}

void RecoilWeaponSwitchCaptureScheduler::clear_pending() {
    pending_captures_.clear();
}

std::size_t RecoilWeaponSwitchCaptureScheduler::pending_count() const {
    return pending_captures_.size();
}

std::string recoil_weapon_utc_timestamp() {
    using clock = std::chrono::system_clock;
    const std::time_t now = clock::to_time_t(clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

}  // namespace controller_native
