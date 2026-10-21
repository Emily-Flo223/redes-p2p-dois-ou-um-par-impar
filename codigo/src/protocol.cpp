// protocol.cpp — Construtores e validação das mensagens JSON.
#include "protocol.hpp"

#include <utility>

namespace proto {

json make_register(const std::string& nome, const std::string& ip_p2p, int porta_p2p, int timeout) {
    return {{"tipo", REGISTER}, {"nome", nome}, {"ip_p2p", ip_p2p}, {"porta_p2p", porta_p2p}, {"timeout", timeout}};
}

json make_roster(const std::string& partida_id, const std::vector<PeerInfo>& peers) {
    json arr = json::array();
    for (const auto& p : peers) {
        arr.push_back({{"nome", p.nome}, {"ip", p.ip}, {"porta", p.porta}, {"timeout", p.timeout}});
    }
    return {{"tipo", ROSTER}, {"partida_id", partida_id}, {"peers", arr}};
}

json make_hello(const std::string& nome, int timeout) {
    return {{"tipo", HELLO}, {"nome", nome}, {"timeout", timeout}};
}

json make_commit(int fase, int tentativa, const std::string& jogador, const std::string& hash_hex) {
    return {{"tipo", MOVE_COMMIT}, {"fase", fase}, {"tentativa", tentativa}, {"jogador", jogador}, {"hash", hash_hex}};
}

json make_reveal(int fase, int tentativa, const std::string& jogador, int jogada, const std::string& salt) {
    return {{"tipo", MOVE_REVEAL}, {"fase", fase},     {"tentativa", tentativa},
            {"jogador", jogador},  {"jogada", jogada}, {"salt", salt}};
}

json make_game_result(const std::string& fase1_eliminado, const Fase2Detalhes& d) {
    const int soma = d.jogada_par + d.jogada_impar;
    const bool deu_par = (soma % 2 == 0);
    return {{"tipo", GAME_RESULT},
            {"vencedor", deu_par ? d.jogador_par : d.jogador_impar},
            {"fase1_eliminado", fase1_eliminado},
            {"fase2_detalhes",
             {{"jogador_par", d.jogador_par},
              {"jogador_impar", d.jogador_impar},
              {"jogada_par", d.jogada_par},
              {"jogada_impar", d.jogada_impar},
              {"soma", soma},
              {"resultado", deu_par ? "PAR" : "IMPAR"}}}};
}

namespace {

enum class T { Str, Int, Arr, Obj };

void require(const json& m, const char* campo, T t) {
    auto it = m.find(campo);
    bool ok = it != m.end();
    if (ok) {
        switch (t) {
            case T::Str: ok = it->is_string(); break;
            case T::Int: ok = it->is_number_integer(); break;
            case T::Arr: ok = it->is_array(); break;
            case T::Obj: ok = it->is_object(); break;
        }
    }
    if (!ok) throw net::ProtocolError(std::string("campo ausente ou com tipo errado: ") + campo);
}

}  // namespace

std::vector<PeerInfo> parse_roster_peers(const json& roster) {
    validate(roster, ROSTER);
    std::vector<PeerInfo> out;
    for (const auto& p : roster.at("peers")) {
        require(p, "nome", T::Str);
        require(p, "ip", T::Str);
        require(p, "porta", T::Int);
        require(p, "timeout", T::Int);
        out.push_back({p["nome"], p["ip"], p["porta"], p["timeout"]});
    }
    return out;
}

void validate(const json& m, const std::string& tipo_esperado) {
    if (!m.is_object()) throw net::ProtocolError("mensagem não é um objeto JSON");
    require(m, "tipo", T::Str);
    if (m["tipo"] != tipo_esperado) {
        throw net::ProtocolError("esperava " + tipo_esperado + ", recebeu " + m["tipo"].get<std::string>());
    }
    if (tipo_esperado == REGISTER) {
        require(m, "nome", T::Str);
        require(m, "ip_p2p", T::Str);
        require(m, "porta_p2p", T::Int);
        require(m, "timeout", T::Int);
    } else if (tipo_esperado == ROSTER) {
        require(m, "partida_id", T::Str);
        require(m, "peers", T::Arr);
    } else if (tipo_esperado == HELLO) {
        require(m, "nome", T::Str);
        require(m, "timeout", T::Int);
    } else if (tipo_esperado == MOVE_COMMIT) {
        require(m, "fase", T::Int);
        require(m, "tentativa", T::Int);
        require(m, "jogador", T::Str);
        require(m, "hash", T::Str);
    } else if (tipo_esperado == MOVE_REVEAL) {
        require(m, "fase", T::Int);
        require(m, "tentativa", T::Int);
        require(m, "jogador", T::Str);
        require(m, "jogada", T::Int);
        require(m, "salt", T::Str);
    } else if (tipo_esperado == GAME_RESULT) {
        require(m, "vencedor", T::Str);
        require(m, "fase1_eliminado", T::Str);
        require(m, "fase2_detalhes", T::Obj);
    }
}

}  // namespace proto
