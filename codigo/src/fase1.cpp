// fase1.cpp — Fase 1, "Dois ou Um". Ver fase1.hpp.
#include "fase1.hpp"

#include "commit_reveal.hpp"
#include "log.hpp"

namespace jogo {

std::optional<std::string> decidir_fase1(const std::map<std::string, int>& jogadas) {
    std::map<int, std::vector<std::string>> por_valor;  // valor -> quem jogou
    for (const auto& [nome, v] : jogadas) por_valor[v].push_back(nome);
    if (por_valor.size() != 2) return std::nullopt;  // todos iguais: empate
    for (const auto& [v, nomes] : por_valor) {
        if (nomes.size() == 1) return nomes.front();  // o isolado (minoria)
    }
    return std::nullopt;  // não acontece com 3 jogadores e 2 valores
}

ResultadoFase1 jogar_fase1(net::Mesh& mesh, const LerJogada& ler_jogada) {
    const char* quem = mesh.nome().c_str();
    const std::vector<std::string> outros = mesh.outros();
    util::logf(quem, "=== FASE 1: Dois ou Um (contra %s e %s) ===", outros[0].c_str(), outros[1].c_str());

    for (int tentativa = 1;; ++tentativa) {
        const int minha = ler_jogada(1, 2, "Fase 1, tentativa " + std::to_string(tentativa) + ": escolha 1 ou 2");
        auto jogadas = etapa_commit_reveal(mesh, 1, tentativa, minha, outros, 1, 2);

        std::string resumo;
        for (const auto& [nome, v] : jogadas) resumo += " " + nome + "=" + std::to_string(v);
        util::logf(quem, "fase 1, tentativa %d, jogadas:%s", tentativa, resumo.c_str());

        auto eliminado = decidir_fase1(jogadas);
        if (!eliminado) {
            util::logf(quem, "EMPATE (todos jogaram %d): repetindo a Fase 1", minha);
            continue;
        }

        ResultadoFase1 r;
        r.eliminado = *eliminado;
        r.tentativas = tentativa;
        r.jogadas = jogadas;
        for (const auto& [nome, v] : jogadas) {
            if (nome != r.eliminado) r.finalistas.push_back(nome);  // std::map: já em ordem crescente
        }
        util::logf(quem, "fase 1 decidida: %s ficou sozinho(a) com %d e foi ELIMINADO(A); finalistas %s e %s",
                   r.eliminado.c_str(), jogadas[r.eliminado], r.finalistas[0].c_str(), r.finalistas[1].c_str());
        if (r.eliminado == mesh.nome()) util::logf(quem, "fui eliminado(a): agora sou observador(a) passivo(a)");
        return r;
    }
}

}  // namespace jogo
