// mesh.cpp — Registro no servidor e malha P2P K3 (ver mesh.hpp e CONTRATO.md §3).
// Dono: Pessoa A.
#include "mesh.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <set>

#include "log.hpp"

namespace net {

namespace {

using Relogio = std::chrono::steady_clock;

constexpr int PRAZO_HELLO_S = 5;          // prazo para o HELLO do outro lado chegar
constexpr int ESPERA_RECONEXAO_MS = 200;  // intervalo entre tentativas de connect()
constexpr int PRAZO_SERVIDOR_S = 10;      // por quanto tempo tentar conectar ao servidor

// Monta sockaddr_in a partir de "a.b.c.d" e porta. Lança MeshError se o IP for inválido.
sockaddr_in montar_endereco(const std::string& ip, int porta) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(static_cast<std::uint16_t>(porta));  // porta em ordem de rede
    if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) throw MeshError("IPv4 inválido: " + ip);
    return a;
}

// Define (segundos > 0) ou remove (segundos = 0) o prazo de leitura do socket.
// Com prazo, um recv() sem dados volta com erro EAGAIN depois de `segundos` e
// recv_msg lança ProtocolError, em vez de travar a thread para sempre.
void prazo_leitura(int fd, int segundos) {
    timeval tv{segundos, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// "ip:porta" local de um socket conectado (para o log bater com o Wireshark).
std::string ponta_local(int fd) {
    sockaddr_in a{};
    socklen_t t = sizeof(a);
    if (getsockname(fd, reinterpret_cast<sockaddr*>(&a), &t) != 0) return "?";
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

// Tipos que podem chegar por uma conexão P2P depois do HELLO.
bool tipo_p2p(const std::string& t) {
    return t == proto::HELLO || t == proto::MOVE_COMMIT || t == proto::MOVE_REVEAL || t == proto::GAME_RESULT;
}

}  // namespace

Mesh::Mesh(std::string meu_nome, int meu_timeout) : nome_(std::move(meu_nome)), timeout_(meu_timeout) {}

Mesh::~Mesh() { fechar_tudo(); }

void Mesh::log(const char* fmt, ...) const {
    va_list ap;
    va_start(ap, fmt);
    util::vlogf(nome_.c_str(), fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// 1. Escuta P2P
// ---------------------------------------------------------------------------
void Mesh::abrir_escuta(const std::string& ip, int porta) {
    const sockaddr_in end = montar_endereco(ip, porta);
    fd_escuta_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_escuta_ < 0) throw MeshError(std::string("socket: ") + std::strerror(errno));
    int um = 1;
    setsockopt(fd_escuta_, SOL_SOCKET, SO_REUSEADDR, &um, sizeof(um));
    if (::bind(fd_escuta_, reinterpret_cast<const sockaddr*>(&end), sizeof(end)) < 0 ||
        ::listen(fd_escuta_, 4) < 0) {
        const std::string e = std::strerror(errno);
        ::close(fd_escuta_);
        fd_escuta_ = -1;
        throw MeshError("não foi possível escutar em " + ip + ":" + std::to_string(porta) + ": " + e);
    }
    ip_p2p_ = ip;
    porta_p2p_ = porta;
    log("escuta P2P aberta em %s:%d", ip.c_str(), porta);
}

// ---------------------------------------------------------------------------
// 2. Registro no servidor central (REGISTER -> ROSTER -> FIN/ACK)
// ---------------------------------------------------------------------------
void Mesh::registrar(const std::string& ip_srv, int porta_srv) {
    const sockaddr_in end = montar_endereco(ip_srv, porta_srv);

    // O servidor pode ainda estar subindo: tenta de novo por até PRAZO_SERVIDOR_S.
    int fd = -1;
    const auto limite = Relogio::now() + std::chrono::seconds(PRAZO_SERVIDOR_S);
    for (;;) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) throw MeshError(std::string("socket: ") + std::strerror(errno));
        if (::connect(fd, reinterpret_cast<const sockaddr*>(&end), sizeof(end)) == 0) break;
        const int e = errno;
        ::close(fd);
        if (Relogio::now() >= limite) {
            throw MeshError("servidor " + ip_srv + ":" + std::to_string(porta_srv) +
                            " inacessível: " + std::strerror(e));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    log("conectado ao servidor %s:%d (porta local %s)", ip_srv.c_str(), porta_srv, ponta_local(fd).c_str());

    try {
        const json reg = proto::make_register(nome_, ip_p2p_, porta_p2p_, timeout_);
        send_msg(fd, reg);
        const std::size_t n = reg.dump().size();
        log("REGISTER enviado: cabeçalho 0x%02zx 0x%02zx = %zu bytes de JSON", (n >> 8) & 0xFF, n & 0xFF, n);

        log("aguardando os outros jogadores se registrarem...");
        const std::optional<json> roster = recv_msg(fd);  // bloqueia até o servidor ter os 3
        if (!roster) throw MeshError("servidor fechou sem enviar ROSTER (nome ou porta repetidos?)");
        validar_roster(*roster);
    } catch (const ProtocolError& e) {
        ::close(fd);
        throw MeshError(std::string("erro na conversa com o servidor: ") + e.what());
    } catch (...) {
        ::close(fd);
        throw;
    }
    graceful_close(fd);  // shutdown(SHUT_WR) -> espera FIN do servidor -> close
    log("conexão com o servidor encerrada (FIN/ACK); partida_id=%s", partida_id_.c_str());
}

void Mesh::validar_roster(const json& roster) {
    std::vector<proto::PeerInfo> peers;
    try {
        peers = proto::parse_roster_peers(roster);
    } catch (const ProtocolError& e) {
        throw MeshError(std::string("ROSTER inválido: ") + e.what());
    }
    if (peers.size() != 3) throw MeshError("ROSTER deveria ter 3 peers, tem " + std::to_string(peers.size()));
    std::set<std::string> nomes;
    bool achei_eu = false;
    for (const auto& p : peers) {
        if (!nomes.insert(p.nome).second) throw MeshError("ROSTER com nome repetido: " + p.nome);
        if (p.porta < 1 || p.porta > 65535) throw MeshError("ROSTER com porta inválida para " + p.nome);
        if (p.nome == nome_ && p.porta == porta_p2p_) achei_eu = true;
    }
    if (!achei_eu) throw MeshError("ROSTER não contém este peer");
    partida_id_ = roster["partida_id"].get<std::string>();
    roster_ = peers;
    for (const auto& p : roster_) {
        log("ROSTER: %s em %s:%d (timeout %d s)%s", p.nome.c_str(), p.ip.c_str(), p.porta, p.timeout,
            p.nome == nome_ ? " <- eu" : "");
    }
}

const proto::PeerInfo* Mesh::buscar_peer(const std::string& nome) const {
    for (const auto& p : roster_) {
        if (p.nome == nome) return &p;
    }
    return nullptr;
}

std::vector<std::string> Mesh::outros() const {
    std::vector<std::string> v;
    for (const auto& p : roster_) {
        if (p.nome != nome_) v.push_back(p.nome);
    }
    std::sort(v.begin(), v.end());
    return v;
}

std::chrono::seconds Mesh::prazo_padrao() const {
    int maior = timeout_;
    for (const auto& p : roster_) maior = std::max(maior, p.timeout);
    return std::chrono::seconds(maior + 5);
}

bool Mesh::conectado(const std::string& peer) const {
    std::lock_guard<std::mutex> lk(m_);
    auto it = conexoes_.find(peer);
    return it != conexoes_.end() && it->second->aberta && !fechado_;
}

// ---------------------------------------------------------------------------
// 3. Malha K3
// ---------------------------------------------------------------------------
// Regra para não haver conexão duplicada: em cada par de peers, o de nome MENOR
// (std::string::operator<, comparação de bytes) faz connect() no de nome MAIOR.
// Com Alice, Bob e Carol: Alice->Bob, Alice->Carol, Bob->Carol. Cada par tem
// exatamente uma conexão TCP, e as 3 juntas formam o triângulo K3.
void Mesh::formar_malha(std::chrono::seconds prazo) {
    if (roster_.empty()) throw MeshError("formar_malha chamado antes de registrar");
    const auto limite = Relogio::now() + prazo;

    std::vector<proto::PeerInfo> conectar;  // nomes maiores que o meu
    int aceitar = 0;                        // quantos têm nome menor que o meu
    for (const auto& p : roster_) {
        if (p.nome == nome_) continue;
        if (nome_ < p.nome) {
            conectar.push_back(p);
        } else {
            ++aceitar;
        }
    }
    log("malha: vou conectar em %zu peer(s) e aceitar %d conexão(ões)", conectar.size(), aceitar);

    // Tudo em paralelo: accept numa thread, cada connect na sua. Assim as conexões
    // abrem de forma concorrente e nenhuma chamada bloqueante segura as outras.
    std::vector<std::thread> ajudantes;
    if (aceitar > 0) ajudantes.emplace_back(&Mesh::thread_accept, this, aceitar, limite);
    for (const auto& p : conectar) ajudantes.emplace_back(&Mesh::thread_connect, this, p, limite);

    bool ok;
    std::string erro;
    {
        std::unique_lock<std::mutex> lk(m_);
        ok = cv_.wait_until(lk, limite, [&] { return conexoes_.size() == 2 || !erros_malha_.empty(); });
        ok = ok && erros_malha_.empty();
        if (!ok) {
            erro = erros_malha_.empty() ? "tempo esgotado formando a malha" : erros_malha_.front();
            abortar_malha_ = true;  // as threads ajudantes desistem na próxima volta
        }
    }
    for (auto& t : ajudantes) t.join();

    // Malha pronta (ou abortada): ninguém mais precisa conectar em mim.
    if (fd_escuta_ >= 0) {
        ::close(fd_escuta_);
        fd_escuta_ = -1;
        log("escuta P2P fechada (não aceita mais conexões)");
    }
    if (!ok) throw MeshError(erro);
    log("malha K3 completa: conectado a %s e %s", outros()[0].c_str(), outros()[1].c_str());
}

// Thread de accept: aceita `esperados` conexões de peers com nome menor que o meu.
// Para cada uma: lê o HELLO (com prazo), confere o nome, responde com o meu HELLO.
void Mesh::thread_accept(int esperados, Relogio::time_point limite) {
    int aceitos = 0;
    while (aceitos < esperados) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (abortar_malha_) return;
        }
        if (Relogio::now() >= limite) {
            std::lock_guard<std::mutex> lk(m_);
            erros_malha_.push_back("tempo esgotado esperando conexões de entrada");
            cv_.notify_all();
            return;
        }
        // poll de 200 ms: accept só quando há conexão pronta; entre voltas conferimos
        // prazo e pedido de aborto, então esta thread nunca fica presa no accept().
        pollfd pfd{fd_escuta_, POLLIN, 0};
        if (::poll(&pfd, 1, ESPERA_RECONEXAO_MS) <= 0) continue;

        sockaddr_in cli{};
        socklen_t t = sizeof(cli);
        const int fd = ::accept(fd_escuta_, reinterpret_cast<sockaddr*>(&cli), &t);
        if (fd < 0) continue;
        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &cli.sin_addr, ip, sizeof(ip));
        const std::string origem = std::string(ip) + ":" + std::to_string(ntohs(cli.sin_port));

        try {
            prazo_leitura(fd, PRAZO_HELLO_S);
            const std::optional<json> hello = recv_msg(fd);
            if (!hello) throw ProtocolError("fechou antes do HELLO");
            proto::validate(*hello, proto::HELLO);
            const std::string quem = (*hello)["nome"];
            const proto::PeerInfo* p = buscar_peer(quem);
            if (!p || quem == nome_) throw ProtocolError("nome '" + quem + "' não está no ROSTER");
            if (!(quem < nome_)) throw ProtocolError(quem + " tem nome maior; eu é que deveria conectar");
            {
                std::lock_guard<std::mutex> lk(m_);
                if (conexoes_.count(quem)) throw ProtocolError("conexão duplicada de " + quem);
            }
            if ((*hello)["timeout"].get<int>() != p->timeout) {
                log("aviso: timeout no HELLO de %s difere do ROSTER", quem.c_str());
            }
            log("HELLO recebido de %s (conexão de entrada %s -> %s)", quem.c_str(), origem.c_str(),
                ponta_local(fd).c_str());
            send_msg(fd, proto::make_hello(nome_, timeout_));  // resposta: troca nos dois sentidos
            log("HELLO enviado a %s", quem.c_str());
            prazo_leitura(fd, 0);
            adicionar_conexao(quem, fd);
            ++aceitos;
        } catch (const std::exception& e) {
            log("conexão de %s recusada: %s", origem.c_str(), e.what());
            ::close(fd);
        }
    }
}

