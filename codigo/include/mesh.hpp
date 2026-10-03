// mesh.hpp — Parte de rede do Nó Jogador: registro no servidor e malha P2P completa (K3).
//
// Dono: Pessoa A. B (Fase 1) e C (Fase 2) usam apenas a parte "API para o jogo" abaixo
// e nunca chamam send/recv direto (CONTRATO.md §3.3 e §5).
//
// Ordem de uso (ver game_peer.cpp):
//   Mesh m(nome, timeout);
//   m.abrir_escuta(ip_p2p, porta_p2p);      // listener P2P ANTES do REGISTER
//   m.registrar(ip_srv, porta_srv);         // REGISTER -> ROSTER -> fecha com o servidor
//   m.formar_malha();                       // 3 conexões TCP + HELLO nos dois sentidos
//   ... jogo (B e C) usando broadcast/esperar ...
//   m.fechar_tudo();                        // FIN/ACK em todas as conexões P2P
//
// Threads criadas pela Mesh:
//   - 1 thread de accept (só durante formar_malha), aceita quem tem nome MENOR que o meu;
//   - 1 thread de connect por peer de nome MAIOR que o meu (só durante formar_malha);
//   - 1 thread de recepção por conexão estabelecida (vive até a conexão fechar).
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "framing.hpp"
#include "protocol.hpp"

namespace net {

// Erro que impede o peer de continuar (porta ocupada, servidor fora do ar, ROSTER
// inválido, malha não formada a tempo). O main mostra a mensagem e sai com código 1.
class MeshError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Mensagem recebida de um peer, já validada com proto::validate.
struct Recebida {
    std::string de;  // nome do peer (tirado do HELLO da conexão)
    json msg;
};

class Mesh {
public:
    Mesh(std::string meu_nome, int meu_timeout);
    ~Mesh();  // chama fechar_tudo()
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    // ---------- Montagem da rede (Pessoa A) ----------

    // socket/bind/listen em ip:porta. Lança MeshError se a porta estiver ocupada etc.
    void abrir_escuta(const std::string& ip, int porta);

    // Conecta ao servidor, envia REGISTER, espera o ROSTER e fecha a conexão
    // (graceful_close: FIN/ACK). Lança MeshError se algo der errado.
    void registrar(const std::string& ip_srv, int porta_srv);

    // Forma a malha K3 a partir do ROSTER: conecta nos peers de nome maior, aceita os
    // de nome menor, troca HELLO em cada conexão e inicia as threads de recepção.
    // Retorna quando as 2 conexões deste peer estão prontas. Lança MeshError se não
    // conseguir dentro de `prazo`.
    void formar_malha(std::chrono::seconds prazo = std::chrono::seconds(30));

    // ---------- Informações ----------
    const std::string& nome() const { return nome_; }
    const std::string& partida_id() const { return partida_id_; }
    const std::vector<proto::PeerInfo>& roster() const { return roster_; }  // inclui eu
    std::vector<std::string> outros() const;  // nomes dos outros 2 peers, em ordem crescente
    // Prazo padrão para esperar mensagens dos outros: max(timeouts do ROSTER) + 5 s
    // (CONTRATO.md §3.3).
    std::chrono::seconds prazo_padrao() const;
    bool conectado(const std::string& peer) const;  // a conexão com esse peer ainda está aberta?

    // ---------- API para o jogo (Pessoas B e C) ----------

    // Envia msg a cada nome de `destinos` (usa o mutex de envio de cada conexão).
    // Retorna false se o envio a algum deles falhou (conexão caída).
    bool enviar(const std::string& destino, const json& msg);
    bool broadcast(const json& msg, const std::vector<std::string>& destinos);

    // Espera (sem busy-wait) a mensagem com esse tipo/fase/tentativa vinda de `jogador`.
    // A mensagem é retirada da fila ao ser devolvida. Retorna nullopt se o prazo acabar
    // ou se a conexão com `jogador` fechar antes de a mensagem chegar.
    std::optional<json> esperar(int fase, int tentativa, const std::string& tipo, const std::string& jogador,
                                std::chrono::seconds prazo);

    // Versão genérica: espera a primeira mensagem de `de` com o `tipo` dado
    // (ex.: GAME_RESULT, que não tem fase). Mesmas regras de retorno.
    std::optional<json> esperar_tipo(const std::string& tipo, const std::string& de, std::chrono::seconds prazo);

    // Encerramento limpo de toda a malha: shutdown(SHUT_WR) em todas as conexões (FIN),
    // espera o FIN de cada peer (até 2 s), junta as threads de recepção e só então
    // close(). Idempotente: pode ser chamado mais de uma vez.
    void fechar_tudo();

private:
    struct Conexao {
        std::string nome;
        int fd = -1;
        std::mutex mutex_envio;  // CONTRATO.md §2: um mutex de envio por socket
        bool aberta = true;      // protegido por Mesh::m_
        std::thread receptor;
    };

    void log(const char* fmt, ...) const __attribute__((format(printf, 2, 3)));
    void validar_roster(const json& roster);
    const proto::PeerInfo* buscar_peer(const std::string& nome) const;
    void thread_accept(int esperados, std::chrono::steady_clock::time_point limite);
    void thread_connect(proto::PeerInfo alvo, std::chrono::steady_clock::time_point limite);
    void adicionar_conexao(const std::string& nome, int fd);  // inicia a thread de recepção
    void thread_recepcao(Conexao* c);
    std::optional<json> esperar_se(const std::string& de, std::chrono::seconds prazo,
                                   const std::function<bool(const json&)>& confere);

    const std::string nome_;
    const int timeout_;
    std::string ip_p2p_;
    int porta_p2p_ = 0;
    int fd_escuta_ = -1;
    std::string partida_id_;
    std::vector<proto::PeerInfo> roster_;

    mutable std::mutex m_;               // protege tudo abaixo
    std::condition_variable cv_;         // avisa: nova conexão, nova mensagem, conexão fechada
    std::map<std::string, std::unique_ptr<Conexao>> conexoes_;
    std::deque<Recebida> fila_;          // mensagens recebidas ainda não consumidas
    std::vector<std::string> erros_malha_;  // falhas das threads de accept/connect
    bool abortar_malha_ = false;  // pede às threads de accept/connect que desistam
    bool fechado_ = false;        // fechar_tudo() já rodou
};

}  // namespace net
