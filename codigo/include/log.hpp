// log.hpp — Impressão na tela segura para várias threads, com horário em milissegundos.
// O horário (HH:MM:SS.mmm) ajuda a comparar a saída dos programas com a coluna Time
// do Wireshark durante a auditoria (Seção 8 do enunciado).
//
// Dono: Pessoa A. Pode ser usado por todos (B e C também).
#pragma once

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace util {

inline std::mutex& mutex_tela() {
    static std::mutex m;
    return m;
}

// Igual a vprintf, mas prefixa "HH:MM:SS.mmm [quem] " e termina a linha.
// O mutex impede que duas threads misturem letras de linhas diferentes na tela.
// (O '\n' aqui é só da tela; nada disso passa pela rede.)
inline void vlogf(const char* quem, const char* fmt, va_list ap) {
    using namespace std::chrono;
    const auto agora = system_clock::now();
    const std::time_t t = system_clock::to_time_t(agora);
    const long ms = static_cast<long>(duration_cast<milliseconds>(agora.time_since_epoch()).count() % 1000);
    std::tm tm{};
    localtime_r(&t, &tm);

    std::lock_guard<std::mutex> lk(mutex_tela());
    std::fprintf(stdout, "%02d:%02d:%02d.%03ld [%s] ", tm.tm_hour, tm.tm_min, tm.tm_sec, ms, quem);
    std::vfprintf(stdout, fmt, ap);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

// Igual a printf, com o mesmo prefixo de vlogf.
// O atributo format faz o compilador conferir os argumentos como confere os do printf.
__attribute__((format(printf, 2, 3))) inline void logf(const char* quem, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vlogf(quem, fmt, ap);
    va_end(ap);
}

}  // namespace util
