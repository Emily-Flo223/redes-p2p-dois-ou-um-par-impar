#include "fase2.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

#include "commit_reveal.hpp"
#include "log.hpp"
#include "protocol.hpp"

namespace jogo
{

    void jogar_fase2(net::Mesh &mesh, const ResultadoFase1 &fase1, const LerJogada &ler)
    {
        const std::string &eu = mesh.nome();
        const std::string &obs = fase1.eliminado;
        const std::string &par = fase1.finalistas[0];
        const std::string &impar = fase1.finalistas[1];

        proto::json resultado_json;

        if (eu == obs)
        {
            // Observador apenas aguarda
            util::logf(eu.c_str(), "Fase 2: sou o observador. Aguardando partida entre %s (PAR) e %s (ÍMPAR)...",
                       par.c_str(), impar.c_str());

            auto msg_par = mesh.esperar_tipo(proto::GAME_RESULT, par, mesh.prazo_padrao());
            if (!msg_par)
                throw TimeoutError("Conexão com " + par + " caiu ou prazo estourou.");

            auto msg_impar = mesh.esperar_tipo(proto::GAME_RESULT, impar, mesh.prazo_padrao());
            if (!msg_impar)
                throw TimeoutError("Conexão com " + impar + " caiu ou prazo estourou.");

            // Dupla checagem: o resultado enviado por PAR deve ser exatamente o mesmo enviado por ÍMPAR
            if (*msg_par != *msg_impar)
            {
                throw FraudeError("Os finalistas enviaram resultados (GAME_RESULT) divergentes ao observador.");
            }

            resultado_json = *msg_par;
            util::logf(eu.c_str(), "Resultado validado. Ambos os finalistas relataram o mesmo vencedor.");
        }
        else
        {
            // Finalistas jogam a Fase 2
            const std::string &adversario = (eu == par) ? impar : par;
            util::logf(eu.c_str(), "Fase 2: sou %s. Adversário: %s", (eu == par) ? "PAR" : "ÍMPAR", adversario.c_str());

            int minha_jogada = ler(0, 5, "Fase 2: escolha de 0 a 5");

            // Executa Commit-Reveal apenas com o adversário, na Fase 2, tentativa 1
            auto jogadas_fase2 = etapa_commit_reveal(mesh, 2, 1, minha_jogada, {adversario}, 0, 5);

            // Monta os detalhes da Fase 2
            proto::Fase2Detalhes d;
            d.jogador_par = par;
            d.jogador_impar = impar;
            d.jogada_par = jogadas_fase2.at(par);
            d.jogada_impar = jogadas_fase2.at(impar);

            // Gera o GAME_RESULT completo (já calcula vencedor e soma internamente)
            resultado_json = proto::make_game_result(obs, d);

            // Envia ao observador
            mesh.broadcast(resultado_json, {obs});
            util::logf(eu.c_str(), "Fase 2 concluída. Resultado enviado a %s.", obs.c_str());
        }

        // Sumário impresso na tela de TODOS os pares antes do encerramento
        std::printf("\n================ RESUMO DA PARTIDA ================\n");
        std::printf("Fase 1 (Dois ou Um): %s foi eliminado em %d tentativa(s).\n", obs.c_str(), fase1.tentativas);
        std::printf("Fase 2 (Par ou Ímpar):\n");
        std::printf("  PAR   (%s): jogou %d\n", par.c_str(), resultado_json["fase2_detalhes"]["jogada_par"].get<int>());
        std::printf("  ÍMPAR (%s): jogou %d\n", impar.c_str(), resultado_json["fase2_detalhes"]["jogada_impar"].get<int>());
        std::printf("  Soma: %d (%s)\n", resultado_json["fase2_detalhes"]["soma"].get<int>(), resultado_json["fase2_detalhes"]["resultado"].get<std::string>().c_str());
        std::printf("VENCEDOR FINAL: %s\n", resultado_json["vencedor"].get<std::string>().c_str());
        std::printf("===================================================\n\n");
    }

} // namespace jogo