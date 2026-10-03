# Pessoa A: servidor de encontro e malha P2P, passo a passo

Este documento explica **o que foi feito e por quê** na parte da Pessoa A (CONTRATO.md §5): o `rendezvous_server`, a parte de rede do `game_peer` (argumentos, escuta P2P, registro, malha K3, HELLO, threads de recepção, encerramento) e como auditar isso no Wireshark (itens 1 e 2 do §8 do enunciado).

Todas as referências a seções (§) são do PDF do trabalho, salvo quando diz "CONTRATO".

---

## 0. Resumo rápido

| Arquivo | O que é | Situação |
|---|---|---|
| `codigo/src/rendezvous_server.cpp` | Servidor central completo | novo (substituiu o esqueleto) |
| `codigo/include/mesh.hpp`, `codigo/src/mesh.cpp` | Classe `net::Mesh`: escuta, registro, malha K3, HELLO, recepção, envio, fechamento | novos |
| `codigo/src/game_peer.cpp` | `main` do jogador: valida os 6 argumentos e usa a `Mesh`; ponto de extensão `TODO(B)` / `TODO(C)` | reescrito |
| `codigo/include/log.hpp` | `util::logf`: impressão com horário em ms, segura entre threads | novo |
| `codigo/tests/teste_malha.sh` | Teste de integração (1 servidor + 3 peers em loopback) | novo |
| `codigo/Makefile` | Uma linha: `game_peer` agora também liga `build/mesh.o` | alterado |
| `CONTRATO.md` §5 | Nota com a API real da `Mesh` | complementado |

Não foi mexido: `framing.*`, `protocol.*`, `crypto.*`, `third_party/` (já prontos e testados).

**Resultado dos testes (2026-10-01):** compila do zero com `-Wall -Wextra -pedantic` e **0 warnings**; `make test` passa; `tests/teste_malha.sh` passa; 15 execuções seguidas com ThreadSanitizer, 15/15 com a malha completa e **nenhuma condição de corrida**; captura com tcpdump mostrou 3 conexões ao servidor, **exatamente 3 conexões P2P** (sem duplicadas), 6 HELLO, 12 FIN (2 por conexão) e 0 RST.

---

## 1. Regras do enunciado que guiaram cada escolha

| Regra (§) | Como foi atendida |
|---|---|
| Linguagem ≠ Python (§1, §9, §12.1) | Tudo em C++17. O teste automatizado é um script **bash**, não Python. |
| Sockets TCP puros (§1, §9, §12.1) | Só chamadas POSIX: `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`, `shutdown`, `close`, `poll`, `setsockopt`. Nenhuma biblioteca que encapsule sockets. |
| Bibliotecas externas só para JSON e SHA-256 (§9) | Só `third_party/json.hpp` (nlohmann) e `picosha2.h`. Threads, mutex e condition_variable são da biblioteca padrão do C++. |
| Sem HTTP/WebSocket/RPC/mensageria (§9) | O protocolo é o nosso: cabeçalho de 2 bytes + JSON. |
| Sem `'\n'` como delimitador (§6, §9) | Todas as mensagens passam por `net::send_msg`/`net::recv_msg` (cabeçalho de 2 bytes big-endian). O único `'\n'` do código novo é o fim de linha **da tela** em `log.hpp`, que nunca vai para a rede. |
| Gerenciamento explícito de threads e chamadas bloqueantes (§1, §12.2) | Uma thread por cliente no servidor; no peer, threads separadas para accept, cada connect e cada recepção. `poll()` com prazo antes de `accept()`; `SO_RCVTIMEO` durante o REGISTER e o HELLO. |
| Servidor só para registro, nunca media o jogo (§2, §9, §12.1) | Depois de mandar o ROSTER, o servidor fecha tudo e **termina o processo**. |
| 6 parâmetros, portas 1..65535, timeout 5..300 (§3.2) | Validação em `game_peer.cpp` com `strtol` e mensagem de uso. |
| Malha completa K3 (§2) | Regra "nome menor conecta no maior" → 3 conexões, nenhuma duplicada. |
| Encerramento com `close()` (§4, §12.2) | `shutdown(SHUT_WR)` → espera o FIN do outro lado → `close()`. Gera FIN/ACK nos dois sentidos. |

