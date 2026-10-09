// crypto.cpp — SHA-256 via PicoSHA2 (header único, licença MIT, em third_party/).
#include "crypto.hpp"

#include <cstdint>
#include <random>

#include "picosha2.h"

namespace crypto {

std::string sha256_hex(const std::string& data) {
    return picosha2::hash256_hex_string(data);
}

std::string make_salt() {
    static const char* HEX = "0123456789abcdef";
    std::random_device rd;
    std::string salt;
    salt.reserve(16);
    for (int i = 0; i < 16; ++i) salt.push_back(HEX[rd() & 0xF]);
    return salt;
}

std::string commit_hash(int jogada, const std::string& salt) {
    return sha256_hex(std::to_string(jogada) + ":" + salt);
}

bool verify_reveal(int jogada, const std::string& salt, const std::string& hash_recebido) {
    if (salt.size() != 16) return false;
    for (char c : salt) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return commit_hash(jogada, salt) == hash_recebido;
}

}  // namespace crypto
