// framing.cpp — Implementação do empacotamento com cabeçalho de 2 bytes big-endian.
#include "framing.hpp"

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <vector>

namespace net {

bool send_all(int fd, const void* buf, std::size_t len) {
    const auto* p = static_cast<const std::uint8_t*>(buf);
    std::size_t enviados = 0;
    while (enviados < len) {
        ssize_t n = ::send(fd, p + enviados, len - enviados, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        enviados += static_cast<std::size_t>(n);
    }
    return true;
}

bool recv_all(int fd, void* buf, std::size_t len) {
    auto* p = static_cast<std::uint8_t*>(buf);
    std::size_t recebidos = 0;
    while (recebidos < len) {
        ssize_t n = ::recv(fd, p + recebidos, len - recebidos, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;  // par encerrou (FIN)
        recebidos += static_cast<std::size_t>(n);
    }
    return true;
}

void send_msg(int fd, const json& msg) {
    const std::string payload = msg.dump();  // compacto, UTF-8, sem '\n'
    if (payload.size() > MAX_PAYLOAD) {
        throw ProtocolError("payload excede 65535 bytes (" + std::to_string(payload.size()) + ")");
    }
    const std::uint16_t tamanho_rede = htons(static_cast<std::uint16_t>(payload.size()));

    std::vector<std::uint8_t> pacote(2 + payload.size());
    std::memcpy(pacote.data(), &tamanho_rede, 2);
    std::memcpy(pacote.data() + 2, payload.data(), payload.size());

    if (!send_all(fd, pacote.data(), pacote.size())) {
        throw ProtocolError(std::string("falha no envio: ") + std::strerror(errno));
    }
}

std::optional<json> recv_msg(int fd) {
    std::uint8_t cabecalho[2];

    // Primeiro byte separado para distinguir "EOF limpo entre mensagens" de
    // "conexão caiu no meio do cabeçalho".
    ssize_t n;
    do {
        n = ::recv(fd, cabecalho, 1, 0);
    } while (n < 0 && errno == EINTR);
    if (n == 0) return std::nullopt;
    if (n < 0) throw ProtocolError(std::string("falha no recv: ") + std::strerror(errno));
    if (!recv_all(fd, cabecalho + 1, 1)) throw ProtocolError("conexão encerrada no meio do cabeçalho");

    // Big-endian: byte mais significativo primeiro.
    const std::size_t tamanho = (static_cast<std::size_t>(cabecalho[0]) << 8) | cabecalho[1];
    if (tamanho == 0) throw ProtocolError("payload de tamanho 0");

    std::string payload(tamanho, '\0');
    if (!recv_all(fd, payload.data(), tamanho)) {
        throw ProtocolError("conexão encerrada no meio do payload");
    }

    try {
        return json::parse(payload);
    } catch (const json::parse_error& e) {
        throw ProtocolError(std::string("JSON inválido: ") + e.what());
    }
}

void graceful_close(int fd, int timeout_ms) {
    if (fd < 0) return;
    ::shutdown(fd, SHUT_WR);  // envia FIN: "não vou mais escrever"
    pollfd pfd{fd, POLLIN, 0};
    char lixo[512];
    while (::poll(&pfd, 1, timeout_ms) > 0) {
        ssize_t n = ::recv(fd, lixo, sizeof(lixo), 0);
        if (n <= 0) break;  // 0 = FIN do par chegou
    }
    ::close(fd);
}

}  // namespace net