---

## 2. Servidor central (`src/rendezvous_server.cpp`)

### Passo 2.1: validar a porta
`./rendezvous_server <porta>`. A porta é lida com `parse_int` (que usa `strtol`), recusando texto (`45a`), vazio, estouro e valores fora de 1..65535.
**Por quê:** `atoi("abc")` devolve 0 sem avisar; com `strtol` dá para saber se sobrou lixo depois do número.

### Passo 2.2: socket de escuta
`socket(AF_INET, SOCK_STREAM)` → `setsockopt(SO_REUSEADDR)` → `bind(0.0.0.0:porta)` → `listen`.
- `SOCK_STREAM` = TCP, como o enunciado exige.
- `SO_REUSEADDR` deixa reabrir a mesma porta logo depois de um teste (senão o `bind` falha por alguns segundos por causa do estado TIME_WAIT). Útil na hora de gravar o vídeo várias vezes.
- `0.0.0.0` (`INADDR_ANY`) aceita conexões em qualquer interface, então funciona tanto em loopback quanto entre máquinas.
- `htons(porta)`: a porta vai para a estrutura em ordem de rede (big-endian).

### Passo 2.3: uma thread por cliente
A thread principal fica num laço: `poll(listener, 200 ms)` → se há conexão pronta, `accept()` → cria `std::thread(atender_cliente, ...)`.
- **Por que uma thread por cliente:** ler o REGISTER é um `recv` bloqueante. Se fosse na thread principal, um cliente lento (ou que conectou e não mandou nada) travaria o servidor para todos. Com uma thread por cliente, o accept continua livre.
- **Por que `poll` de 200 ms antes do `accept`:** o `accept()` sozinho bloqueia para sempre. Com `poll`, a cada volta a thread principal confere se já temos os 3 jogadores e sai do laço.
- **Prazo de 15 s para o REGISTER** (`SO_RCVTIMEO` em cada cliente): um cliente parado não segura a thread para sempre.

### Passo 2.4: validar o REGISTER (`atender_cliente` e `motivo_recusa`)
1. `net::recv_msg` lê exatamente 2 bytes de cabeçalho e N bytes de JSON.
2. `proto::validate(msg, "REGISTER")` confere os campos e os tipos (`nome`, `ip_p2p`, `porta_p2p`, `timeout`).
3. `motivo_recusa` confere os valores: nome 1..64 caracteres, `ip_p2p` IPv4 válido, porta 1..65535, timeout 5..300, **nome não repetido**, `ip:porta` não repetido, partida ainda não cheia.
4. Recusado → fecha só aquela conexão e o servidor continua esperando (decisão do CONTRATO §3.2). O peer recusado vê "servidor fechou sem enviar ROSTER".

Tudo que é compartilhado entre threads (lista de jogadores, sockets, conjunto de pendentes, flag `cheio`) fica na `struct Registro`, protegida por **um mutex**. Assim duas threads nunca aceitam o "3º jogador" ao mesmo tempo.

### Passo 2.5: quórum, ROSTER e saída
Quando há 3 jogadores:
1. Fecha o listener (ninguém mais entra) e faz `shutdown` nos clientes extras que ainda estejam no meio de um REGISTER, para suas threads terminarem; depois `join` em todas as threads.
2. Gera `partida_id` como **UUID v4** (`gerar_uuid_v4`): 16 bytes de `std::random_device`, com os bits de versão (4) e variante fixados como manda a RFC 4122. O enunciado mostra `"uuid-xyz"`, e o exemplo do professor usa `uuid4`.
3. Envia **o mesmo** ROSTER aos 3 (`proto::make_roster`), com `nome`, `ip`, `porta`, `timeout` de cada um (formato do exemplo do professor).
4. `net::graceful_close` em cada cliente: `shutdown(SHUT_WR)` (sai o FIN), espera o FIN do peer e só então `close()`. É o FIN/ACK que o item 1 do Wireshark pede.
5. `return 0`: **o processo do servidor termina.** Não há como ele mediar o jogo, porque ele não existe mais (§12.1).

