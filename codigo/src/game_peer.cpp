// game_peer.cpp — Nó Jogador (Seção 3.2 do enunciado).
// Uso: ./game_peer <ip_servidor> <porta_servidor> <ip_escuta_p2p> <porta_escuta_p2p> <nome> <timeout_jogada>
//
// Donos: Pessoa A (argumentos, registro, malha K3, HELLO, threads de recepção — ver mesh.hpp),
//        Pessoa B (Commit-Reveal, barreira, Fase 1) e Pessoa C (Fase 2, observador,
//        GAME_RESULT, entrada da jogada com timeout). Fluxo em CONTRATO.md §3 e §4.
#include <arpa/inet.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "commit_reveal.hpp"
#include "crypto.hpp"
#include "entrada.hpp"
#include "fase1.hpp"
#include "framing.hpp"
#include "log.hpp"
#include "mesh.hpp"
#include "protocol.hpp"

namespace {

// Converte s para inteiro em [min, max]. Recusa vazio, sobras ("50a"), estouro e
// valores fora da faixa. atoi() não serve: atoi("abc") devolve 0 sem avisar.
bool parse_int(const char* s, int min, int max, int& out) {
    char* fim = nullptr;
    errno = 0;
    long v = std::strtol(s, &fim, 10);
    if (*s == '\0' || *fim != '\0' || errno != 0 || v < min || v > max) return false;
    out = static_cast<int>(v);
    return true;
}

bool ipv4_valido(const char* s) {
    in_addr a{};
    return inet_pton(AF_INET, s, &a) == 1;
}

// Nome: 1 a 64 caracteres visíveis, sem espaço (é usado no desempate PAR/ÍMPAR e
// como identificador nas mensagens; espaços confundiriam a leitura na tela e no Wireshark).
bool nome_valido(const std::string& n) {
    if (n.empty() || n.size() > 64) return false;
    for (unsigned char ch : n) {
        if (ch <= 0x20 || ch == 0x7F) return false;
    }
    return true;
}

void uso(const char* prog) {
    std::fprintf(stderr,
                 "Uso: %s <ip_servidor> <porta_servidor> <ip_escuta_p2p> <porta_escuta_p2p> <nome> <timeout_jogada>\n"
                 "  ip_servidor, ip_escuta_p2p: IPv4 (ex.: 127.0.0.1)\n"
                 "  porta_servidor, porta_escuta_p2p: 1 a 65535\n"
                 "  nome: 1 a 64 caracteres, sem espaços\n"
                 "  timeout_jogada: 5 a 300 segundos\n",
                 prog);
}

}  // namespace

int main(int argc, char** argv) {
    // ---------- 1. Argumentos (§3.2): exatamente 6 ----------
    if (argc != 7) {
        uso(argv[0]);
        return 1;
    }
    int porta_srv = 0, porta_p2p = 0, timeout = 0;
    if (!ipv4_valido(argv[1]) || !ipv4_valido(argv[3])) {
        std::fprintf(stderr, "erro: ip_servidor e ip_escuta_p2p devem ser IPv4 válidos\n");
        uso(argv[0]);
        return 1;
    }
    if (!parse_int(argv[2], 1, 65535, porta_srv) || !parse_int(argv[4], 1, 65535, porta_p2p)) {
        std::fprintf(stderr, "erro: portas devem ser inteiros entre 1 e 65535\n");
        uso(argv[0]);
        return 1;
    }
    const std::string nome = argv[5];
    if (!nome_valido(nome)) {
        std::fprintf(stderr, "erro: nome deve ter de 1 a 64 caracteres, sem espaços\n");
        uso(argv[0]);
        return 1;
    }
    if (!parse_int(argv[6], 5, 300, timeout)) {
        std::fprintf(stderr, "erro: timeout_jogada deve ser um inteiro entre 5 e 300 segundos\n");
        uso(argv[0]);
        return 1;
    }

    net::Mesh mesh(nome, timeout);
    try {
        // ---------- 2. Rede (Pessoa A) ----------
        mesh.abrir_escuta(argv[3], porta_p2p);  // antes do REGISTER (CONTRATO.md §3.1)
        mesh.registrar(argv[1], porta_srv);     // REGISTER -> ROSTER -> FIN/ACK com o servidor
        mesh.formar_malha();                    // 3 conexões TCP + HELLO nos dois sentidos

        // ---------- 3. Jogo (Pessoas B e C) ----------
        // Ponto de extensão. Daqui em diante a rede está pronta e o servidor já saiu.
        // Use somente:
        //   mesh.outros()                  -> nomes dos outros 2 peers
        //   mesh.broadcast(msg, destinos)  -> envia a vários (mutex por socket já tratado)
        //   mesh.esperar(fase, tentativa, tipo, jogador, prazo) -> recebe sem busy-wait
        //   mesh.esperar_tipo(tipo, de, prazo)                  -> ex.: GAME_RESULT
        //   mesh.prazo_padrao()            -> max(timeouts) + 5 s
        // Mensagens sempre montadas com proto::make_* (protocol.hpp).
        //
        // Leitura da jogada: teclado com prazo timeout_jogada (entrada.hpp).
        const jogo::LerJogada ler = [&](int min, int max, const std::string& pergunta) {
            return jogo::ler_jogada_teclado(nome, pergunta, min, max, timeout);
        };

        // Fase 1 (Pessoa B): Commit-Reveal entre os 3, repete em empate.
        const jogo::ResultadoFase1 fase1 = jogo::jogar_fase1(mesh, ler);

        // TODO(C): jogar_fase2(mesh, fase1, ler);                          // src/fase2.cpp
        //   Finalistas: fase1.finalistas (ordem crescente: [0] = PAR, [1] = ÍMPAR).
        //   Commit-Reveal da Fase 2: jogo::etapa_commit_reveal(mesh, 2, 1, jogada, {adversario}, 0, 5).
        util::logf(nome.c_str(), "fase 1 concluída (eliminado: %s); fase 2 ainda não implementada",
                   fase1.eliminado.c_str());

        // ---------- 4. Encerramento ----------
        mesh.fechar_tudo();  // FIN/ACK em todas as conexões P2P, depois close()
    } catch (const jogo::FraudeError& e) {
        std::fprintf(stderr, "[%s] FRAUDE DETECTADA: %s\n", nome.c_str(), e.what());
        mesh.fechar_tudo();
        return 2;
    } catch (const jogo::TimeoutError& e) {
        std::fprintf(stderr, "[%s] partida abortada: %s\n", nome.c_str(), e.what());
        mesh.fechar_tudo();
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[%s] erro: %s\n", nome.c_str(), e.what());
        mesh.fechar_tudo();
        return 1;
    }
    return 0;
}
