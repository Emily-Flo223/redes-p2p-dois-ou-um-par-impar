// crypto.hpp — Commit-Reveal com SHA-256 (Seção 5 do enunciado).
//   H = SHA-256( to_string(V) + ":" + S ), em hex minúsculo (64 caracteres).
//   S = salt de 16 caracteres hex minúsculos (8 bytes aleatórios).
//
// Dono: Pessoa B (Commit-Reveal, barreira, Fase 1). Implementação inicial já funcional.
#pragma once

#include <string>

namespace crypto {

// SHA-256 de uma string qualquer, em hex minúsculo (64 caracteres).
std::string sha256_hex(const std::string& data);

// Gera salt de 16 caracteres hex minúsculos a partir de std::random_device.
std::string make_salt();

// Hash de compromisso da jogada: sha256_hex(std::to_string(jogada) + ":" + salt).
std::string commit_hash(int jogada, const std::string& salt);

// true se o salt tem 16 hex minúsculos e commit_hash(jogada, salt) == hash_recebido.
bool verify_reveal(int jogada, const std::string& salt, const std::string& hash_recebido);

}  // namespace crypto