---

## 3. Nó jogador, parte de rede (`src/game_peer.cpp` + `net::Mesh`)

### Passo 3.1: validar os 6 argumentos (`game_peer.cpp`)
`./game_peer <ip_servidor> <porta_servidor> <ip_escuta_p2p> <porta_escuta_p2p> <nome> <timeout_jogada>`
- Exatamente 6 (`argc == 7`), senão mostra o uso.
- IPs com `inet_pton(AF_INET, ...)`: só IPv4 válido, como diz a tabela do §3.2.
- Portas 1..65535 e timeout 5..300 com `strtol` (mesmo motivo do servidor).
- Nome: 1..64 caracteres visíveis, **sem espaços**. É usado no desempate PAR/ÍMPAR e como identificador em todas as mensagens; espaços atrapalhariam na tela e no Wireshark.
Cada erro mostra o motivo + o uso, e sai com código 1.

### Passo 3.2: abrir a escuta P2P **antes** do REGISTER (`Mesh::abrir_escuta`)
`socket` → `SO_REUSEADDR` → `bind(ip_escuta_p2p:porta_escuta_p2p)` → `listen`.
**Por quê antes:** assim que o servidor mandar o ROSTER, os outros peers podem tentar conectar. Se eu só abrisse a porta depois, eles levariam "connection refused". E se a porta já estiver ocupada, o peer descobre isso **antes** de se registrar, sem estragar a partida dos outros (decisão do CONTRATO §3.1).

### Passo 3.3: registrar no servidor (`Mesh::registrar`)
1. `connect` ao servidor. Se o servidor ainda não subiu, tenta de novo a cada 500 ms por até 10 s (testado: peers iniciados 1 s antes do servidor funcionam).
2. Envia o REGISTER com `proto::make_register` e mostra na tela o cabeçalho em hex, por exemplo `REGISTER enviado: cabeçalho 0x00 0x54 = 84 bytes de JSON`. **Isso é o que vocês vão comparar com o painel hex do Wireshark.**
3. Espera o ROSTER (bloqueia aqui até os 3 se registrarem, que é o comportamento esperado: não há nada para fazer antes).
4. `validar_roster`: exatamente 3 peers, nomes distintos, portas válidas e **eu estou na lista**.
5. `graceful_close` com o servidor (FIN/ACK).

### Passo 3.4: formar a malha K3 sem conexões duplicadas (`Mesh::formar_malha`)
**Regra:** em cada par de peers, o de **nome menor** (`std::string::operator<`, comparação byte a byte) faz `connect()` no de **nome maior**.

| Peer | Conecta em | Aceita de |
|---|---|---|
| Alice | Bob, Carol | ninguém |
| Bob | Carol | Alice |
| Carol | ninguém | Alice, Bob |

São 3 pares e cada par tem **uma** conexão TCP: Alice→Bob, Alice→Carol, Bob→Carol. Juntas formam o triângulo K3.
**Por que essa regra:** se os dois lados de um par conectassem um no outro, teríamos 6 conexões (2 por par) e ambiguidade sobre qual usar. A regra é determinística e todos chegam à mesma conclusão só olhando o ROSTER, sem precisar combinar nada pela rede. É a mesma ideia do exemplo do professor.

**Abertura concorrente (§8.2):** dentro de `formar_malha` são criadas
- 1 thread de accept (`thread_accept`), se eu tiver alguém de nome menor;
- 1 thread de connect por peer de nome maior (`thread_connect`).

Todas rodam ao mesmo tempo, e a thread principal dorme numa `condition_variable` até as 2 conexões estarem prontas (ou dar erro/prazo de 30 s). Na captura, os SYNs da Alice para Bob e para Carol saem com microssegundos de diferença.

- `thread_connect` tenta `connect()` de novo a cada 200 ms se o outro ainda não está escutando (CONTRATO §3.3).
- `thread_accept` usa `poll` de 200 ms antes de `accept()`, para nunca ficar presa e poder desistir se o prazo acabar.
- Quando a malha fica completa, **o listener P2P é fechado**: ninguém mais precisa conectar, e assim nenhuma conexão estranha entra no meio do jogo.