// Thread de connect: conecta em `alvo` (nome maior que o meu), tentando de novo a cada
// 200 ms enquanto ele ainda não escuta; envia HELLO e espera o HELLO de resposta.
void Mesh::thread_connect(proto::PeerInfo alvo, Relogio::time_point limite) {
    sockaddr_in end{};
    try {
        end = montar_endereco(alvo.ip, alvo.porta);
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lk(m_);
        erros_malha_.push_back(e.what());
        cv_.notify_all();
        return;
    }

    int fd = -1;
    int tentativas = 0;
    for (;;) {
        {
            std::lock_guard<std::mutex> lk(m_);
            if (abortar_malha_) return;
        }
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd >= 0 && ::connect(fd, reinterpret_cast<const sockaddr*>(&end), sizeof(end)) == 0) break;
        const int e = errno;
        if (fd >= 0) ::close(fd);
        ++tentativas;
        if (Relogio::now() >= limite) {
            std::lock_guard<std::mutex> lk(m_);
            erros_malha_.push_back("não consegui conectar em " + alvo.nome + ": " + std::strerror(e));
            cv_.notify_all();
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(ESPERA_RECONEXAO_MS));
    }
    log("conectado a %s em %s:%d (porta local %s, %d tentativa(s) recusada(s) antes)", alvo.nome.c_str(),
        alvo.ip.c_str(), alvo.porta, ponta_local(fd).c_str(), tentativas);

    try {
        send_msg(fd, proto::make_hello(nome_, timeout_));  // quem conecta manda primeiro
        log("HELLO enviado a %s", alvo.nome.c_str());
        prazo_leitura(fd, PRAZO_HELLO_S);
        const std::optional<json> hello = recv_msg(fd);
        if (!hello) throw ProtocolError("fechou antes de responder o HELLO");
        proto::validate(*hello, proto::HELLO);
        if ((*hello)["nome"] != alvo.nome) {
            throw ProtocolError("esperava HELLO de " + alvo.nome + ", veio de " + (*hello)["nome"].get<std::string>());
        }
        log("HELLO recebido de %s", alvo.nome.c_str());
        prazo_leitura(fd, 0);
        adicionar_conexao(alvo.nome, fd);
    } catch (const std::exception& e) {
        ::close(fd);
        std::lock_guard<std::mutex> lk(m_);
        erros_malha_.push_back("HELLO com " + alvo.nome + " falhou: " + e.what());
        cv_.notify_all();
    }
}

