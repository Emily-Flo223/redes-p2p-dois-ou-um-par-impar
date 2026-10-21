# Relatório: seção da Pessoa B (Commit-Reveal e Fase 1)

## Integridade criptográfica e Fase 1

Esta parte garante que nenhum jogador consegue mudar sua jogada depois de ver a dos outros. Ela também decide quem é eliminado na Fase 1. Cada jogada passa por um esquema Commit-Reveal com SHA-256 e por uma barreira de sincronização estrita. A Fase 1 ("Dois ou Um") repete em caso de empate e elimina o jogador que ficou sozinho no valor minoritário.

| Arquivo | Responsabilidade |
| --- | --- |
| `src/commit_reveal.cpp` | Uma etapa completa: COMMIT, barreira, REVEAL e validação. Serve para as duas fases. |
| `src/fase1.cpp` | Regra do "Dois ou Um", repetição em empate e escolha do eliminado. |
| `src/crypto.cpp` | SHA-256 (PicoSHA2), geração do salt e verificação do REVEAL. |
| `src/entrada.cpp` | Leitura da jogada pelo teclado com prazo, sem travar o programa. |
| `tests/test_fase1.cpp`, `tests/teste_fase1.sh` | Testes da regra, do hash, da fraude e da partida completa em loopback. |

### Esquema Commit-Reveal

A cada jogada, o peer gera um salt novo e envia só o hash. A jogada aberta só vai depois da barreira. O salt S tem 16 caracteres hexadecimais minúsculos, tirados de 8 bytes de `std::random_device`, e é gerado de novo em toda tentativa e em toda fase. O hash de compromisso da jogada V é:

```
H = SHA-256( V + ":" + S )
```

Por exemplo, V = 2 e S = `a1b2c3d4e5f60718` dão o hash de `"2:a1b2c3d4e5f60718"`, que é `8fd486e9...0b6e1f`. Esse valor é conferido no teste automático contra o `sha256sum` do Linux.

1. **COMMIT.** O peer envia `MOVE_COMMIT` com `fase`, `tentativa`, `jogador` e `hash` a todos os outros participantes ativos da etapa. A jogada e o salt não aparecem nessa mensagem.
2. **Barreira.** O peer espera e guarda em memória o `MOVE_COMMIT` de cada um dos outros (próxima seção).
3. **REVEAL.** O peer envia `MOVE_REVEAL` com `jogada` e `salt` em claro.
4. **Validação.** Para cada REVEAL recebido, o peer recalcula o SHA-256 e compara com o hash guardado. Também confere se a jogada está na faixa: 1 a 2 na Fase 1, 0 a 5 na Fase 2.

Um hash que não confere, uma jogada fora da faixa ou um COMMIT que não tem 64 caracteres hex é tratado como fraude. O peer mostra `FRAUDE DETECTADA` com o jogador e os valores, fecha todas as conexões com FIN/ACK e sai com código 2. Para provar isso, um peer de teste (`peer_trapaceiro`) faz o COMMIT da jogada 1 e revela a jogada 2 com o mesmo salt. Os dois peers honestos acusam a fraude.

### Barreira de sincronização estrita

Nenhum peer envia `MOVE_REVEAL` antes de ter recebido e guardado o `MOVE_COMMIT` de todos os outros ativos da mesma fase e tentativa. Isso é garantido pela estrutura do código, e não por uma verificação feita depois.

- Em `etapa_commit_reveal` existe um laço que chama `mesh.esperar(fase, tentativa, "MOVE_COMMIT", jogador, prazo)` para cada outro ativo. Cada hash recebido vai para um mapa em memória.
- O envio do REVEAL está depois desse laço. O laço só termina quando todos os COMMIT chegaram, ou então lança uma exceção (timeout ou conexão caída), e nesse caso o REVEAL nunca é enviado.
- `etapa_commit_reveal` é o único ponto do programa que envia `MOVE_REVEAL`. Não há caminho alternativo.
- A espera não ocupa a CPU. Cada conexão tem uma thread de recepção, que coloca as mensagens numa fila protegida por mutex e avisa uma `condition_variable`. A thread do jogo dorme nessa variável até a mensagem chegar ou o prazo acabar.

