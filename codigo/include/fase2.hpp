// fase2.hpp — Fase 2, "Par ou Ímpar" e encerramento.
// Responsabilidade da Pessoa C.
#pragma once

#include "fase1.hpp"
#include "mesh.hpp"

namespace jogo
{

    // Executa a Fase 2 exclusivamente entre os finalistas, isolando o observador.
    // Ao final, o resultado é validado pelo observador e o sumário é exibido na tela.
    void jogar_fase2(net::Mesh &mesh, const ResultadoFase1 &fase1, const LerJogada &ler);

} // namespace jogo