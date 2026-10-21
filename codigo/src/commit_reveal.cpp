// commit_reveal.cpp — Etapa Commit-Reveal com barreira estrita. Ver commit_reveal.hpp.
#include "commit_reveal.hpp"

#include <chrono>

#include "crypto.hpp"
#include "log.hpp"
#include "protocol.hpp"

namespace jogo {

namespace {

using Relogio = std::chrono::steady_clock;

// Tempo que falta até `limite`, arredondado para cima em segundos (Mesh::esperar recebe
// segundos). Nunca 0, para que uma mensagem já na fila ainda seja apanhada.
std::chrono::seconds restante(Relogio::time_point limite) {
    auto falta = std::chrono::ceil<std::chrono::seconds>(limite - Relogio::now());
    return falta.count() < 1 ? std::chrono::seconds(1) : falta;
}

// Mensagem de erro para um esperar() que devolveu nullopt: prazo ou conexão caída.
std::string motivo_falta(net::Mesh& mesh, const std::string& quem, const char* tipo, int fase, int tentativa) {
    const std::string onde = std::string(tipo) + " da fase " + std::to_string(fase) + " (tentativa " +
                             std::to_string(tentativa) + ") de " + quem;
    if (!mesh.conectado(quem)) return "conexão com " + quem + " caiu antes do " + onde;
    return "timeout esperando " + onde;
}

}  // namespace

bool hash_valido(const std::string& h) {
    if (h.size() != 64) return false;
    for (char c : h) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

std::map<std::string, int> etapa_commit_reveal(net::Mesh& mesh, int fase, int tentativa, int minha_jogada,
                                               const std::vector<std::string>& outros_ativos, int jogada_min,
                                               int jogada_max) {
    const std::string& eu = mesh.nome();
    const char* quem = eu.c_str();

    // ---------- 1. Compromisso: salt novo a cada etapa ----------
    const std::string salt = crypto::make_salt();
    const std::string hash = crypto::commit_hash(minha_jogada, salt);
    util::logf(quem, "fase %d, tentativa %d: minha jogada %d, salt %s", fase, tentativa, minha_jogada, salt.c_str());
    util::logf(quem, "  H = SHA-256(\"%d:%s\") = %s", minha_jogada, salt.c_str(), hash.c_str());

    // ---------- 2. MOVE_COMMIT a todos os outros ativos (só o hash) ----------
    if (!mesh.broadcast(proto::make_commit(fase, tentativa, eu, hash), outros_ativos)) {
        throw TimeoutError("não consegui enviar MOVE_COMMIT a todos (conexão caída)");
    }
    util::logf(quem, "MOVE_COMMIT enviado (fase %d, tentativa %d)", fase, tentativa);

    // ---------- 3. Barreira: COMMIT de todos os outros guardado em memória ----------
    std::map<std::string, std::string> commits;  // jogador -> hash recebido
    auto limite = Relogio::now() + mesh.prazo_padrao();
    for (const auto& p : outros_ativos) {
        auto m = mesh.esperar(fase, tentativa, proto::MOVE_COMMIT, p, restante(limite));
        if (!m) throw TimeoutError(motivo_falta(mesh, p, "MOVE_COMMIT", fase, tentativa));
        const std::string h = (*m)["hash"];
        if (!hash_valido(h)) throw FraudeError(p + " mandou MOVE_COMMIT com hash inválido: " + h);
        commits[p] = h;
        util::logf(quem, "MOVE_COMMIT recebido de %s: %s", p.c_str(), h.c_str());
    }
    // Daqui para baixo `commits` tem o hash de TODOS os outros ativos: barreira cumprida.
    util::logf(quem, "barreira cumprida: %zu/%zu MOVE_COMMIT recebidos, liberado o MOVE_REVEAL", commits.size(),
               outros_ativos.size());

    // ---------- 4. MOVE_REVEAL (jogada e salt abertos) ----------
    if (!mesh.broadcast(proto::make_reveal(fase, tentativa, eu, minha_jogada, salt), outros_ativos)) {
        throw TimeoutError("não consegui enviar MOVE_REVEAL a todos (conexão caída)");
    }
    util::logf(quem, "MOVE_REVEAL enviado (fase %d, tentativa %d)", fase, tentativa);

    // ---------- 5. Recebe e confere os REVEAL dos outros ----------
    std::map<std::string, int> jogadas{{eu, minha_jogada}};
    limite = Relogio::now() + mesh.prazo_padrao();
    for (const auto& p : outros_ativos) {
        auto m = mesh.esperar(fase, tentativa, proto::MOVE_REVEAL, p, restante(limite));
        if (!m) throw TimeoutError(motivo_falta(mesh, p, "MOVE_REVEAL", fase, tentativa));
        const int v = (*m)["jogada"];
        const std::string s = (*m)["salt"];
        if (!crypto::verify_reveal(v, s, commits[p])) {
            throw FraudeError(p + " revelou jogada " + std::to_string(v) + " com salt \"" + s +
                              "\", mas SHA-256 não confere com o MOVE_COMMIT " + commits[p]);
        }
        if (v < jogada_min || v > jogada_max) {
            throw FraudeError(p + " revelou jogada " + std::to_string(v) + ", fora da faixa " +
                              std::to_string(jogada_min) + ".." + std::to_string(jogada_max));
        }
        jogadas[p] = v;
        util::logf(quem, "MOVE_REVEAL de %s conferido: jogada %d, salt %s, hash OK", p.c_str(), v, s.c_str());
    }
    return jogadas;
}

}  // namespace jogo