Na tela, cada peer registra `barreira cumprida: 2/2 MOVE_COMMIT recebidos, liberado o MOVE_REVEAL` antes do envio. Os horários têm precisão de milissegundos para serem comparados com a coluna Time do Wireshark. Na Fase 2, os ativos são só os dois finalistas, e o observador não participa da barreira.

### Timeout

Há dois prazos: um para o jogador digitar e outro para esperar os outros peers. Nenhum dos dois trava o programa.

| Prazo | Valor | Se esgotar |
| --- | --- | --- |
| Digitar a jogada | `timeout_jogada` do próprio peer (5 a 300 s) | O peer usa uma jogada válida aleatória e avisa na tela. |
| Esperar COMMIT ou REVEAL dos outros | maior `timeout_jogada` do ROSTER + 5 s | O peer mostra `partida abortada` com o motivo, fecha tudo e sai com código 3. |

A leitura do teclado usa `poll()` no stdin com o tempo restante. Uma entrada inválida pede de novo dentro do mesmo prazo. O prazo de espera é o maior timeout mais 5 s porque um jogador pode levar todo o seu tempo para digitar. Se a conexão com um peer cair, a espera termina na hora, sem aguardar o prazo, e a mensagem diz que a conexão caiu.

### Fase 1 e desempate

Se os três escolhem o mesmo valor, a Fase 1 se repete com `tentativa` + 1. Se não, o jogador sozinho no valor minoritário é eliminado. Com três jogadores e os valores 1 e 2, sempre há uma minoria de exatamente um quando não há empate. As 8 combinações possíveis são verificadas no teste automático.

O campo `tentativa` é uma extensão do grupo ao formato do §7. Ele começa em 1 e aumenta a cada empate. As mensagens são buscadas pela chave (fase, tentativa, tipo, jogador). Assim, um COMMIT da tentativa 2 que chega enquanto um peer ainda processa a tentativa 1 fica na fila e não é confundido com o anterior. O exemplo em Python fornecido apaga os commits após um empate e pode perder uma mensagem da nova tentativa.

O eliminado continua conectado como observador passivo. Os dois finalistas seguem para a Fase 2 em ordem crescente de nome, e o primeiro é o PAR.

### Auditoria no Wireshark (item 3 do §8)

A captura da partida mostra que, em cada tentativa, nenhum jogador envia `MOVE_REVEAL` antes de receber o `MOVE_COMMIT` dos outros dois. Na nossa execução houve 4 tentativas na Fase 1: três empates seguidos e a quarta decidindo. A captura foi feita na interface de loopback com o script `tests/captura_wireshark.sh`, que digita as jogadas com 2 s entre um jogador e outro. Assim, a barreira aparece como um intervalo visível na coluna Time. Sem abrir os pacotes, dá para distingui-los pelo tamanho: COMMIT tem Len 137 a 139 e REVEAL tem Len 100 a 102.

Como o JSON vem depois dos 2 bytes de cabeçalho, os filtros usam `tcp.payload contains`:

- `tcp.payload contains "MOVE_"` mostra só os COMMIT e os REVEAL.
- `tcp.payload contains "MOVE_COMMIT"` mostra só os compromissos.
- `tcp.payload contains "\"tentativa\":2"` isola uma tentativa depois de um empate.

