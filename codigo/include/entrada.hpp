// entrada.hpp — Leitura da jogada pelo teclado com prazo (timeout_jogada), sem travar o programa.
//
// Dono no CONTRATO: Pessoa C. Versão escrita pela Pessoa B para a Fase 1 poder rodar;
// C pode trocar a implementação, desde que mantenha a assinatura.
// Regra (CONTRATO.md §4): se o prazo acabar (ou o stdin fechar), usa uma jogada válida
// aleatória e avisa na tela.
#pragma once

#include <string>

namespace jogo {

// Mostra `pergunta`, espera uma linha com um inteiro em [min, max] por até `prazo_s`
// segundos (poll() no stdin). Entrada inválida pede de novo, dentro do mesmo prazo.
int ler_jogada_teclado(const std::string& quem, const std::string& pergunta, int min, int max, int prazo_s);

}  // namespace jogo
