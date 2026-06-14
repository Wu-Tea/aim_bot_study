#include "replay_reader.h"

#include <fstream>
#include <string>

namespace replay_native {

ReplayReadResult read_replay_jsonl(const std::filesystem::path& path) {
    ReplayReadResult result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return result;
    }

    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            ++result.unsupported_lines;
        }
    }
    return result;
}

}  // namespace replay_native