void Mesh::adicionar_conexao(const std::string& nome, int fd) {
    std::lock_guard<std::mutex> lk(m_);
    auto c = std::make_unique<Conexao>();
    c->nome = nome;
    c->fd = fd;
    Conexao* bruto = c.get();
    conexoes_[nome] = std::move(c);
    bruto->receptor = std::thread(&Mesh::thread_recepcao, this, bruto);
    cv_.notify_all();
}

// ---------------------------------------------------------------------------
// 4. Recepção: uma thread por conexão; o jogo nunca chama recv() direto.
// ---------------------------------------------------------------------------
void Mesh::thread_recepcao(Conexao* c) {
    for (;;) {
        std::optional<json> msg;
        try {
            msg = recv_msg(c->fd);
        } catch (const ProtocolError& e) {
            log("conexão com %s com erro: %s", c->nome.c_str(), e.what());
            break;
        }
        if (!msg) {
            log("%s encerrou a conexão (FIN recebido)", c->nome.c_str());
            break;
        }
        // Valida antes de entregar ao jogo: tipo conhecido, campos certos e, em
        // MOVE_COMMIT/MOVE_REVEAL, "jogador" igual ao nome do HELLO desta conexão.
        try {
            if (!msg->is_object() || !msg->contains("tipo") || !(*msg)["tipo"].is_string()) {
                throw ProtocolError("mensagem sem campo tipo");
            }
            const std::string tipo = (*msg)["tipo"];
            if (!tipo_p2p(tipo)) throw ProtocolError("tipo inesperado na malha: " + tipo);
            proto::validate(*msg, tipo);
            if (tipo == proto::HELLO) continue;  // HELLO repetido: já identificado, ignora
            if ((tipo == proto::MOVE_COMMIT || tipo == proto::MOVE_REVEAL) && (*msg)["jogador"] != c->nome) {
                throw ProtocolError("campo jogador não confere com a conexão");
            }
        } catch (const ProtocolError& e) {
            log("mensagem de %s descartada: %s", c->nome.c_str(), e.what());
            continue;
        }
        std::lock_guard<std::mutex> lk(m_);
        fila_.push_back({c->nome, std::move(*msg)});
        cv_.notify_all();
    }
    std::lock_guard<std::mutex> lk(m_);
    c->aberta = false;
    cv_.notify_all();
}

