// peer_trapaceiro.cpp — Peer de TESTE que trapaceia na Fase 1, para provar que a fraude é detectada.
// Mesmos 6 parâmetros do game_peer. Faz o COMMIT de uma jogada e, depois da barreira,
// revela a outra (com o mesmo salt). Os peers honestos devem acusar FRAUDE e sair.
// Não faz parte da entrega do jogo; só é compilado por tests/teste_fase1.sh.
#include <cstdlib>
#include <string>

#include "crypto.hpp"
#include "log.hpp"
#include "mesh.hpp"
#include "protocol.hpp"

int main(int argc, char** argv) {
    if (argc != 7) return 1;
    const std::string nome = argv[5];
    net::Mesh mesh(nome, std::atoi(argv[6]));
    try {
        mesh.abrir_escuta(argv[3], std::atoi(argv[4]));
        mesh.registrar(argv[1], std::atoi(argv[2]));
        mesh.formar_malha();

        const std::string salt = crypto::make_salt();
        mesh.broadcast(proto::make_commit(1, 1, nome, crypto::commit_hash(1, salt)), mesh.outros());
        util::logf(nome.c_str(), "TRAPAÇA: COMMIT da jogada 1");
        for (const auto& p : mesh.outros()) mesh.esperar(1, 1, proto::MOVE_COMMIT, p, mesh.prazo_padrao());
        mesh.broadcast(proto::make_reveal(1, 1, nome, 2, salt), mesh.outros());
        util::logf(nome.c_str(), "TRAPAÇA: REVEAL da jogada 2 com o mesmo salt");
        for (const auto& p : mesh.outros()) mesh.esperar(1, 1, proto::MOVE_REVEAL, p, mesh.prazo_padrao());
    } catch (const std::exception& e) {
        util::logf(nome.c_str(), "erro: %s", e.what());
    }
    mesh.fechar_tudo();
    return 0;
}