| Print | Filtro e onde clicar | O que deve aparecer |
| --- | --- | --- |
| Opacidade do COMMIT | `tcp.payload contains "MOVE_COMMIT"`, um pacote aberto no painel de bytes | Só `fase`, `tentativa`, `jogador` e `hash` com 64 hex. Sem `jogada` e sem `salt`. Ao lado, a linha `H = SHA-256(...)` do terminal de quem enviou, com o mesmo hash. |
| Tabela cronológica da barreira | `tcp.payload contains "MOVE_"`, coluna Time em segundos | Para cada tentativa, os 6 COMMIT (cada jogador envia aos outros 2) em horários diferentes. O primeiro REVEAL de cada jogador só aparece depois que ele recebeu os 2 COMMIT dos outros. |
| REVEAL e validação | um `MOVE_REVEAL` aberto, mais o terminal de quem recebeu | `jogada` e `salt` em claro. No terminal, `MOVE_REVEAL de X conferido: ... hash OK`. |
| Empate e nova tentativa | `tcp.payload contains "MOVE_"` cobrindo duas tentativas | Mensagens com `"tentativa":1` e jogadas iguais, depois a mesma sequência com `"tentativa":2`, sem mistura entre elas. |
| Follow TCP Stream | botão direito numa conexão P2P, Follow, TCP Stream | HELLO, COMMIT e REVEAL de cada tentativa na ordem, nos dois sentidos. Os 2 bytes antes de cada `{` são o cabeçalho. |
| Fraude (opcional) | captura do cenário `fraude` e terminal de Alice ou Bob | A Carol revela jogada 2 com o hash da jogada 1, e o terminal mostra `FRAUDE DETECTADA`. |

![Captura com filtro MOVE_, quadros 52 a 101](wireshark_inicio.png)

**Figura 1. Tentativas 1 e 2 e início da 3.** Na tentativa 1, Carol envia COMMIT em 9,92 s, Bob em 12,50 s e Alice em 15,216 s. Cada jogador só revela depois de receber os outros dois. Bob revela no quadro 61, logo após o COMMIT de Alice no quadro 60, e Carol só revela no quadro 67, depois do COMMIT de Alice que chega a ela no quadro 65. O painel de bytes mostra o COMMIT de Carol (quadro 52) com `fase`, `hash`, `jogador` e `tentativa`, sem jogada nem salt.

![Captura com filtro MOVE_, quadros 87 a 136](wireshark_final.png)

**Figura 2. Fim da tentativa 2 e tentativas 3 e 4.** Nas tentativas 3 e 4, os seis COMMIT saem em três momentos separados (26,0 s, 28,1 s e 30,66 s; 34,6 s, 36,6 s e 39,57 s), e os seis REVEAL só aparecem depois do último deles. A tentativa 4 é a que decide a eliminação.

Modelo da tabela cronológica para o relatório (preencha com os tempos da sua captura, uma tabela por tentativa):

| Time (s) | De | Para | Mensagem | Tentativa |
| --- | --- | --- | --- | --- |
|  | Alice | Bob | MOVE_COMMIT | 1 |
|  | Alice | Carol | MOVE_COMMIT | 1 |
|  | Bob | Alice | MOVE_COMMIT | 1 |
|  | Bob | Carol | MOVE_COMMIT | 1 |
|  | Carol | Alice | MOVE_COMMIT | 1 |
|  | Carol | Bob | MOVE_COMMIT | 1 |
|  | (primeiro) |  | MOVE_REVEAL | 1 |

Para saber quem é quem em cada linha, use o campo `jogador` do JSON. O destino é o outro lado da conexão, identificado pelo HELLO daquela stream. O script imprime essa tabela pronta quando o tshark está instalado.

### Dificuldades

- **Mensagens de tentativas diferentes.** Depois de um empate, um peer rápido pode mandar o COMMIT da tentativa seguinte antes de outro peer terminar a anterior. Isso foi resolvido com o campo `tentativa` e a busca por chave na fila, em vez de apagar mensagens após o empate.
- **Provar a barreira na captura.** Em loopback, todas as mensagens saem no mesmo milissegundo, e a tabela do Wireshark não mostra ordem nenhuma. O script de captura passou a digitar as jogadas com 2 s de intervalo, e a barreira ficou visível na coluna Time.
- **Testar a detecção de fraude.** Um peer honesto nunca gera um REVEAL inválido. Por isso, foi escrito um peer de teste que trapaceia de propósito.
- **Esperar sem travar.** A thread do jogo não pode chamar `recv` diretamente nem ficar em espera ativa. A solução foi a fila com `condition_variable`, com prazo, alimentada pelas threads de recepção de cada conexão.