### Passo 3.5: troca de HELLO nos dois sentidos
- **Quem conecta** manda o HELLO primeiro (`{"tipo":"HELLO","nome":"Alice","timeout":30}`) e espera a resposta.
- **Quem aceita** lê o HELLO, confere que o nome **está no ROSTER**, que **é menor que o meu** (senão quem deveria conectar era eu) e que **ainda não existe conexão** com ele. Só então responde com o próprio HELLO.
- Prazo de 5 s para o HELLO chegar (`SO_RCVTIMEO`), depois o prazo é removido.

**Por que nos dois sentidos:** quem aceita uma conexão não sabe quem está do outro lado (só vê IP e uma porta efêmera); o HELLO identifica o peer. E quem conectou recebe a confirmação de que falou com o peer certo. Na captura aparecem 6 HELLO (2 por conexão), que é a "troca de mensagens HELLO" pedida no §8.2.

### Passo 3.6: uma thread de recepção por conexão (`Mesh::thread_recepcao`)
Assim que a conexão está identificada, `adicionar_conexao` cria uma thread que roda `recv_msg` em laço e:
1. confere que a mensagem tem `tipo` conhecido e passa em `proto::validate`;
2. em MOVE_COMMIT/MOVE_REVEAL, confere que `jogador` é o mesmo nome do HELLO daquela conexão (ninguém joga "em nome" de outro, CONTRATO §3.5);
3. coloca a mensagem numa **fila protegida por mutex** e acorda quem está esperando (`notify_all`).

**Por quê:** o jogo (B e C) nunca chama `recv` diretamente, então nunca trava esperando um socket específico. As mensagens podem chegar em qualquer ordem (por exemplo, o commit da Carol antes do da Bob) e ficam guardadas até alguém pedir. Isso é essencial para a barreira do Commit-Reveal da Pessoa B.

### Passo 3.7: envio seguro entre threads (`Mesh::enviar` / `broadcast`)
Cada conexão tem o seu `mutex_envio` (CONTRATO §2). Se duas threads mandassem mensagens no mesmo socket ao mesmo tempo, os bytes de uma poderiam se misturar com os da outra e o cabeçalho de 2 bytes deixaria de bater. Com o mutex, cada `send_msg` vai inteiro. Ler e escrever no mesmo socket ao mesmo tempo (thread de recepção + envio) é seguro no TCP.

### Passo 3.8: esperar mensagens sem travar (`Mesh::esperar` / `esperar_tipo`)
`esperar(fase, tentativa, tipo, jogador, prazo)` procura na fila; se não achar, **dorme** na `condition_variable` até chegar algo novo ou o prazo acabar (sem laço girando à toa). Devolve `std::nullopt` se:
- o prazo acabou, ou
- a conexão com aquele jogador fechou (não adianta esperar mais).

`prazo_padrao()` = maior timeout do ROSTER + 5 s (CONTRATO §3.3).

### Passo 3.9: encerramento limpo (`Mesh::fechar_tudo`)
1. `shutdown(SHUT_WR)` em **todas** as conexões → sai um FIN para cada peer ("não vou mais escrever").
2. Cada thread de recepção recebe o FIN do outro lado (`recv` devolve 0) e termina.
3. Se algum peer não fechar em 2 s, `shutdown(SHUT_RD)` acorda o `recv` daquela thread.
4. `join` em cada thread e **só então** `close()`.

**Por que não `graceful_close` aqui:** a thread de recepção já está lendo o socket. Se `fechar_tudo` também lesse, as duas disputariam os bytes. **Por que `close` só depois do `join`:** se o número do socket fosse liberado antes, o sistema poderia reaproveitá-lo para outro arquivo enquanto a thread ainda o usa.
O destrutor de `Mesh` chama `fechar_tudo()`, então mesmo em caso de erro (exceção) as conexões são fechadas.

### Diagrama de threads (útil para o relatório)

