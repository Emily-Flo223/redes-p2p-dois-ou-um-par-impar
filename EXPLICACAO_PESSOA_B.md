# Pessoa B: Commit-Reveal, barreira e Fase 1

Este documento explica o que foi feito na parte da Pessoa B (CONTRATO.md §5) e como auditar no Wireshark (item 3 do §8 do enunciado). Referências com § são do PDF, salvo quando dizem "CONTRATO".

## 0. Resumo

| Arquivo | O que é |
|---|---|
| `codigo/include/commit_reveal.hpp`, `codigo/src/commit_reveal.cpp` | `jogo::etapa_commit_reveal`: uma etapa completa de Commit-Reveal com barreira estrita. Serve para a Fase 1 e para a Fase 2 (Pessoa C). |
| `codigo/include/fase1.hpp`, `codigo/src/fase1.cpp` | `jogo::jogar_fase1`: "Dois ou Um" com repetição em empate; `jogo::decidir_fase1`: a regra pura, sem rede. |
| `codigo/include/entrada.hpp`, `codigo/src/entrada.cpp` | `jogo::ler_jogada_teclado`: lê a jogada com prazo `timeout_jogada` usando `poll()`; prazo esgotado = jogada aleatória. **No CONTRATO isso é da Pessoa C**; foi escrito aqui para a Fase 1 rodar. C pode trocar o corpo mantendo a assinatura. |
| `codigo/tests/test_fase1.cpp` | Testes sem rede: as 8 combinações da Fase 1, hash conhecido, fraude por jogada/salt, formato do hash, 1000 salts diferentes. Roda em `make test`. |
| `codigo/tests/teste_fase1.sh` | Integração em loopback: empate seguido de eliminação, fraude e stdin fechado. Confere a barreira pelos horários dos logs. |
| `codigo/tests/peer_trapaceiro.cpp` | Peer de teste que faz COMMIT da jogada 1 e REVEAL da jogada 2. Só para o teste de fraude; não faz parte do jogo. |
| `pessoa_b/integracao.patch` | Mudanças em `src/game_peer.cpp` (chama a Fase 1 no lugar do `TODO(B)`) e no `Makefile` (novos objetos e testes). **Não aplicado** na pasta compartilhada, porque são arquivos da Pessoa A. |

Aplicar a integração: `cd codigo && patch -p1 < ../pessoa_b/integracao.patch`.

**Testes (2026-10-08):** compila do zero com `-Wall -Wextra -pedantic` e 0 warnings; `make test` passa (framing + Fase 1); `tests/teste_fase1.sh` passou 15 vezes seguidas, 5 delas com ThreadSanitizer sem nenhum aviso; `tests/teste_malha.sh` da Pessoa A continua passando.

## 1. Commit-Reveal (`etapa_commit_reveal`)

Recebe a minha jogada, a lista dos **outros participantes ativos da etapa** e a faixa válida. Faz, sempre nesta ordem:

1. **Salt novo**: `crypto::make_salt()` (16 hex de `std::random_device`) a cada chamada, ou seja, a cada tentativa e a cada fase.
2. **Hash**: `H = SHA-256(to_string(V) + ":" + S)` com `crypto::commit_hash`. Aparece na tela para comparar com o Wireshark.
3. **MOVE_COMMIT** só com o hash (`proto::make_commit`), enviado com `mesh.broadcast` a todos os outros ativos.
4. **Barreira**: para cada outro ativo, `mesh.esperar(fase, tentativa, "MOVE_COMMIT", nome, prazo)` e o hash vai para o mapa `commits`. Hash fora do formato (64 hex minúsculos) é fraude. Só quando o laço termina, com o hash de **todos** guardado, o código segue. Log: `barreira cumprida: 2/2 MOVE_COMMIT recebidos`.
5. **MOVE_REVEAL** (`proto::make_reveal`, jogada + salt). É o único lugar do programa que envia REVEAL, e ele está depois do laço da barreira; não há caminho que pule o laço (um COMMIT que não chega lança exceção).
6. **Validação**: para cada REVEAL recebido, `crypto::verify_reveal(jogada, salt, commits[nome])` e checagem da faixa. Qualquer falha lança `FraudeError`.

Prazo: `mesh.prazo_padrao()` (maior timeout do ROSTER + 5 s) para o conjunto dos COMMIT e de novo para o conjunto dos REVEAL. Se alguém não manda ou a conexão cai, lança `TimeoutError` com a causa.

