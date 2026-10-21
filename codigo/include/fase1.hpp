// fase1.hpp — Fase 1, "Dois ou Um" (Seção 4 do enunciado, CONTRATO.md §4).
//
// Dono: Pessoa B. Os três jogam 1 ou 2 com Commit-Reveal; três iguais = empate e repete
// (tentativa++); o único com o valor minoritário é eliminado e vira observador.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mesh.hpp"

namespace jogo {

// Lê a jogada do usuário em [min, max]. `pergunta` é o texto mostrado na tela.
// Quem chama decide como (teclado com prazo, valor fixo em teste...).
using LerJogada = std::function<int(int min, int max, const std::string& pergunta)>;

struct ResultadoFase1 {
    std::string eliminado;                   // vira observador passivo
    std::vector<std::string> finalistas;     // os 2 que seguem, em ordem crescente de nome
    int tentativas = 0;                      // 1 se não houve empate
    std::map<std::string, int> jogadas;      // jogadas da tentativa que decidiu
};

// Regra pura, sem rede: devolve o nome do jogador isolado no valor minoritário,
// ou nullopt se todos jogaram igual (empate).
std::optional<std::string> decidir_fase1(const std::map<std::string, int>& jogadas);

// Joga a Fase 1 inteira (com as repetições por empate). Lança FraudeError ou
// TimeoutError (commit_reveal.hpp) se alguém trapacear ou sumir.
ResultadoFase1 jogar_fase1(net::Mesh& mesh, const LerJogada& ler_jogada);

}  // namespace jogo
