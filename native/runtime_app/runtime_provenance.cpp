#include "runtime_provenance.h"

#include <Windows.h>
#include <wincrypt.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace runtime_app {

std::string sha256_file_with_context(
    const std::filesystem::path& path,
    std::string_view context) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};

    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContextW(
            &provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) ||
        !CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        if (provider != 0) CryptReleaseContext(provider, 0);
        return {};
    }

    bool ok = true;
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && !CryptHashData(
                hash,
                reinterpret_cast<const BYTE*>(buffer.data()),
                static_cast<DWORD>(count),
                0)) {
            ok = false;
            break;
        }
    }
    if (!input.eof()) ok = false;
    if (ok && !context.empty()) {
        const BYTE separator = 0;
        ok = CryptHashData(hash, &separator, 1, 0) != FALSE;
    }
    if (ok && !context.empty()) {
        ok = CryptHashData(
            hash,
            reinterpret_cast<const BYTE*>(context.data()),
            static_cast<DWORD>(context.size()),
            0) != FALSE;
    }

    std::array<BYTE, 32> digest{};
    DWORD digest_size = static_cast<DWORD>(digest.size());
    if (ok) {
        ok = CryptGetHashParam(hash, HP_HASHVAL, digest.data(), &digest_size, 0) != FALSE &&
            digest_size == digest.size();
    }
    CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    if (!ok) return {};

    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0');
    for (BYTE value : digest) encoded << std::setw(2) << static_cast<unsigned int>(value);
    return encoded.str();
}

}  // namespace runtime_app
