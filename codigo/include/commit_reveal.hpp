// commit_reveal.hpp — Uma etapa do esquema Commit-Reveal com barreira estrita (Seção 5 do enunciado).
//
// Dono: Pessoa B. Usado pela Fase 1 (fase1.cpp) e pela Fase 2 (Pessoa C), sempre do mesmo jeito:
//   auto jogadas = jogo::etapa_commit_reveal(mesh, fase, tentativa, minha_jogada, ativos, min, max);
//
// O que a etapa faz, nesta ordem (CONTRATO.md §4):
//   1. gera salt novo (16 hex) e H = SHA-256(jogada + ":" + salt);
//   2. envia MOVE_COMMIT (só o hash) a todos os outros ativos;
//   3. BARREIRA: espera e guarda o MOVE_COMMIT de cada outro ativo da mesma (fase, tentativa);
//   4. só então envia MOVE_REVEAL (jogada + salt);
//   5. espera os MOVE_REVEAL dos outros e confere cada um contra o hash guardado e a faixa.
// Não existe outro caminho no código que envie MOVE_REVEAL.
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "mesh.hpp"

namespace jogo {

// Um REVEAL não bate com o COMMIT, a jogada está fora da faixa ou o hash do COMMIT
// não tem o formato certo. O main mostra, fecha tudo e sai com código 1.
class FraudeError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Um participante não mandou COMMIT/REVEAL dentro do prazo, ou a conexão com ele caiu.
// Mesma saída da fraude (CONTRATO.md §4, item 5).
class TimeoutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// true se h tem exatamente 64 caracteres hex minúsculos.
bool hash_valido(const std::string& h);

// Executa uma etapa completa entre `outros_ativos` (sem incluir eu) e devolve a jogada
// revelada de cada participante, eu incluído, por nome. Lança FraudeError ou TimeoutError.
std::map<std::string, int> etapa_commit_reveal(net::Mesh& mesh, int fase, int tentativa, int minha_jogada,
                                               const std::vector<std::string>& outros_ativos, int jogada_min,
                                               int jogada_max);

}  // namespace jogo