```
rendezvous_server
  principal: poll+accept ──► cria ──► [cliente 1] recv REGISTER ─┐
                                     [cliente 2] recv REGISTER ─┼─► Registro (mutex)
                                     [cliente 3] recv REGISTER ─┘
  principal: (3 ok) envia ROSTER x3 → graceful_close x3 → termina

game_peer (ex.: Bob)
  principal: abrir_escuta → registrar (REGISTER/ROSTER) → formar_malha ──► espera cv
                 ├─► [accept]  aceita Alice, troca HELLO ──► cria [recepção Alice]
                 └─► [connect] conecta Carol, troca HELLO ──► cria [recepção Carol]
  principal: jogo (B/C) usa broadcast/esperar  ◄── fila (mutex+cv) ◄── [recepção *]
  principal: fechar_tudo → FIN → join [recepção *] → close
```

---

## 4. Pontos de extensão para B e C

Em `src/game_peer.cpp`, depois de `mesh.formar_malha()`, há um bloco marcado `TODO(B)` / `TODO(C)`. Lá a rede está pronta e o servidor já saiu. B e C usam só:

```cpp
mesh.outros();                                         // {"Bob", "Carol"} (sem mim, em ordem)
mesh.broadcast(proto::make_commit(1, t, eu, h), destinos);
auto m = mesh.esperar(1, t, proto::MOVE_COMMIT, "Bob", mesh.prazo_padrao());
auto r = mesh.esperar_tipo(proto::GAME_RESULT, "Alice", mesh.prazo_padrao());
mesh.fechar_tudo();                                    // já chamado no fim do main
```

Testado (num build temporário, fora do projeto): os 3 peers trocaram um MOVE_COMMIT de teste e cada um recebeu os 2 commits dos outros; `esperar` com tentativa inexistente devolveu `nullopt` após 1 s, como esperado. Tudo sob ThreadSanitizer, sem alertas.

Hoje, sem as fases, cada peer forma a malha, escreve "rede pronta ... fases do jogo ainda não implementadas" e fecha tudo.

---

## 5. Como compilar e rodar

Num Ubuntu/Debian só com `g++` e `make`:

```bash
cd codigo
make                 # gera ./rendezvous_server e ./game_peer
make test            # testes de framing/protocolo/cripto
bash tests/teste_malha.sh   # 1 servidor + 3 peers em loopback, confere os logs
```

Manual, em 4 terminais (portas do guia do professor):

```bash
./rendezvous_server 45000
./game_peer 127.0.0.1 45000 127.0.0.1 50001 Alice 30
./game_peer 127.0.0.1 45000 127.0.0.1 50002 Bob 30
./game_peer 127.0.0.1 45000 127.0.0.1 50003 Carol 30
```

Cada linha da saída tem horário com milissegundos (`15:27:42.902 [Bob] ...`), para comparar com a coluna Time do Wireshark.

---

## 6. Wireshark: itens 1 e 2 do roteiro (§8)

### Preparar a captura
1. Abrir o Wireshark (`sudo wireshark`, ou com o usuário no grupo `wireshark`).
2. Interface **Loopback: lo** (tudo roda em 127.0.0.1).
3. Filtro de exibição geral: `tcp.port in {45000 50001 50002 50003}`
4. Iniciar a captura **antes** do servidor; depois rodar servidor e peers.
   Alternativa sem interface gráfica: `sudo tcpdump -i lo -w partida.pcap 'tcp port 45000 or portrange 50001-50003'` e abrir o arquivo no Wireshark.
5. Dica: *View → Time Display Format → Time of Day* deixa a coluna Time no mesmo formato do log dos programas.

### Item 1: interação com o servidor central
**a) Three-way handshake.** Filtro: `tcp.port == 45000 && tcp.flags.syn == 1`
Aparecem 3 SYN (um por peer) e 3 SYN/ACK. Tirando o filtro de flags, cada SYN → SYN/ACK → ACK aparece em sequência. *Statistics → Conversations → TCP* mostra as 3 conversas com a porta 45000.

