# Checklist de revisão final

Marcar cada item antes de enviar. Prazo: **26/10/2026**, Tarefa *2ou1eParÍmpar* no AVA. Entrega atrasada = **nota zero**.

Responsáveis: **A** = servidor, rede do peer, malha K3, HELLO, framing. **B** = Commit-Reveal, barreira, Fase 1. **C** = Fase 2, observador, GAME_RESULT, close(), entrada com timeout, Makefile/scripts. Testes, relatório, roteiro e vídeo são dos três juntos (ver CONTRATO.md §5).

## Nota zero (qualquer um zera o trabalho)
- [ ] Nenhum arquivo `.py` no zip enviado (nem o exemplo do professor, nem scripts auxiliares)
- [ ] Código autoral, sem plágio (o exemplo Python só serviu de referência)
- [ ] `make` compila do zero numa máquina Ubuntu/Debian limpa, só com `g++` e `make` (testar após `make clean`, de preferência numa VM ou container novo)
- [ ] Os dois programas executam e completam uma partida inteira
- [ ] Nenhuma biblioteca que encapsule sockets (só POSIX; bibliotecas externas apenas JSON e SHA-256)
- [ ] Servidor encerra depois do ROSTER e não media nenhum pacote do jogo
- [ ] Enviado antes do prazo

## Execução e parâmetros (§3) · responsável: A
- [ ] `./rendezvous_server <porta>` recebe só a porta
- [ ] `./game_peer <ip_srv> <porta_srv> <ip_p2p> <porta_p2p> <nome> <timeout>` com exatamente 6 parâmetros
- [ ] Portas validadas em 1–65535; timeout validado em 5–300 s; mensagem de uso clara quando errado

## Arquitetura e conexão — 1,5 pt · responsável: A
- [ ] Servidor junta 3 REGISTER e envia ROSTER aos 3
- [ ] Conexões com o servidor fechadas (FIN/ACK) antes do jogo
- [ ] Exatamente 3 conexões TCP P2P (malha K3), sem conexões duplicadas
- [ ] HELLO trocado nas 3 conexões

## Empacotamento de 2 bytes — 1,5 pt (desconto de até 1,5) · responsável: A
- [ ] Cabeçalho de exatamente 2 bytes big-endian (`htons` / shift manual)
- [ ] Nenhum `'\n'` como delimitador em lugar nenhum do protocolo
- [ ] `send` e `recv` em laço tratando I/O parcial (`send_all` / `recv_all`)
- [ ] Payload JSON UTF-8, limite de 65535 bytes respeitado
- [ ] `make test` passa

## Integridade criptográfica — 2,0 pt (desconto de até 2,0) · responsável: B
- [ ] Salt de 16 hex, novo a cada jogada
- [ ] `H = SHA-256(V + ":" + S)` e MOVE_COMMIT contém só o hash, não a jogada
- [ ] **Barreira:** nenhum MOVE_REVEAL sai antes de todos os MOVE_COMMIT dos ativos da etapa terem chegado (rever o código E conferir na captura)
- [ ] Cada REVEAL recebido é validado contra o hash; fraude é detectada e informada
- [ ] Após empate na Fase 1, commits da nova tentativa não se misturam com os antigos (campo `tentativa`)

## Regras da rodada única — 1,5 pt (desconto de até 1,0 nas regras, até 1,0 no close) · responsáveis: B (Fase 1) e C (Fase 2, GAME_RESULT, close)
- [ ] Fase 1: três iguais = empate e repete; minoria única é eliminada
- [ ] Eliminado continua conectado como observador passivo
- [ ] Fase 2: nome menor em ordem lexicográfica é PAR; jogadas 0–5; soma par vence PAR
- [ ] Testado: Alice/Bob/Carol com vários resultados, inclusive pelo menos um empate na Fase 1
- [ ] Finalistas enviam GAME_RESULT ao observador
- [ ] Todos mostram o sumário final
- [ ] Todos os sockets fechados com `close()` (FIN/ACK em todas as conexões na captura)

## Concorrência e bloqueio (desconto de até 1,5) · responsáveis: A (threads de rede) e C (entrada com timeout)
- [ ] Uma thread de recepção por conexão P2P; `accept` em thread própria
- [ ] Leitura do teclado com prazo `timeout_jogada` sem travar o programa
- [ ] Mutex de envio por socket; estruturas compartilhadas protegidas
- [ ] Nenhum `recv`/`accept`/`connect` bloqueante travando a interface ou o jogo

## Relatório técnico (.pdf, até 8 páginas) — 2,5 pt (desconto de até 2,5) · os três (cada um a seção da sua parte, revisão cruzada)
- [ ] Nome completo de todos os integrantes
- [ ] Descrição da arquitetura
- [ ] Diagrama de threads
- [ ] Relato das dificuldades
- [ ] **Seção Wireshark** com capturas legíveis:
  - [ ] (A) Three-way handshake com o servidor (porta 45000)
  - [ ] (A) 2 primeiros bytes do REGISTER destacados no painel hex, com a conversão para decimal batendo com o tamanho do JSON
  - [ ] (A) FIN/ACK do fechamento com o servidor
  - [ ] (A) Abertura das 3 conexões P2P e troca de HELLO
  - [ ] (B) Opacidade do MOVE_COMMIT (só o hash de 64 hex)
  - [ ] (B) **Tabela cronológica** (coluna Time) provando que nenhum REVEAL saiu antes de todos os COMMIT
  - [ ] (C) GAME_RESULT chegando ao observador
  - [ ] (C) FIN/ACK em todas as conexões P2P
  - [ ] (todos, cada um nas suas conexões) Reconstrução de sessões com **Follow TCP Stream**
- [ ] No máximo 8 páginas

## Vídeo (até 10 min) — parte de 1,0 pt; desconto de 1,0 por aluno ausente · os três
- [ ] Mostra compilação (`make`)
- [ ] Mostra execução simultânea dos 4 processos
- [ ] Mostra captura no Wireshark em tempo real
- [ ] **Cada integrante aparece com nome e rosto, falando por pelo menos 3 minutos**
- [ ] Duração total ≤ 10 minutos

## Pacote final · C (Makefile e scripts), revisão dos três
- [ ] Código-fonte completo + Makefile + `third_party/` (json.hpp e picosha2.h com licenças)
- [ ] Script ou instruções de compilação e execução para Linux
- [ ] Relatório .pdf
- [ ] Vídeo (ou link) conforme pedido no AVA
