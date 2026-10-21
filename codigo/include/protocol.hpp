// protocol.hpp — Construtores e validação das mensagens JSON do protocolo.
// Ver CONTRATO.md para a semântica de cada campo. Pessoa A e Pessoa B usam
// SEMPRE estas funções para montar mensagens, para que os nomes de campo não divirjam.
#pragma once

#include <string>
#include <vector>

#include "framing.hpp"

namespace proto {

using json = nlohmann::json;

// Valores do campo "tipo".
inline constexpr const char* REGISTER = "REGISTER";
inline constexpr const char* ROSTER = "ROSTER";
inline constexpr const char* HELLO = "HELLO";
inline constexpr const char* MOVE_COMMIT = "MOVE_COMMIT";
inline constexpr const char* MOVE_REVEAL = "MOVE_REVEAL";
inline constexpr const char* GAME_RESULT = "GAME_RESULT";

struct PeerInfo {
    std::string nome;
    std::string ip;
    int porta = 0;
    int timeout = 0;
};

// Peer -> Servidor
json make_register(const std::string& nome, const std::string& ip_p2p, int porta_p2p, int timeout);
// Servidor -> Peers
json make_roster(const std::string& partida_id, const std::vector<PeerInfo>& peers);
// P2P (os dois lados enviam logo após a conexão TCP abrir)
json make_hello(const std::string& nome, int timeout);
// P2P. "tentativa" é extensão nossa (ver CONTRATO.md §4): começa em 1 e sobe a cada empate.
json make_commit(int fase, int tentativa, const std::string& jogador, const std::string& hash_hex);
json make_reveal(int fase, int tentativa, const std::string& jogador, int jogada, const std::string& salt);

struct Fase2Detalhes {
    std::string jogador_par;
    std::string jogador_impar;
    int jogada_par = 0;
    int jogada_impar = 0;
};
// P2P (finalistas -> observador). Calcula soma, resultado e vencedor a partir de d.
json make_game_result(const std::string& fase1_eliminado, const Fase2Detalhes& d);

// Converte o array "peers" de um ROSTER em PeerInfo. Lança net::ProtocolError se faltar campo.
std::vector<PeerInfo> parse_roster_peers(const json& roster);

// Verifica se msg tem "tipo" == tipo esperado e todos os campos obrigatórios
// com o tipo JSON certo. Lança net::ProtocolError com mensagem clara se não tiver.
void validate(const json& msg, const std::string& tipo_esperado);

}  // namespace proto
