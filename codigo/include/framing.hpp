// framing.hpp — Empacotamento de mensagens do protocolo (Seção 6 do enunciado).
//
// Formato de cada mensagem no fio:
//   [ 2 bytes: tamanho N do payload, unsigned, big-endian ][ N bytes: JSON UTF-8 ]
// Nenhum delimitador textual ('\n') é usado. Envio e recepção tratam I/O parcial em laço.
//
// Dono: Pessoa A (rede/servidor/framing).
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>

#include "json.hpp"

namespace net {

using json = nlohmann::json;

// Maior payload representável em 2 bytes.
constexpr std::size_t MAX_PAYLOAD = 0xFFFF;  // 65535

// Erro de protocolo ou de conexão (payload grande demais, JSON inválido,
// conexão encerrada no meio de uma mensagem, erro de send/recv).
class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Envia exatamente len bytes, repetindo send() até terminar (envio parcial).
// Repete em EINTR. Usa MSG_NOSIGNAL para não morrer com SIGPIPE.
// Retorna false se a conexão caiu ou houve erro.
bool send_all(int fd, const void* buf, std::size_t len);

// Lê exatamente len bytes, repetindo recv() até terminar (recebimento parcial).
// Repete em EINTR. Retorna false se o par fechou (recv == 0) ou houve erro.
bool recv_all(int fd, void* buf, std::size_t len);

// Serializa msg em JSON compacto (sem '\n'), monta cabeçalho big-endian de 2 bytes
// e envia cabeçalho + payload num único buffer (um único laço de send_all).
// Lança ProtocolError se o payload passar de 65535 bytes ou se o envio falhar.
//
// ATENÇÃO (concorrência): duas threads NÃO podem chamar send_msg no mesmo fd ao
// mesmo tempo, ou os bytes se intercalam. Proteja com um mutex por socket.
// Uma thread lendo e outra escrevendo no mesmo fd é seguro.
void send_msg(int fd, const json& msg);

// Lê uma mensagem: exatamente 2 bytes de cabeçalho, depois exatamente N bytes.
// Retorna std::nullopt se o par fechou a conexão de forma limpa ANTES de começar
// uma nova mensagem (EOF entre mensagens = fim normal).
// Lança ProtocolError se a conexão cair no meio da mensagem, se N == 0
// ou se o payload não for JSON válido.
std::optional<json> recv_msg(int fd);

// Encerramento gracioso: shutdown(SHUT_WR) (envia FIN), drena o que ainda chegar
// até o par também fechar (ou até timeout_ms), e então close().
// Gera no Wireshark a sequência FIN/ACK nos dois sentidos.
void graceful_close(int fd, int timeout_ms = 2000);

}  // namespace net
