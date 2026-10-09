#!/usr/bin/env bash
# teste_malha.sh — Teste de integração da parte de rede (Pessoa A), em loopback.
# Sobe 1 servidor e 3 peers, espera todos terminarem e confere nos logs que:
#   - o servidor recebeu 3 REGISTER e enviou o ROSTER aos 3;
#   - cada peer formou a malha K3 (2 conexões com HELLO trocado).
# Uso (dentro de codigo/, depois de make):  bash tests/teste_malha.sh [porta_servidor]
set -u
cd "$(dirname "$0")/.."
PORTA=${1:-45000}
LOGS=$(mktemp -d)

./rendezvous_server "$PORTA" > "$LOGS/servidor.log" 2>&1 &
sleep 0.3
./game_peer 127.0.0.1 "$PORTA" 127.0.0.1 50001 Alice 30 > "$LOGS/Alice.log" 2>&1 &
./game_peer 127.0.0.1 "$PORTA" 127.0.0.1 50002 Bob 30   > "$LOGS/Bob.log"   2>&1 &
./game_peer 127.0.0.1 "$PORTA" 127.0.0.1 50003 Carol 30 > "$LOGS/Carol.log" 2>&1 &
wait

falhas=0
confere() {  # confere <arquivo> <texto> <quantidade esperada>
    local n
    n=$(grep -c -- "$2" "$LOGS/$1")
    if [ "$n" -ne "$3" ]; then
        echo "FALHOU: $1 tem $n linha(s) com '$2' (esperado $3)"
        falhas=$((falhas + 1))
    fi
}
confere servidor.log "REGISTER de" 3
confere servidor.log "ROSTER enviado" 3
for p in Alice Bob Carol; do
    confere "$p.log" "conexão com o servidor encerrada" 1
    confere "$p.log" "HELLO recebido de" 2
    confere "$p.log" "HELLO enviado a" 2
    confere "$p.log" "malha K3 completa" 1
    confere "$p.log" "fechada (close)" 2
done

if [ "$falhas" -eq 0 ]; then
    echo "OK: servidor + malha K3 funcionando (logs em $LOGS)"
else
    echo "$falhas verificação(ões) falharam; logs em $LOGS"
    exit 1
fi