**b) Os 2 primeiros bytes do REGISTER (big-endian → decimal).** Filtro: `tcp.dstport == 45000 && tcp.len > 0`
Clicar no pacote de um peer; no painel de bytes, logo depois do cabeçalho TCP, estão os 2 bytes de tamanho e depois `{"ip_p2p":...`. Exemplo real da nossa captura (Bob):

```
0x0030:  .... 0054 7b22 6970 5f70 3270 223a   ..T{"ip_p2p":
```

`00 54` em big-endian = 0×256 + 0x54 = **84**, e o campo `Len` do TCP é **86** = 2 de cabeçalho + 84 de JSON. A linha `REGISTER enviado: cabeçalho 0x00 0x54 = 84 bytes de JSON` no terminal do Bob confirma. Alice e Carol têm nomes de 5 letras, então mostram `00 56` = 86.
Bônus para o relatório: o **ROSTER** tem 260 bytes, cabeçalho `01 04` = 1×256 + 4 = **260**. Mostra bem por que o byte alto vem primeiro.
Obs.: o JSON sai com as chaves em ordem alfabética (`"tipo"` por último) porque a nlohmann/json ordena as chaves. JSON não tem ordem de campos, então está conforme o §7.

**c) Fechamento (FIN/ACK).** Filtro: `tcp.port == 45000 && tcp.flags.fin == 1`
6 pacotes FIN: 2 por conexão (servidor → peer e peer → servidor), cada um seguido de ACK. Nenhum RST. Depois disso não há **nenhum** pacote na porta 45000: o servidor terminou e não media nada.

**d) Follow TCP Stream.** Botão direito num pacote da porta 45000 → *Follow → TCP Stream*: mostra REGISTER (vermelho, do peer) e ROSTER (azul, do servidor) da sessão inteira.

### Item 2: formação da malha P2P
**a) Abertura concorrente das 3 conexões.** Filtro: `tcp.port in {50001 50002 50003} && tcp.flags.syn == 1 && tcp.flags.ack == 0`
Exatamente **3 SYN**:
- porta efêmera da Alice → **50002** (Bob)
- porta efêmera da Alice → **50003** (Carol)
- porta efêmera do Bob → **50003** (Carol)

Na nossa captura os três SYN saíram em 15:27:42.902280, .902300 e .902611 (menos de 1 ms entre eles), o que mostra a abertura concorrente. Nenhum SYN para 50001 (ninguém conecta na Alice, ela é o menor nome) e nenhuma conexão duplicada. *Statistics → Conversations → TCP* deve listar só essas 3 conversas P2P.

**b) Troca de HELLO.** Filtro: `tcp.port in {50001 50002 50003} && tcp.len > 0 && frame contains "HELLO"`
6 pacotes, 2 por conexão: primeiro o de quem conectou, depois a resposta de quem aceitou. Cada um começa com o tamanho do JSON (`00 2c` = 44 para Alice e Carol, `00 2a` = 42 para Bob, com timeout 30) seguido de `{"nome":"...","timeout":30,"tipo":"HELLO"}`. *Follow → TCP Stream* em cada conexão mostra os dois HELLO.

**c) Fechamento (ainda sem jogo).** Com a versão atual (sem as fases de B e C), logo após os HELLO aparecem os FIN/ACK das 3 conexões P2P (6 FIN). Quando B e C integrarem, os MOVE_COMMIT/MOVE_REVEAL/GAME_RESULT vão aparecer entre os HELLO e esses FIN (itens 3 e 4, de B e C).

---

## 7. Decisões tomadas onde o enunciado é omisso

- Servidor recusa REGISTER inválido, nome repetido ou `ip:porta` repetido fechando só aquela conexão (CONTRATO §3.2).
- Peer tenta conectar ao servidor por até 10 s (para não depender da ordem exata em que os terminais são abertos).
- Prazo de 30 s para a malha se formar e 5 s para cada HELLO; se estourar, o peer mostra o erro, fecha o que tiver aberto e sai com código 1.
- Nome do jogador limitado a 64 caracteres visíveis, sem espaço.
- O servidor leva até 200 ms entre o 3º REGISTER e o envio do ROSTER (é o intervalo do `poll` do accept). Não afeta nada do jogo.
