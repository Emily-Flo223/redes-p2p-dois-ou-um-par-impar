// test_fase1.cpp — Testes sem rede da Pessoa B: regra da Fase 1, hash do Commit-Reveal e salt.
#include <cstdio>
#include <set>
#include <string>

#include "commit_reveal.hpp"
#include "crypto.hpp"
#include "fase1.hpp"

namespace {

int falhas = 0;

void confere(bool ok, const char* o_que) {
    if (!ok) {
        std::printf("FALHOU: %s\n", o_que);
        ++falhas;
    }
}

}  // namespace

int main() {
    using jogo::decidir_fase1;

    // Fase 1: três iguais = empate; senão o isolado é eliminado (as 8 combinações).
    confere(!decidir_fase1({{"Alice", 1}, {"Bob", 1}, {"Carol", 1}}), "1,1,1 é empate");
    confere(!decidir_fase1({{"Alice", 2}, {"Bob", 2}, {"Carol", 2}}), "2,2,2 é empate");
    confere(decidir_fase1({{"Alice", 2}, {"Bob", 1}, {"Carol", 1}}) == std::string("Alice"), "2,1,1 elimina Alice");
    confere(decidir_fase1({{"Alice", 1}, {"Bob", 2}, {"Carol", 1}}) == std::string("Bob"), "1,2,1 elimina Bob");
    confere(decidir_fase1({{"Alice", 1}, {"Bob", 1}, {"Carol", 2}}) == std::string("Carol"), "1,1,2 elimina Carol");
    confere(decidir_fase1({{"Alice", 1}, {"Bob", 2}, {"Carol", 2}}) == std::string("Alice"), "1,2,2 elimina Alice");
    confere(decidir_fase1({{"Alice", 2}, {"Bob", 1}, {"Carol", 2}}) == std::string("Bob"), "2,1,2 elimina Bob");
    confere(decidir_fase1({{"Alice", 2}, {"Bob", 2}, {"Carol", 1}}) == std::string("Carol"), "2,2,1 elimina Carol");

    // Hash do compromisso: exemplo do CONTRATO.md §3.5, conferido com `printf '2:a1b2c3d4e5f60718' | sha256sum`.
    const std::string h = crypto::commit_hash(2, "a1b2c3d4e5f60718");
    confere(h == "8fd486e9cc1f07ba3b1587c694c70439c48c62c30e8e07b24e74d875a30b6e1f", "SHA-256(\"2:a1b2...\")");
    confere(jogo::hash_valido(h), "hash de 64 hex minúsculos é válido");
    confere(crypto::verify_reveal(2, "a1b2c3d4e5f60718", h), "REVEAL honesto confere");
    confere(!crypto::verify_reveal(1, "a1b2c3d4e5f60718", h), "REVEAL com outra jogada é fraude");
    confere(!crypto::verify_reveal(2, "a1b2c3d4e5f60719", h), "REVEAL com outro salt é fraude");
    confere(!jogo::hash_valido(h.substr(1)), "hash de 63 caracteres é inválido");
    std::string maiusculo = h;
    maiusculo[0] = 'F';
    confere(!jogo::hash_valido(maiusculo), "hash com maiúscula é inválido");
    confere(!jogo::hash_valido("2"), "jogada aberta no lugar do hash é inválida");

    // Salt: 16 hex minúsculos e novo a cada jogada.
    std::set<std::string> salts;
    for (int i = 0; i < 1000; ++i) {
        const std::string s = crypto::make_salt();
        confere(s.size() == 16 && s.find_first_not_of("0123456789abcdef") == std::string::npos, "salt 16 hex");
        salts.insert(s);
    }
    confere(salts.size() == 1000, "1000 salts diferentes");

    if (falhas == 0) std::printf("OK: testes da Fase 1 e do Commit-Reveal passaram\n");
    return falhas == 0 ? 0 : 1;
}
