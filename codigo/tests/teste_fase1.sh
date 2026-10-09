#!/usr/bin/env bash
# teste_fase1.sh — Teste de integração da Pessoa B (Commit-Reveal, barreira, Fase 1), em loopback.
# Três cenários, cada um com 1 servidor + 3 peers:
#   1. empate na tentativa 1 e eliminação na tentativa 2 (jogadas vindas do stdin);
#   2. Carol trapaceia (revela jogada diferente da do COMMIT): Alice e Bob acusam FRAUDE;
#   3. stdin fechado: todos usam jogada aleatória e a Fase 1 termina mesmo assim.
# Em todos confere, pelos horários dos logs, que nenhum MOVE_REVEAL saiu antes de
# todos os MOVE_COMMIT daquela tentativa (barreira).
# Uso (dentro de codigo/, depois de make):  bash tests/teste_fase1.sh [porta_servidor]
set -u
cd "$(dirname "$0")/.."
make -s game_peer rendezvous_server build/peer_trapaceiro || exit 1
PORTA=${1:-45000}
LOGS=$(mktemp -d)
falhas=0

falha() { echo "FALHOU ($cenario): $*"; falhas=$((falhas + 1)); }

confere() {  # confere <arquivo> <texto> <quantidade esperada>
    local n
    n=$(grep -c -- "$2" "$DIR/$1")
    [ "$n" -eq "$3" ] || falha "$1 tem $n linha(s) com '$2' (esperado $3)"
}

confere_saida() {  # confere_saida <nome> <código esperado>
    local c
    c=$(cat "$DIR/$1.rc")
    [ "$c" -eq "$2" ] || falha "$1 saiu com código $c (esperado $2)"
}

# Barreira: em cada tentativa, o primeiro "MOVE_REVEAL enviado" de qualquer peer tem de vir
# depois do último "MOVE_COMMIT enviado" de todos. Horário HH:MM:SS.mmm compara como texto.
confere_barreira() {
    local t ultimo_commit primeiro_reveal
    for t in $(grep -ho "MOVE_COMMIT enviado (fase 1, tentativa [0-9]*" "$DIR"/*.log | grep -o "[0-9]*$" | sort -u); do
        ultimo_commit=$(grep -h "MOVE_COMMIT enviado (fase 1, tentativa $t)" "$DIR"/*.log | cut -c1-12 | sort | tail -1)
        primeiro_reveal=$(grep -h "MOVE_REVEAL enviado (fase 1, tentativa $t)" "$DIR"/*.log | cut -c1-12 | sort | head -1)
        [ -z "$primeiro_reveal" ] && continue
        [[ "$primeiro_reveal" < "$ultimo_commit" ]] &&
            falha "tentativa $t: REVEAL às $primeiro_reveal antes do último COMMIT às $ultimo_commit"
    done
}

# rodar <cenário> <programa da Carol> <entrada Alice> <entrada Bob> <entrada Carol>
# Entrada "-" = stdin fechado (/dev/null).
rodar() {
    cenario=$1
    DIR="$LOGS/$1"
    mkdir -p "$DIR"
    ./rendezvous_server "$PORTA" > "$DIR/servidor.log" 2>&1 &
    sleep 0.3
    local nomes=(Alice Bob Carol) portas=(50001 50002 50003) entradas=("$3" "$4" "$5") i prog
    for i in 0 1 2; do
        prog=./game_peer
        [ "${nomes[$i]}" = Carol ] && prog=$2
        (
            if [ "${entradas[$i]}" = "-" ]; then exec < /dev/null; else exec < <(printf '%b' "${entradas[$i]}"); fi
            "$prog" 127.0.0.1 "$PORTA" 127.0.0.1 "${portas[$i]}" "${nomes[$i]}" 5 > "$DIR/${nomes[$i]}.log" 2>&1
            echo $? > "$DIR/${nomes[$i]}.rc"
        ) &
    done
    wait
}

# ---------- 1. Empate e depois eliminação ----------
rodar empate ./game_peer '1\n1\n' '1\n2\n' '1\n2\n'
for p in Alice Bob Carol; do
    confere_saida $p 0
    confere $p.log "EMPATE" 1
    confere $p.log "barreira cumprida: 2/2" 2
    confere $p.log "MOVE_REVEAL de .* conferido" 4
    confere $p.log "Alice ficou sozinho(a) com 1 e foi ELIMINADO(A); finalistas Bob e Carol" 1
done
confere Alice.log "fui eliminado" 1
confere_barreira

# ---------- 2. Fraude ----------
rodar fraude ./build/peer_trapaceiro '1\n' '1\n' '-'
for p in Alice Bob; do
    confere_saida $p 2
    confere $p.log "FRAUDE DETECTADA: Carol revelou jogada 2" 1
    confere $p.log "fechada (close)" 2
done
confere_barreira

# ---------- 3. Sem entrada: jogada aleatória ----------
rodar aleatorio ./game_peer - - -
for p in Alice Bob Carol; do
    confere_saida $p 0
    confere $p.log "usando jogada aleatória" "$(grep -c 'barreira cumprida' "$DIR/$p.log")"
    confere $p.log "fase 1 decidida" 1
done
confere_barreira

if [ "$falhas" -eq 0 ]; then
    echo "OK: Commit-Reveal, barreira e Fase 1 funcionando (logs em $LOGS)"
else
    echo "$falhas verificação(ões) falharam; logs em $LOGS"
    exit 1
fi
