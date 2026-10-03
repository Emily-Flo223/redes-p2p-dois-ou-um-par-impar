// rendezvous_server.cpp — Servidor Central de Encontro (Seção 3.1 do enunciado).
// Uso: ./rendezvous_server <porta_servidor>
//
// Dono: Pessoa A. Fluxo (CONTRATO.md §3.2):
//   1. socket/bind/listen em 0.0.0.0:<porta>.
//   2. A thread principal só faz accept(); cada cliente aceito ganha UMA thread própria,
//      que lê o REGISTER (o recv bloqueante fica nessa thread e não trava o accept).
//   3. Com 3 REGISTER válidos e nomes distintos: gera partida_id (UUID v4) e envia o
//      mesmo ROSTER aos 3.
//   4. net::graceful_close() em cada cliente (FIN/ACK), fecha o listener e termina.
//   O servidor NUNCA participa do jogo depois disso (§9 e §12.1).
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "framing.hpp"
#include "log.hpp"
#include "protocol.hpp"

#define LOG(...) util::logf("servidor", __VA_ARGS__)

namespace {

constexpr int JOGADORES = 3;
constexpr int PRAZO_REGISTER_S = 15;  // cliente conectado tem até 15 s para mandar o REGISTER

bool parse_int(const char* s, int min, int max, int& out) {
    char* fim = nullptr;
    errno = 0;
    long v = std::strtol(s, &fim, 10);
    if (*s == '\0' || *fim != '\0' || errno != 0 || v < min || v > max) return false;
    out = static_cast<int>(v);
    return true;
}

bool ipv4_valido(const std::string& s) {
    in_addr a{};
    return inet_pton(AF_INET, s.c_str(), &a) == 1;
}

// UUID versão 4 (RFC 4122): 128 bits aleatórios, com 4 bits fixos de versão (0100)
// e 2 bits fixos de variante (10). Formato 8-4-4-4-12 em hex minúsculo.
std::string gerar_uuid_v4() {
    std::random_device rd;
    std::uint8_t b[16];
    for (auto& x : b) x = static_cast<std::uint8_t>(rd() & 0xFF);
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40);  // versão 4
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80);  // variante RFC 4122
    static const char* HEX = "0123456789abcdef";
    std::string s;
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s.push_back('-');
        s.push_back(HEX[b[i] >> 4]);
        s.push_back(HEX[b[i] & 0x0F]);
    }
    return s;
}

// Estado compartilhado entre a thread principal (accept) e as threads de cliente.
// Tudo protegido por um único mutex.
struct Registro {
    std::mutex m;
    std::vector<proto::PeerInfo> peers;  // jogadores aceitos, na ordem de chegada
    std::vector<int> fds;                // socket de cada jogador aceito (mesma ordem)
    std::set<int> pendentes;             // sockets cujo REGISTER ainda não chegou
    bool cheio = false;                  // já temos os 3?
};

// Confere os valores do REGISTER (o formato já foi checado por proto::validate).
// Devolve "" se estiver tudo certo, ou o motivo da recusa. Chamar com reg.m travado.
std::string motivo_recusa(const proto::PeerInfo& p, const Registro& r) {
    if (r.cheio) return "partida já tem 3 jogadores";
    if (p.nome.empty() || p.nome.size() > 64) return "nome vazio ou com mais de 64 caracteres";
    if (!ipv4_valido(p.ip)) return "ip_p2p não é IPv4 válido";
    if (p.porta < 1 || p.porta > 65535) return "porta_p2p fora de 1..65535";
    if (p.timeout < 5 || p.timeout > 300) return "timeout fora de 5..300";
    for (const auto& q : r.peers) {
        if (q.nome == p.nome) return "nome repetido";
        if (q.ip == p.ip && q.porta == p.porta) return "ip_p2p:porta_p2p repetido";
    }
    return "";
}

