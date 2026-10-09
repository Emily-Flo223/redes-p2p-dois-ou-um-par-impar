// entrada.cpp — Leitura da jogada com prazo. Ver entrada.hpp.
#include "entrada.hpp"

#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <random>

#include "log.hpp"

namespace jogo {

namespace {

// Bytes já lidos do stdin que ainda não formaram uma linha completa (ou linhas
// digitadas a mais, que servem para a próxima pergunta). Só a thread do jogo usa.
std::string g_buffer;
bool g_stdin_fechado = false;

int aleatoria(int min, int max) {
    std::random_device rd;
    return std::uniform_int_distribution<int>(min, max)(rd);
}

// Tira uma linha de g_buffer (sem o fim de linha). false se ainda não há linha completa.
// O '\n' aqui é o Enter do teclado; não tem nada a ver com o protocolo de rede.
bool tirar_linha(std::string& linha) {
    const auto pos = g_buffer.find('\n');
    if (pos == std::string::npos) return false;
    linha = g_buffer.substr(0, pos);
    g_buffer.erase(0, pos + 1);
    if (!linha.empty() && linha.back() == '\r') linha.pop_back();
    return true;
}

bool converter(const std::string& s, int min, int max, int& out) {
    if (s.empty()) return false;
    char* fim = nullptr;
    errno = 0;
    const long v = std::strtol(s.c_str(), &fim, 10);
    while (*fim == ' ' || *fim == '\t') ++fim;
    if (*fim != '\0' || errno != 0 || v < min || v > max) return false;
    out = static_cast<int>(v);
    return true;
}

}  // namespace

int ler_jogada_teclado(const std::string& quem, const std::string& pergunta, int min, int max, int prazo_s) {
    using Relogio = std::chrono::steady_clock;
    const auto limite = Relogio::now() + std::chrono::seconds(prazo_s);
    util::logf(quem.c_str(), ">>> %s (%d a %d), prazo %d s:", pergunta.c_str(), min, max, prazo_s);

    for (;;) {
        std::string linha;
        while (tirar_linha(linha)) {
            int v = 0;
            if (converter(linha, min, max, v)) {
                util::logf(quem.c_str(), "jogada escolhida: %d", v);
                return v;
            }
            util::logf(quem.c_str(), "entrada inválida \"%s\": digite um inteiro de %d a %d", linha.c_str(), min, max);
        }

        const auto falta =
            std::chrono::duration_cast<std::chrono::milliseconds>(limite - Relogio::now()).count();
        if (g_stdin_fechado || falta <= 0) break;

        // Espera o teclado sem travar para sempre: poll() com prazo.
        pollfd p{STDIN_FILENO, POLLIN, 0};
        const int r = poll(&p, 1, static_cast<int>(falta));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;  // prazo esgotado (ou erro)
        char buf[256];
        const ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
        if (n <= 0) {
            g_stdin_fechado = true;  // EOF: entrada redirecionada acabou
            // Última linha sem Enter no fim ainda vale.
            if (!g_buffer.empty()) g_buffer.push_back('\n');
            continue;
        }
        g_buffer.append(buf, static_cast<size_t>(n));
    }

    const int v = aleatoria(min, max);
    util::logf(quem.c_str(), "%s: usando jogada aleatória %d",
               g_stdin_fechado ? "entrada encerrada" : "prazo esgotado", v);
    return v;
}

}  // namespace jogo
