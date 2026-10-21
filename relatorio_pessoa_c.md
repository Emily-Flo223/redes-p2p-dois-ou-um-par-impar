# Relatório: seção da Pessoa C (Fase 2, Observador e Encerramento)

## Fase 2, Consenso de Resultados e Fechamento

Esta seção documenta a implementação do encerramento da partida, englobando a lógica da Fase 2 ("Par ou Ímpar"), o isolamento do jogador eliminado (observador), o envio e a validação do resultado final, e o fechamento limpo das conexões TCP da malha K3.

| Arquivo | Responsabilidade |
| --- | --- |
| `src/fase2.cpp`, `include/fase2.hpp` | Lógica central da Fase 2, determinação de PAR/ÍMPAR, envio do `GAME_RESULT` e exibição do sumário. |
| `src/entrada.cpp` | Leitura da jogada pelo teclado com uso de `poll()` para respeitar o limite de tempo (timeout_jogada). |
| `Makefile` | Atualização das regras de compilação para incluir os novos objetos da lógica final. |

### Fase 2: Par ou Ímpar

A Fase 2 ocorre exclusivamente entre os dois finalistas que não foram eliminados na Fase 1. A determinação dos papéis ocorre de forma descentralizada e determinística, sem necessidade de troca de mensagens extras: os nomes dos dois finalistas são comparados em ordem lexicográfica (byte a byte). O nome menor é designado como **PAR**, e o nome maior como **ÍMPAR**.

Durante esta etapa, reutilizamos a infraestrutura de Commit-Reveal (desenvolvida pela Pessoa B). No entanto, o escopo dos participantes ativos é restrito. A função `etapa_commit_reveal` é invocada apenas com o nome do adversário na lista de destinos, garantindo que o jogador eliminado não receba os hashes ou as jogadas da Fase 2. O intervalo válido para a entrada via teclado nesta fase é de 0 a 5. 

### O Papel do Observador e o GAME_RESULT

O jogador eliminado na Fase 1 transita para o estado de "observador passivo". Ele não encerra suas conexões prematuramente. Em vez disso, ele aguarda o término da Fase 2 para receber e auditar o resultado.

Assim que os finalistas terminam a etapa de Commit-Reveal da Fase 2, eles calculam localmente a soma e determinam o vencedor. Ambos montam uma mensagem JSON do tipo `GAME_RESULT`, contendo o vencedor, o eliminado da Fase 1 e os detalhes da Fase 2 (jogadores e suas respectivas jogadas). 

Para evitar fraudes onde um finalista tenta forjar uma vitória unilateral, aplicamos uma técnica de **dupla checagem**:
1. Os dois finalistas enviam o `GAME_RESULT` ao observador de forma independente.
2. O observador utiliza a função `mesh.esperar_tipo` com prazo para coletar as mensagens dos dois finalistas.
3. Se as mensagens não forem absolutamente idênticas (ou se ocorrer timeout de um dos lados), o observador acusa fraude, dispara a interrupção da partida e sai com código de erro 2.

### Encerramento Limpo das Conexões TCP (FIN/ACK)

Após a validação do `GAME_RESULT` e a exibição do sumário final na tela de todos os nós, o programa invoca o método `mesh.fechar_tudo()`. 

Para cumprir estritamente os requisitos de liberação de recursos, o encerramento não acontece de forma abrupta. O método envia um `shutdown(SHUT_WR)` em todos os descritores de rede P2P, sinalizando o envio de um pacote TCP FIN aos vizinhos. As threads de recepção leem até encontrarem o fim do fluxo (retorno `0` do `recv`), confirmando o recebimento do FIN do outro lado (ACK). Somente após o encerramento ordenado (Join) de todas as threads de recepção é que o sistema operacional invoca o `close()` real no socket, evitando o estado TIME_WAIT infinito ou vazamento de descritores de arquivo.

### Entrada com Prazo (Timeout)

A leitura das jogadas (responsabilidade de Pessoa C, estruturada com o apoio da Pessoa B para testes antecipados) foi implementada usando a chamada de sistema POSIX `poll()` sobre o descritor `STDIN_FILENO`. O laço de leitura monitora o tempo decorrido utilizando `std::chrono::steady_clock`. Se o tempo exceder o parâmetro `timeout_jogada` do usuário, a entrada é abortada e uma jogada aleatória válida é selecionada automaticamente, evitando que um peer ausente trave o progresso do sistema distribuído.

---

### Auditoria no Wireshark (Item 4 do §8)

Para comprovar a corretude desta etapa, os seguintes eventos podem ser auditados utilizando o Wireshark na interface de loopback (`lo`).

| Evento | Filtro Sugerido e Onde Clicar | O que deve aparecer na captura |
| --- | --- | --- |
| **GAME_RESULT chegando ao observador** | `tcp.payload contains "GAME_RESULT"` | Dois pacotes direcionados à porta efêmera (ou de escuta) do jogador eliminado. O painel de bytes mostrará o JSON completo com os detalhes da Fase 2 (soma, vencedor, etc). O conteúdo de ambos os pacotes deve ser idêntico. |
| **Encerramento Limpo (FIN/ACK)** | `tcp.flags.fin == 1` (ao final do fluxo) | Serão visualizados pacotes FIN sendo trocados em ambas as direções para cada uma das 3 conexões P2P. Todos os pacotes FIN são sucedidos por ACKs, totalizando o desligamento integral do triângulo K3 sem pacotes de RST (Reset). |
| **Follow TCP Stream** | Clique direito sobre um dos pacotes de `GAME_RESULT` > *Follow > TCP Stream* | A visualização remontará a stream inteira da conexão entre um finalista e o observador, mostrando: `HELLO` inicial, os `MOVE_COMMIT` e `MOVE_REVEAL` da Fase 1, e finalmente o `GAME_RESULT` antes do encerramento da conexão. |

### Dificuldades Encontradas

- **Isolamento do tráfego para o observador:** Garantir que o observador não recebesse as mensagens `MOVE_COMMIT` da Fase 2 exigiu um cuidado especial na passagem da lista de destinos para a função de broadcast, filtrando ativamente o peer eliminado.
- **Validação simultânea:** O observador não sabe qual finalista enviará a mensagem primeiro. Utilizar o mecanismo de fila sincronizada (`esperar_tipo`) foi crucial para permitir a coleta concorrente das mensagens independentemente da ordem de chegada da rede.