// ---------------------------------------------------------------------------
// 5. API para o jogo
// ---------------------------------------------------------------------------
bool Mesh::enviar(const std::string& destino, const json& msg) {
    Conexao* c = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_);
        auto it = conexoes_.find(destino);
        if (it == conexoes_.end() || fechado_) return false;
        c = it->second.get();
    }
    std::lock_guard<std::mutex> lk(c->mutex_envio);  // nunca duas threads no mesmo socket
    if (c->fd < 0) return false;
    try {
        send_msg(c->fd, msg);
        return true;
    } catch (const ProtocolError& e) {
        log("falha ao enviar a %s: %s", destino.c_str(), e.what());
        return false;
    }
}

bool Mesh::broadcast(const json& msg, const std::vector<std::string>& destinos) {
    bool tudo_ok = true;
    for (const auto& d : destinos) tudo_ok = enviar(d, msg) && tudo_ok;
    return tudo_ok;
}

std::optional<json> Mesh::esperar_se(const std::string& de, std::chrono::seconds prazo,
                                     const std::function<bool(const json&)>& confere) {
    const auto limite = Relogio::now() + prazo;
    std::unique_lock<std::mutex> lk(m_);
    for (;;) {
        for (auto it = fila_.begin(); it != fila_.end(); ++it) {
            if (it->de == de && confere(it->msg)) {
                json m = std::move(it->msg);
                fila_.erase(it);
                return m;
            }
        }
        auto c = conexoes_.find(de);
        if (c == conexoes_.end() || !c->second->aberta) return std::nullopt;  // não vai chegar mais nada
        // Dorme até chegar algo (notify) ou o prazo acabar; sem espera ativa.
        if (cv_.wait_until(lk, limite) == std::cv_status::timeout) {
            // Última olhada antes de desistir.
            for (auto it = fila_.begin(); it != fila_.end(); ++it) {
                if (it->de == de && confere(it->msg)) {
                    json m = std::move(it->msg);
                    fila_.erase(it);
                    return m;
                }
            }
            return std::nullopt;
        }
    }
}