// Corpo da thread de cada cliente: lê UM REGISTER e decide se aceita.
// Aceito: guarda fd e dados no Registro (a thread principal envia o ROSTER depois).
// Recusado: fecha a conexão daquele cliente e o servidor continua esperando outros.
void atender_cliente(int fd, std::string origem, Registro& reg) {
    std::optional<net::json> msg;
    try {
        msg = net::recv_msg(fd);  // bloqueia só esta thread
        if (!msg) throw net::ProtocolError("cliente fechou sem enviar REGISTER");
        proto::validate(*msg, proto::REGISTER);
    } catch (const std::exception& e) {
        LOG("recusado %s: %s", origem.c_str(), e.what());
        std::lock_guard<std::mutex> lk(reg.m);
        reg.pendentes.erase(fd);
        ::close(fd);
        return;
    }

    const proto::PeerInfo p{(*msg)["nome"], (*msg)["ip_p2p"], (*msg)["porta_p2p"], (*msg)["timeout"]};

    std::lock_guard<std::mutex> lk(reg.m);
    reg.pendentes.erase(fd);
    const std::string motivo = motivo_recusa(p, reg);
    if (!motivo.empty()) {
        LOG("recusado %s (%s): %s", origem.c_str(), p.nome.c_str(), motivo.c_str());
        ::close(fd);
        return;
    }
    reg.peers.push_back(p);
    reg.fds.push_back(fd);
    if (static_cast<int>(reg.peers.size()) == JOGADORES) reg.cheio = true;
    LOG("REGISTER de %s: nome=%s p2p=%s:%d timeout=%d [%zu/%d]", origem.c_str(), p.nome.c_str(), p.ip.c_str(),
        p.porta, p.timeout, reg.peers.size(), JOGADORES);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "Uso: %s <porta_servidor>\n", argv[0]);
        return 1;
    }
    int porta = 0;
    if (!parse_int(argv[1], 1, 65535, porta)) {
        std::fprintf(stderr, "porta_servidor deve ser um inteiro entre 1 e 65535\n");
        return 1;
    }

    // 1. Socket de escuta TCP/IPv4.
    const int lfd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        std::perror("socket");
        return 1;
    }
    // SO_REUSEADDR: permite reabrir a porta logo após um teste anterior (estado TIME_WAIT).
    int um = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &um, sizeof(um));

    sockaddr_in end{};
    end.sin_family = AF_INET;
    end.sin_addr.s_addr = htonl(INADDR_ANY);                  // 0.0.0.0: todas as interfaces
    end.sin_port = htons(static_cast<std::uint16_t>(porta));  // porta em ordem de rede
    if (::bind(lfd, reinterpret_cast<sockaddr*>(&end), sizeof(end)) < 0) {
        std::perror("bind");
        ::close(lfd);
        return 1;
    }
    if (::listen(lfd, 8) < 0) {
        std::perror("listen");
        ::close(lfd);
        return 1;
    }
    LOG("escutando em 0.0.0.0:%d, aguardando %d jogadores", porta, JOGADORES);

    // 2. Laço de accept na thread principal; uma thread por cliente.
    Registro reg;
    std::vector<std::thread> threads;
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(reg.m);
            if (reg.cheio) break;
        }
        // poll com prazo de 200 ms: accept só é chamado quando há conexão pronta,
        // e a cada volta conferimos se os 3 jogadores já chegaram.
        pollfd pfd{lfd, POLLIN, 0};
        const int pr = ::poll(&pfd, 1, 200);
        if (pr < 0 && errno != EINTR) {
            std::perror("poll");
            break;
        }
        if (pr <= 0) continue;

        sockaddr_in cli{};
        socklen_t tam = sizeof(cli);
        const int cfd = ::accept(lfd, reinterpret_cast<sockaddr*>(&cli), &tam);
        if (cfd < 0) {
            if (errno != EINTR) std::perror("accept");
            continue;
        }
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        const std::string origem = std::string(ip) + ":" + std::to_string(ntohs(cli.sin_port));
        LOG("conexão aceita de %s", origem.c_str());

        // Prazo para o REGISTER chegar: um cliente parado não segura a thread para sempre.
        timeval tv{PRAZO_REGISTER_S, 0};
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        {
            std::lock_guard<std::mutex> lk(reg.m);
            reg.pendentes.insert(cfd);
        }
        threads.emplace_back(atender_cliente, cfd, origem, std::ref(reg));
    }

    // 3. Quórum atingido: ninguém mais entra. Fecha o listener e acorda (shutdown)
    //    clientes extras que ainda estejam no meio do REGISTER, para suas threads terminarem.
    ::close(lfd);
    {
        std::lock_guard<std::mutex> lk(reg.m);
        for (int fd : reg.pendentes) ::shutdown(fd, SHUT_RDWR);
    }
    for (auto& t : threads) t.join();

    if (static_cast<int>(reg.peers.size()) != JOGADORES) {
        LOG("encerrando sem formar partida");
        for (int fd : reg.fds) net::graceful_close(fd);
        return 1;
    }

    const std::string partida_id = gerar_uuid_v4();
    const net::json roster = proto::make_roster(partida_id, reg.peers);
    LOG("quórum atingido; partida_id=%s", partida_id.c_str());

    // 4. Mesmo ROSTER para os 3; depois fechamento gracioso (FIN nos dois sentidos).
    for (std::size_t i = 0; i < reg.fds.size(); ++i) {
        try {
            net::send_msg(reg.fds[i], roster);
            LOG("ROSTER enviado a %s (%zu bytes de JSON)", reg.peers[i].nome.c_str(), roster.dump().size());
        } catch (const std::exception& e) {
            LOG("falha ao enviar ROSTER a %s: %s", reg.peers[i].nome.c_str(), e.what());
        }
    }
    for (std::size_t i = 0; i < reg.fds.size(); ++i) {
        net::graceful_close(reg.fds[i]);
        LOG("conexão com %s encerrada", reg.peers[i].nome.c_str());
    }
    LOG("servidor encerrado; o jogo segue só entre os peers (P2P)");
    return 0;
}
