# Pessoa C: Fase 2, Observador, GAME_RESULT e Encerramento

Este documento explica **o que foi feito e por quê** na parte da Pessoa C (CONTRATO.md §5): a lógica da Fase 2 (Par ou Ímpar), o comportamento do observador, o envio do resultado final, o encerramento com `close()` e a entrada com timeout. Também detalha como auditar essa etapa no Wireshark (item 4 do §8 do enunciado).

Todas as referências a seções (§) são do PDF do trabalho, salvo quando diz "CONTRATO".

---

## 0. Resumo rápido

| Arquivo | O que é | Situação |
|---|---|---|
| `codigo/include/fase2.hpp`, `codigo/src/fase2.cpp` | Lógica central da Fase 2, isolamento do observador e envio/validação do `GAME_RESULT`. | novos |
| `codigo/src/game_peer.cpp` | Integração da chamada `jogo::jogar_fase2` no lugar do `TODO(C)`. | alterado |
| `codigo/Makefile` | Inclusão do `fase2.o` na regra de compilação do `game_peer`. | alterado |
| `codigo/src/entrada.cpp` | Leitura assíncrona do teclado com `poll()`. | mantido (escrito na base por B, revisado por C) |

**Resultado dos testes:** compila do zero com `make` sem warnings. O encerramento com `close()` ocorre de forma limpa em todos os nós, gerando FIN/ACKs corretos no Wireshark sem disparar *connection resets* (RST).

---

## 1. Fase 2: Par ou Ímpar (`jogar_fase2`)

### Passo 1.1: Identificando os papéis
A Fase 1 devolve a estrutura `ResultadoFase1`, que contém o nome do eliminado e um vetor `finalistas` já ordenado de forma crescente (ordem lexicográfica byte a byte, `std::string::operator<`).
**Decisão:** Não precisamos de mensagens de rede para decidir quem é PAR ou ÍMPAR. O vetor já nos diz que `finalistas[0]` é o PAR e `finalistas[1]` é o ÍMPAR. O nó compara o seu próprio nome (`mesh.nome()`) com esses dados para descobrir seu papel no jogo.

### Passo 1.2: Isolando o Observador
Se o meu nome for igual ao do eliminado, eu sou o observador.
**Por quê:** O observador não digita jogada na Fase 2 e não participa da barreira criptográfica. Ele entra em um bloco condicional `if (eu == obs)` onde fica travado esperando o `GAME_RESULT`.

### Passo 1.3: Commit-Reveal Restrito
Se eu for finalista, leio minha jogada (de 0 a 5) e chamo `etapa_commit_reveal` (da Pessoa B).
**O Pulo do Gato:** Passo como parâmetro de destino *apenas* o nome do adversário (`{adversario}`). Isso garante que o COMMIT e o REVEAL da Fase 2 trafeguem apenas entre os dois finalistas, cumprindo a regra de que o observador é passivo e não recebe tráfego de jogo nesta etapa.

---

## 2. O GAME_RESULT e a Dupla Checagem

Quando o Commit-Reveal da Fase 2 acaba, os dois finalistas conhecem as duas jogadas. Eles usam `proto::make_game_result` para montar o JSON. Essa função empacota os dados e já calcula se a soma é par ou ímpar, determinando o vencedor.

### Passo 2.1: Envio
Os dois finalistas fazem `mesh.broadcast(resultado_json, {obs})`.
**Por quê os dois enviam?** Para descentralizar a confiança. Se apenas um enviasse, ele poderia alterar o JSON forjando a própria vitória e o observador não teria como contestar.

### Passo 2.2: Validação pelo Observador
O observador usa `mesh.esperar_tipo(proto::GAME_RESULT, ...)` duas vezes: uma para receber do PAR e outra para receber do ÍMPAR. O prazo usado é o prazo padrão (maior timeout da rede + 5s).
**A Dupla Checagem:** O observador compara se o JSON que o PAR mandou é *exatamente igual* ao JSON que o ÍMPAR mandou (`if (*msg_par != *msg_impar)`). Se houver divergência, ele lança uma `FraudeError`, o jogo avisa na tela e aborta.

### Passo 2.3: Sumário
Após a validação, **todos os nós** imprimem na tela o `RESUMO DA PARTIDA`, mostrando as jogadas da Fase 2, a soma e o vencedor final.

---

## 3. Encerramento e `close()` (FIN/ACK)

Após imprimir o sumário, a função `jogar_fase2` retorna. O `main` em `game_peer.cpp` então executa o bloco final que chama `mesh.fechar_tudo()`.

**Como o fechamento limpo funciona (implementado na base por A, validado por C):**
1. O nó executa `shutdown(fd, SHUT_WR)` em todos os 3 sockets P2P. Isso envia o pacote TCP **FIN** para a rede, dizendo "não vou escrever mais nada".
2. As threads de recepção de cada conexão (que estão rodando em laço de `recv`) detectam quando o outro lado também manda o FIN (o `recv` retorna `0`).
3. Quando o `recv` retorna `0`, a thread encerra.
4. O `fechar_tudo()` usa `join()` para esperar todas as threads de recepção acabarem e **só então** chama `close(fd)`.

**Por que isso é vital:** Se chamássemos `close()` direto enquanto as threads ainda estivessem no `recv`, causaríamos comportamentos indefinidos ou abortos abruptos (RST). O `shutdown` seguido de `join` e `close` garante o *graceful teardown* exigido (§12.2).

---

## 4. Entrada com Timeout (`entrada.cpp`)

A leitura do teclado (obrigação da Pessoa C, mas criada provisoriamente por B para testes) funciona usando `poll()` no descritor de arquivo de entrada padrão (`STDIN_FILENO`).
- Ele calcula quanto tempo falta (`limite - agora`).
- Chama `poll()` com esse tempo. Se o usuário não digitar nada antes do tempo acabar, o `poll` retorna 0.
- Se retornar 0, o laço quebra, o programa sorteia um número válido aleatoriamente (`std::uniform_int_distribution`), imprime que usou uma jogada aleatória e segue o jogo. Isso evita que o jogo inteiro trave porque um único jogador foi tomar café.

---

## 5. Wireshark: item 4 do §8

Para o seu relatório e roteiro de vídeo, você precisa demonstrar o fim do jogo.

### a) GAME_RESULT chegando ao observador
- **Filtro:** `tcp.payload contains "GAME_RESULT"`
- **O que provar:** Clique no pacote, vá ao painel de bytes e mostre a estrutura do JSON. Mostre que existem **dois** pacotes com esse conteúdo sendo enviados para a porta de escuta do observador (um vindo do PAR, outro vindo do ÍMPAR).

### b) FIN/ACK em todas as conexões P2P
- **Filtro:** `tcp.flags.fin == 1 && tcp.port in {50001 50002 50003}` (ajuste as portas para as do seu teste).
- **O que provar:** No fim da captura, após o `GAME_RESULT`, devem aparecer **6 pacotes com a flag FIN setada** (2 para cada uma das 3 conexões, representando o FIN de ida e o FIN de volta).
- **Verificação de ausência de erros:** Limpe o filtro e desça até o fim da captura. Certifique-se de que não existem pacotes pretos/vermelhos marcados como `RST` (Reset). A presença apenas de `FIN` seguidos de `ACK` prova que o `close()` foi perfeitamente sincronizado.