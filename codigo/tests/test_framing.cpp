// test_framing.cpp — Testes do framing (I/O parcial, big-endian), protocolo e cripto.
// Roda com: make test
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "crypto.hpp"
#include "framing.hpp"
#include "protocol.hpp"

using json = nlohmann::json;

static int falhas = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::fprintf(stderr, "FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++falhas;                                                     \
        }                                                                 \
    } while (0)

static void par_de_sockets(int fds[2]) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        std::perror("socketpair");
        std::exit(2);
    }
}

static void teste_cabecalho_big_endian() {
    int fds[2];
    par_de_sockets(fds);
    json m = proto::make_hello("Alice", 30);
    const std::string esperado = m.dump();
    net::send_msg(fds[0], m);

    std::vector<unsigned char> bruto(2 + esperado.size());
    CHECK(net::recv_all(fds[1], bruto.data(), bruto.size()));
    CHECK(bruto[0] == ((esperado.size() >> 8) & 0xFF));
    CHECK(bruto[1] == (esperado.size() & 0xFF));
    CHECK(std::string(bruto.begin() + 2, bruto.end()) == esperado);
    CHECK(esperado.find('\n') == std::string::npos);
    close(fds[0]);
    close(fds[1]);
}

// Escreve uma mensagem 1 byte por vez: força o recv_msg a lidar com leituras parciais
// tanto no cabeçalho quanto no payload.
static void teste_recv_parcial() {
    int fds[2];
    par_de_sockets(fds);
    json m = proto::make_commit(1, 1, "Bob", crypto::commit_hash(2, "a1b2c3d4e5f60718"));
    const std::string payload = m.dump();
    std::string pacote;
    pacote.push_back(static_cast<char>((payload.size() >> 8) & 0xFF));
    pacote.push_back(static_cast<char>(payload.size() & 0xFF));
    pacote += payload;

    std::thread escritor([&] {
        for (char c : pacote) {
            CHECK(write(fds[0], &c, 1) == 1);
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    });
    auto r = net::recv_msg(fds[1]);
    escritor.join();
    CHECK(r.has_value() && *r == m);
    close(fds[0]);
    close(fds[1]);
}

// Payload perto do máximo com buffer de envio pequeno: força send() parcial.
static void teste_send_parcial_payload_grande() {
    int fds[2];
    par_de_sockets(fds);
    int pequeno = 4096;
    setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &pequeno, sizeof(pequeno));
    setsockopt(fds[1], SOL_SOCKET, SO_RCVBUF, &pequeno, sizeof(pequeno));

    json m = {{"tipo", "TESTE"}, {"dados", std::string(65000, 'x')}};
    std::thread escritor([&] { net::send_msg(fds[0], m); });
    auto r = net::recv_msg(fds[1]);
    escritor.join();
    CHECK(r.has_value() && *r == m);
    close(fds[0]);
    close(fds[1]);
}

static void teste_payload_grande_demais() {
    int fds[2];
    par_de_sockets(fds);
    json m = {{"dados", std::string(70000, 'x')}};
    bool lancou = false;
    try {
        net::send_msg(fds[0], m);
    } catch (const net::ProtocolError&) {
        lancou = true;
    }
    CHECK(lancou);
    close(fds[0]);
    close(fds[1]);
}

// Duas mensagens coladas num único write (como o TCP pode entregar).
static void teste_mensagens_coladas() {
    int fds[2];
    par_de_sockets(fds);
    json a = proto::make_hello("Alice", 30), b = proto::make_hello("Bob", 45);
    net::send_msg(fds[0], a);
    net::send_msg(fds[0], b);
    auto ra = net::recv_msg(fds[1]);
    auto rb = net::recv_msg(fds[1]);
    CHECK(ra && *ra == a);
    CHECK(rb && *rb == b);
    close(fds[0]);
    close(fds[1]);
}

static void teste_eof() {
    int fds[2];
    par_de_sockets(fds);
    close(fds[0]);
    CHECK(!net::recv_msg(fds[1]).has_value());  // EOF limpo entre mensagens
    close(fds[1]);

    par_de_sockets(fds);
    const unsigned char meia[] = {0x00, 0x10, '{'};  // anuncia 16 bytes, manda 1
    CHECK(write(fds[0], meia, sizeof(meia)) == 3);
    close(fds[0]);
    bool lancou = false;
    try {
        net::recv_msg(fds[1]);
    } catch (const net::ProtocolError&) {
        lancou = true;
    }
    CHECK(lancou);
    close(fds[1]);
}

static void teste_cripto() {
    CHECK(crypto::sha256_hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string salt = crypto::make_salt();
    CHECK(salt.size() == 16);
    const std::string h = crypto::commit_hash(2, salt);
    CHECK(h.size() == 64);
    CHECK(h == crypto::sha256_hex("2:" + salt));
    CHECK(crypto::verify_reveal(2, salt, h));
    CHECK(!crypto::verify_reveal(1, salt, h));             // jogada trocada
    CHECK(!crypto::verify_reveal(2, "0000000000000000", h));  // salt trocado
    CHECK(crypto::make_salt() != salt);
}

static void teste_protocolo() {
    proto::validate(proto::make_register("Alice", "127.0.0.1", 50001, 30), proto::REGISTER);
    proto::validate(proto::make_hello("Alice", 30), proto::HELLO);
    proto::validate(proto::make_commit(1, 1, "Alice", std::string(64, 'a')), proto::MOVE_COMMIT);
    proto::validate(proto::make_reveal(1, 1, "Alice", 2, "a1b2c3d4e5f60718"), proto::MOVE_REVEAL);

    json roster = proto::make_roster("uuid-xyz", {{"Alice", "127.0.0.1", 50001, 30}, {"Bob", "127.0.0.1", 50002, 30}});
    auto peers = proto::parse_roster_peers(roster);
    CHECK(peers.size() == 2 && peers[1].nome == "Bob" && peers[1].porta == 50002);

    // Alice < Bob: Alice é PAR. 3 + 4 = 7 ímpar -> vence Bob (ÍMPAR).
    json r = proto::make_game_result("Carol", {"Alice", "Bob", 3, 4});
    proto::validate(r, proto::GAME_RESULT);
    CHECK(r["vencedor"] == "Bob");
    CHECK(r["fase2_detalhes"]["soma"] == 7);
    CHECK(r["fase2_detalhes"]["resultado"] == "IMPAR");

    bool lancou = false;
    try {
        proto::validate(json{{"tipo", "HELLO"}, {"nome", "Alice"}}, proto::HELLO);  // falta timeout
    } catch (const net::ProtocolError&) {
        lancou = true;
    }
    CHECK(lancou);
}

int main() {
    teste_cabecalho_big_endian();
    teste_recv_parcial();
    teste_send_parcial_payload_grande();
    teste_payload_grande_demais();
    teste_mensagens_coladas();
    teste_eof();
    teste_cripto();
    teste_protocolo();
    if (falhas == 0) {
        std::printf("OK: todos os testes passaram\n");
        return 0;
    }
    std::printf("%d verificação(ões) falharam\n", falhas);
    return 1;
}