**Por que as tentativas não se misturam:** `mesh.esperar` só devolve a mensagem com a mesma `(fase, tentativa)`. Um COMMIT da tentativa 2 que chega enquanto ainda estou na tentativa 1 fica na fila até ser pedido. É o problema do exemplo em Python que o campo `tentativa` resolve (CONTRATO §3.5).

**Saída em fraude ou timeout** (CONTRATO §4): `game_peer` mostra `FRAUDE DETECTADA: ...` ou `partida abortada: ...`, chama `mesh.fechar_tudo()` (FIN/ACK em tudo) e sai com código 2 (fraude) ou 3 (timeout). Erros de rede da Pessoa A continuam saindo com 1.

## 2. Fase 1 (`jogar_fase1`)

- Participantes: os 3 (`mesh.outros()` + eu). Faixa: 1 ou 2.
- Laço de tentativas começando em 1. Em cada uma: lê a jogada, roda a etapa, aplica `decidir_fase1`.
- `decidir_fase1`: agrupa por valor. Um valor só = **empate** → `tentativa++` e repete. Dois valores = quem está sozinho num valor é **eliminado**.
- Devolve `ResultadoFase1`: `eliminado`, `finalistas` (em ordem crescente de nome, então `finalistas[0]` é PAR e `finalistas[1]` é ÍMPAR para a Pessoa C), `tentativas` e as jogadas da tentativa que decidiu.
- O eliminado só registra na tela que virou observador. Ficar conectado e receber o GAME_RESULT é da Pessoa C; nada é fechado pela Fase 1.

## 3. Para a Pessoa C (Fase 2)

```cpp
const auto& f = fase1.finalistas;           // [0] = PAR, [1] = ÍMPAR
if (mesh.nome() != fase1.eliminado) {
    const std::string adversario = (mesh.nome() == f[0]) ? f[1] : f[0];
    const int v = ler(0, 5, "Fase 2: escolha de 0 a 5");
    auto jogadas = jogo::etapa_commit_reveal(mesh, 2, 1, v, {adversario}, 0, 5);
    // jogadas[f[0]] e jogadas[f[1]] -> proto::make_game_result(...)
}
```
O observador não entra na etapa da Fase 2 (CONTRATO §4).

## 4. Wireshark, item 3 do §8 (parte da Pessoa B)

Filtro na interface `lo`: `tcp.port in {50001, 50002, 50003}`. Como o JSON passa depois de 2 bytes de cabeçalho, para filtrar por tipo: `tcp.payload contains "MOVE_COMMIT"` ou `tcp.payload contains "MOVE_REVEAL"`.

**Opacidade do COMMIT:** abrir um pacote MOVE_COMMIT, painel de bytes: só aparecem `fase`, `tentativa`, `jogador` e `hash` (64 hex). Não há `jogada` nem `salt`. Comparar o hash com a linha `H = SHA-256(...)` do terminal de quem enviou.

**Tabela cronológica da barreira:** filtro `tcp.payload contains "MOVE_COMMIT" or tcp.payload contains "MOVE_REVEAL"`, coluna Time em segundos desde o início. Montar uma tabela por tentativa:

| Time | Origem → Destino | Tipo | tentativa |
|---|---|---|---|
| ... | Alice → Bob | MOVE_COMMIT | 1 |
| ... | (6 COMMIT ao todo: cada um dos 3 para os outros 2) | | |
| ... | primeiro MOVE_REVEAL | | |

O que provar: para cada peer, o primeiro REVEAL que ele envia vem **depois** de ele ter recebido os 2 COMMIT dos outros naquela tentativa. Em loopback tudo acontece no mesmo milissegundo; para a captura ficar legível, usar timeout grande (ex.: 30) e digitar as jogadas com alguns segundos de intervalo: o REVEAL de quem digitou primeiro só sai depois que o último digitou.

**Empate:** jogar 1,1,1 na primeira tentativa. A captura mostra COMMIT/REVEAL com `"tentativa":1` e depois com `"tentativa":2`, sem mistura.

**Follow TCP Stream:** em qualquer das 3 conexões P2P aparece a sequência HELLO, COMMIT, REVEAL (e a repetição se houve empate). Os 2 bytes antes de cada `{` são o cabeçalho.

## 5. Decisões tomadas nesta parte

- **Escopo da entrada com prazo:** o CONTRATO dá a C; foi implementada aqui porque a Fase 1 precisa dela para rodar. C revisa ou substitui.
- **Códigos de saída:** 2 = fraude, 3 = timeout/conexão caída na partida (o CONTRATO só pede "≠ 0").
- **Fraude não é anunciada aos outros** por mensagem (não existe mensagem para isso no §7): quem detecta fecha tudo, e os outros veem a conexão cair e abortam com "conexão caiu".
