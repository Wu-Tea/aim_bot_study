#include "runtime_provenance.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

int main() {
    try {
        const auto path = std::filesystem::temp_directory_path() /
            ("cod_runtime_provenance_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        { std::ofstream output(path, std::ios::binary); output << "abc"; }
        const auto digest = runtime_app::sha256_file_with_context(path);
        const auto contextual = runtime_app::sha256_file_with_context(path, "profile=dev");
        std::filesystem::remove(path);
        if (digest !=
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
            throw std::runtime_error("SHA-256 file fingerprint mismatch");
        }
        if (contextual.size() != 64 || contextual == digest) {
            throw std::runtime_error("provenance context must affect the fingerprint");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[RuntimeProvenanceTests] FAIL " << error.what() << '\n';
        return 1;
    }
}
