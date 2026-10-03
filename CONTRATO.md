# Contrato do protocolo — Dois ou Um + Par ou Ímpar P2P

Este documento é o acordo entre a Pessoa A (rede/servidor/framing) e a Pessoa B (lógica de jogo e cripto). Tudo que está aqui é obrigatório para o código das duas partes. Onde o enunciado é omisso, a decisão tomada está marcada com **[decisão]**.

Código base: `codigo/` (C++17, `g++ -std=c++17 -pthread`, Linux). Rodar `make && make test`.

## 1. Escolhas técnicas

| Item | Escolha | Motivo |
|---|---|---|
| Linguagem | C++17, g++, Linux Ubuntu/Debian | decidido no chat |
| JSON | nlohmann/json 3.11.3, header único em `codigo/third_party/json.hpp` **[decisão]** | permitido pelo §9 (só parsing JSON); não precisa instalar nada |
| SHA-256 | PicoSHA2 (MIT), header único em `codigo/third_party/picosha2.h` **[decisão]** | permitido pelo §9 (só hash); sem depender de `libssl-dev` na máquina do professor |
| Sockets | POSIX puro (`socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `close`) | §9 proíbe bibliotecas que encapsulem sockets |
| Threads | `std::thread`, `std::mutex`, `std::condition_variable` | biblioteca padrão |

## 2. Framing (§6) — já implementado em `codigo/src/framing.cpp`

```
+--------+--------+-----------------------------+
| N alto | N baixo|  N bytes de JSON UTF-8      |
+--------+--------+-----------------------------+
  2 bytes, big-endian (htons)     sem '\n' no fim
```

- `net::send_msg(fd, json)`: serializa com `dump()` compacto, monta cabeçalho + payload num buffer só e envia em laço (`send_all`). Lança `net::ProtocolError` se N > 65535.
- `net::recv_msg(fd)`: lê exatamente 2 bytes, depois exatamente N bytes, em laço (`recv_all`). Retorna `std::nullopt` se o par fechou entre mensagens; lança `ProtocolError` se fechou no meio, se N = 0 ou se o JSON é inválido.
- `net::graceful_close(fd)`: `shutdown(SHUT_WR)` → espera o FIN do outro lado (até 2 s) → `close()`.
- **Concorrência [decisão]:** cada socket P2P tem **um mutex de envio**. Nunca duas threads em `send_msg` no mesmo fd sem esse mutex. Leitura e escrita simultâneas no mesmo fd são seguras.
- Mensagens sempre montadas com as funções de `codigo/include/protocol.hpp` (`proto::make_*`) e conferidas com `proto::validate()` ao receber.

## 3. Mensagens e fluxo

Campos exatamente como no §7 do PDF. Inteiros são números JSON, não strings. Campos extras são ignorados por quem recebe.

### 3.1 REGISTER (Peer → Servidor)
```json
{"tipo":"REGISTER","nome":"Alice","ip_p2p":"127.0.0.1","porta_p2p":50001,"timeout":30}
```
- O peer **abre o listener P2P antes** de mandar o REGISTER **[decisão]**, para que ninguém tente conectar numa porta que ainda não escuta.
- `nome`: não vazio, único na partida. Recomendado ASCII sem espaços.

### 3.2 ROSTER (Servidor → cada Peer)
```json
{"tipo":"ROSTER","partida_id":"6f1c...-uuid","peers":[
  {"nome":"Alice","ip":"127.0.0.1","porta":50001,"timeout":30},
  {"nome":"Bob","ip":"127.0.0.1","porta":50002,"timeout":30},
  {"nome":"Carol","ip":"127.0.0.1","porta":50003,"timeout":30}]}
```
- Formato de cada item de `peers` igual ao exemplo Python do professor **[decisão]**. A lista inclui o próprio destinatário.
- `partida_id`: UUID v4 em texto, gerado pelo servidor.
- Servidor: aceita conexões até ter 3 REGISTER válidos; REGISTER inválido ou com nome repetido → fecha aquela conexão e continua esperando **[decisão]**. Depois de enviar o ROSTER aos 3, faz `graceful_close` em cada um, fecha o listener e **termina o processo**. Não participa de mais nada.

### 3.3 Malha K3 e HELLO (P2P)
```json
{"tipo":"HELLO","nome":"Alice","timeout":30}
```
- **Quem conecta em quem [decisão]:** para cada par, o nome menor (comparação de bytes, `std::string::operator<`) faz `connect()` no maior. Resultado: Alice→Bob, Alice→Carol, Bob→Carol = 3 conexões TCP, nenhuma duplicada.
- `connect()` tenta de novo a cada 200 ms enquanto o outro ainda não escuta.
- **Os dois lados mandam HELLO [decisão]:** quem conecta envia primeiro; quem aceita responde com o seu. Assim a captura mostra a "troca de HELLO" do §8.2.
- Quem aceita identifica o par pelo `nome` do HELLO e confere que ele está no ROSTER. Desconhecido → fecha.
- Cada conexão ganha **uma thread de recepção** que roda `recv_msg` em laço e entrega as mensagens numa fila protegida por mutex + `condition_variable`. A thread principal (jogo) nunca chama `recv` direto **[decisão]**.
- O timeout efetivo para esperar mensagens dos outros é `max(timeouts de todos) + 5 s` **[decisão]**.

### 3.4 MOVE_COMMIT (P2P)
```json
{"tipo":"MOVE_COMMIT","fase":1,"tentativa":1,"jogador":"Alice","hash":"<64 hex minúsculos>"}
```
### 3.5 MOVE_REVEAL (P2P)
```json
{"tipo":"MOVE_REVEAL","fase":1,"tentativa":1,"jogador":"Alice","jogada":2,"salt":"a1b2c3d4e5f60718"}
```
- `hash = SHA-256( to_string(jogada) + ":" + salt )`, hex minúsculo. Ex.: jogada 2, salt `a1b2c3d4e5f60718` → hash de `"2:a1b2c3d4e5f60718"`. Implementado em `crypto::commit_hash` / `crypto::verify_reveal`.
- `salt`: 16 caracteres hex minúsculos (8 bytes de `std::random_device`), **novo a cada commit**.
- **`tentativa` é extensão nossa [decisão]:** começa em 1 e sobe a cada empate na Fase 1 (na Fase 2 é sempre 1). Sem ela, um commit da tentativa 2 que chega cedo pode ser confundido com o da tentativa 1 (o exemplo Python do professor tem esse problema ao apagar mensagens após empate). Mensagens são guardadas por chave `(fase, tentativa, tipo, jogador)`.
- `jogador` deve ser igual ao nome do HELLO daquela conexão; senão é descartada.

### 3.6 GAME_RESULT (P2P, finalistas → observador)
```json
{"tipo":"GAME_RESULT","vencedor":"Bob","fase1_eliminado":"Carol",
 "fase2_detalhes":{"jogador_par":"Alice","jogador_impar":"Bob","jogada_par":3,"jogada_impar":4,"soma":7,"resultado":"IMPAR"}}
```
- Conteúdo de `fase2_detalhes` é **[decisão]** (o PDF mostra `{...}`). Montado por `proto::make_game_result`.
- **Os dois finalistas enviam** ao observador. O observador aceita o primeiro e, se o segundo chegar, confere que é igual (e avisa na tela se não for).

## 4. Regras do jogo (§4) e barreira (§5)

### Commit-Reveal, igual nas duas fases
1. Ler a jogada do teclado com prazo de `timeout_jogada` s (sem travar: `poll()` no stdin ou thread de entrada). Se esgotar o prazo, usar uma **jogada válida aleatória** e avisar na tela **[decisão]**.
2. Gerar salt, calcular hash, enviar MOVE_COMMIT a **todos os participantes ativos da etapa**.
3. **Barreira:** esperar até ter recebido e guardado o MOVE_COMMIT de **todos os outros ativos** da mesma `(fase, tentativa)`. Só então enviar MOVE_REVEAL. Nenhum caminho de código pode enviar REVEAL antes disso.
4. Esperar todos os MOVE_REVEAL, validar cada um com `verify_reveal` e checar a faixa da jogada. Hash errado ou jogada fora da faixa → **fraude**: mostrar na tela, encerrar todas as conexões e sair com código ≠ 0 **[decisão]**.
5. Timeout esperando commit ou reveal de alguém → mesma saída da fraude, com mensagem "timeout".

### Fase 1 — Dois ou Um (3 jogadores, jogada ∈ {1, 2})
- Os três iguais → empate, `tentativa++`, repete.
- Senão, quem ficou sozinho no valor minoritário é eliminado e vira **observador passivo** (continua conectado, não joga).

### Fase 2 — Par ou Ímpar (2 finalistas, jogada ∈ {0..5})
- Nome menor (mesma comparação de bytes do §3.3) é **PAR**; o outro é **ÍMPAR**.
- Commit/Reveal só entre os dois finalistas (o observador não é participante ativo desta etapa) **[decisão]**.
- Soma par → vence PAR; soma ímpar → vence ÍMPAR.

### Encerramento
- Finalistas enviam GAME_RESULT ao observador. Todos mostram o sumário (eliminado, PAR/ÍMPAR, jogadas, soma, vencedor).
- Todos fazem `net::graceful_close` em todas as conexões P2P e encerram o listener. Threads de recepção terminam quando `recv_msg` devolve `nullopt`.

## 5. Divisão do trabalho

| Pessoa | Código | Wireshark (§8) |
|---|---|---|
| **A** | `src/rendezvous_server.cpp`; parte de rede do `game_peer` (argumentos, escuta, malha K3, HELLO, threads de recepção); `framing` (pronto, testado) | itens 1 e 2: servidor central e formação da malha |
| **B** | Commit-Reveal e `crypto` (pronto, testado); barreira estrita; Fase 1 (empate e eliminação) | item 3: auditoria do Commit-Reveal e tabela da barreira |
| **C** | Fase 2 (PAR/ÍMPAR); observador; GAME_RESULT; encerramento com `close()`; entrada da jogada com timeout; Makefile e scripts | item 4: GAME_RESULT e FIN/ACK de todos os sockets |

`include/protocol.hpp` e `src/protocol.cpp` são compartilhados: qualquer mudança é combinada entre os três antes.

**Os três juntos:** relatório (cada um escreve a seção da sua parte, com revisão cruzada), roteiro do vídeo, testes e o vídeo (cada um fala pelo menos 3 minutos).

Sugestão para não brigarem no mesmo arquivo: A cria `src/mesh.cpp` com uma classe `Mesh` exposta por `include/mesh.hpp`; B cria `src/fase1.cpp` e C cria `src/fase2.cpp`, que só usam estas três operações da `Mesh`:

```cpp
void broadcast(const json& msg, const std::vector<std::string>& destinos); // envia aos nomes listados
std::optional<json> esperar(int fase, int tentativa, const std::string& tipo,
                            const std::string& jogador, std::chrono::seconds prazo); // nullopt = timeout
void fechar_tudo();                                                        // graceful_close em tudo
```

**Implementado (Pessoa A, 2026-10-01)** em `include/mesh.hpp` + `src/mesh.cpp`, namespace `net`. Diferenças e extras em relação à sugestão acima:
- `broadcast` e `enviar(destino, msg)` devolvem `bool` (false = conexão caída com algum destino).
- `esperar(...)` também devolve `nullopt` cedo se a conexão com `jogador` fechar antes de a mensagem chegar.
- Extras: `esperar_tipo(tipo, de, prazo)` (para GAME_RESULT, que não tem fase), `outros()` (os 2 outros nomes, em ordem crescente), `prazo_padrao()` (max dos timeouts + 5 s), `conectado(nome)`, `nome()`, `partida_id()`, `roster()`.
- `fechar_tudo()` não usa `graceful_close` (a thread de recepção já está lendo o socket): faz `shutdown(SHUT_WR)` em todas, espera o FIN de cada peer até 2 s, junta as threads e só então `close()`. Detalhes em `EXPLICACAO_PESSOA_A.md`.
- Ponto de extensão para B e C marcado com `TODO(B)` / `TODO(C)` em `src/game_peer.cpp`.

## 6. Teste local padrão (portas do guia do professor)

```
./rendezvous_server 45000
./game_peer 127.0.0.1 45000 127.0.0.1 50001 Alice 30
./game_peer 127.0.0.1 45000 127.0.0.1 50002 Bob 30
./game_peer 127.0.0.1 45000 127.0.0.1 50003 Carol 30
```
Filtro Wireshark (interface `lo`): `tcp.port in {45000, 50001, 50002, 50003}`