std::optional<json> Mesh::esperar(int fase, int tentativa, const std::string& tipo, const std::string& jogador,
                                  std::chrono::seconds prazo) {
    return esperar_se(jogador, prazo, [&](const json& m) {
        return m["tipo"] == tipo && m.value("fase", -1) == fase && m.value("tentativa", -1) == tentativa;
    });
}

std::optional<json> Mesh::esperar_tipo(const std::string& tipo, const std::string& de, std::chrono::seconds prazo) {
    return esperar_se(de, prazo, [&](const json& m) { return m["tipo"] == tipo; });
}

// ---------------------------------------------------------------------------
// 6. Encerramento limpo
// ---------------------------------------------------------------------------
// Por que não usar graceful_close() aqui: a thread de recepção já está em recv() no
// mesmo socket. Se fechar_tudo também lesse, as duas disputariam os bytes. Então:
//   a) shutdown(SHUT_WR) em todas as conexões -> sai um FIN para cada peer;
//   b) a thread de recepção de cada conexão recebe o FIN do peer (recv == 0) e termina;
//   c) se um peer não fechar em 2 s, shutdown(SHUT_RD) acorda o recv() da thread;
//   d) join em cada thread e, só então, close() (o fd não é reaproveitado enquanto
//      alguma thread ainda poderia usá-lo).
void Mesh::fechar_tudo() {
    std::vector<Conexao*> cs;
    {
        std::lock_guard<std::mutex> lk(m_);
        if (fechado_) return;
        fechado_ = true;
        abortar_malha_ = true;
        for (auto& kv : conexoes_) cs.push_back(kv.second.get());
    }
    if (fd_escuta_ >= 0) {
        ::close(fd_escuta_);
        fd_escuta_ = -1;
    }
    for (Conexao* c : cs) {
        std::lock_guard<std::mutex> lk(c->mutex_envio);  // espera um envio em andamento terminar
        ::shutdown(c->fd, SHUT_WR);
        log("FIN enviado a %s", c->nome.c_str());
    }
    {
        std::unique_lock<std::mutex> lk(m_);
        cv_.wait_for(lk, std::chrono::seconds(2), [&] {
            return std::none_of(cs.begin(), cs.end(), [](const Conexao* c) { return c->aberta; });
        });
        for (Conexao* c : cs) {
            if (c->aberta) ::shutdown(c->fd, SHUT_RD);
        }
    }
    for (Conexao* c : cs) {
        if (c->receptor.joinable()) c->receptor.join();
        std::lock_guard<std::mutex> lk(c->mutex_envio);
        ::close(c->fd);
        c->fd = -1;
        log("conexão com %s fechada (close)", c->nome.c_str());
    }
}

}  // namespace net
