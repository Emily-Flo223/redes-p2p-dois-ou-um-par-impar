# Redes P2P: Dois ou Um e Par ou Ímpar

Este repositório contém o código-fonte de uma aplicação distribuída que implementa uma rodada eliminatória dos jogos "Dois ou Um" e "Par ou Ímpar", desenvolvida como trabalho prático para a disciplina de Redes de Computadores.

## Arquitetura do Sistema

O sistema utiliza uma arquitetura híbrida:
- **Fase de Descoberta:** Baseada num modelo cliente-servidor, onde um Servidor Central (Rendezvous) regista os jogadores participantes e partilha o catálogo de nós.
- **Fase de Jogo:** Baseada num modelo Peer-to-Peer (P2P), operando numa malha completa (K3). Três jogadores conectam-se diretamente entre si, sendo as conexões TCP bidirecionais puras, sem recurso a frameworks de alto nível.

A integridade do jogo é garantida através de um esquema criptográfico **Commit-Reveal** com *hashes* SHA-256, impedindo batotas ou antecipação de jogadas. Todas as mensagens trocadas no protocolo incluem um cabeçalho explícito de 2 bytes (em codificação *big-endian*) e um corpo em formato JSON.

## Fases do Jogo (Rodada Única)

A partida está dividida em duas fases consecutivas:

1.  **Fase 1 ("Dois ou Um"):** Disputada simultaneamente pelos três participantes (Peer A, B e C). O jogador que apresentar a jogada minoritária é eliminado e transita para um estado de observador passivo. Em caso de empate absoluto, a rodada repete-se.
2.  **Fase 2 ("Par ou Ímpar"):** Disputada exclusivamente entre os dois sobreviventes da fase anterior. A determinação de quem escolhe "Par" ou "Ímpar" baseia-se na ordem lexicográfica dos seus apelidos.

Ao término da Fase 2, os finalistas comunicam o vencedor de forma descentralizada. Todos os nós registam o resultado final e procedem ao encerramento limpo imediato das conexões TCP (*close*).

## Requisitos e Restrições Técnicas

- As comunicações efetuam-se usando exclusivamente **sockets TCP puros** (as bibliotecas de comunicação de alto nível são proibidas).
- Tratamento explícito de chamadas bloqueantes com recurso a gestão de *threads*.
- Encerramento rigoroso e limpo de todas as conexões TCP após o final do jogo.

## Estrutura do Projeto

*   `rendezvous_server`: Serviço central transitório de registo dos três jogadores iniciais.
*   `game_peer`: Serviço que corre em cada nó para efetuar a lógica de jogo P2P.

## Como Executar

### 1. Iniciar o Servidor Central
Execute o comando abaixo, indicando a porta de escuta pretendida:
```bash
./rendezvous_server <porta_servidor>
```

### 2. Iniciar os Nós Jogadores (Peers)
Inicie três instâncias distintas do nó jogador. Cada instância obriga a seis parâmetros específicos:
```bash
./game_peer <ip_servidor> <porta_servidor> <ip_escuta_p2p> <porta_escuta_p2p> <nome> <timeout_jogada>
```

## Ferramentas para Análise e Depuração

Toda a interação de pacotes TCP e sincronização das fases pode (e deve) ser auditada recorrendo ao **Wireshark**.

# Redes P2P: Dois ou Um e Par ou Ímpar

Este repositório contém o código-fonte de uma aplicação distribuída que implementa uma rodada eliminatória dos jogos "Dois ou Um" e "Par ou Ímpar", desenvolvida como trabalho prático para a disciplina de Redes de Computadores.

## Arquitetura do Sistema

O sistema utiliza uma arquitetura híbrida:
- **Fase de Descoberta:** Baseada num modelo cliente-servidor, onde um Servidor Central (Rendezvous) regista os jogadores participantes e partilha o catálogo de nós.
- **Fase de Jogo:** Baseada num modelo Peer-to-Peer (P2P), operando numa malha completa (K3). Três jogadores conectam-se diretamente entre si, sendo as conexões TCP bidirecionais puras, sem recurso a frameworks de alto nível.

A integridade do jogo é garantida através de um esquema criptográfico **Commit-Reveal** com *hashes* SHA-256, impedindo batotas ou antecipação de jogadas. Todas as mensagens trocadas no protocolo incluem um cabeçalho explícito de 2 bytes (em codificação *big-endian*) e um corpo em formato JSON.

## Fases do Jogo (Rodada Única)

A partida está dividida em duas fases consecutivas:

1.  **Fase 1 ("Dois ou Um"):** Disputada simultaneamente pelos três participantes (Peer A, B e C). O jogador que apresentar a jogada minoritária é eliminado e transita para um estado de observador passivo. Em caso de empate absoluto, a rodada repete-se.
2.  **Fase 2 ("Par ou Ímpar"):** Disputada exclusivamente entre os dois sobreviventes da fase anterior. A determinação de quem escolhe "Par" ou "Ímpar" baseia-se na ordem lexicográfica dos seus apelidos.

Ao término da Fase 2, os finalistas comunicam o vencedor de forma descentralizada. Todos os nós registam o resultado final e procedem ao encerramento limpo imediato das conexões TCP (*close*).

## Requisitos e Restrições Técnicas

- As comunicações efetuam-se usando exclusivamente **sockets TCP puros** (as bibliotecas de comunicação de alto nível são proibidas).
- Tratamento explícito de chamadas bloqueantes com recurso a gestão de *threads*.
- Encerramento rigoroso e limpo de todas as conexões TCP após o final do jogo.

## Estrutura do Projeto

*   `rendezvous_server`: Serviço central transitório de registo dos três jogadores iniciais.
*   `game_peer`: Serviço que corre em cada nó para efetuar a lógica de jogo P2P.

## Como Executar

### 1. Iniciar o Servidor Central
Execute o comando abaixo, indicando a porta de escuta pretendida:
```bash
./rendezvous_server <porta_servidor>
```

### 2. Iniciar os Nós Jogadores (Peers)
Inicie três instâncias distintas do nó jogador. Cada instância obriga a seis parâmetros específicos:
```bash
./game_peer <ip_servidor> <porta_servidor> <ip_escuta_p2p> <porta_escuta_p2p> <nome> <timeout_jogada>
```

## Ferramentas para Análise e Depuração

Toda a interação de pacotes TCP e sincronização das fases pode (e deve) ser auditada recorrendo ao **Wireshark**.

# Redes P2P: Dois ou Um e Par ou Ímpar

Este repositório contém o código-fonte de uma aplicação distribuída que implementa uma rodada eliminatória dos jogos "Dois ou Um" e "Par ou Ímpar", desenvolvida como trabalho prático para a disciplina de Redes de Computadores.

## Arquitetura do Sistema

O sistema utiliza uma arquitetura híbrida:
- **Fase de Descoberta:** Baseada num modelo cliente-servidor, onde um Servidor Central (Rendezvous) regista os jogadores participantes e partilha o catálogo de nós.
- **Fase de Jogo:** Baseada num modelo Peer-to-Peer (P2P), operando numa malha completa (K3). Três jogadores conectam-se diretamente entre si, sendo as conexões TCP bidirecionais puras, sem recurso a frameworks de alto nível.

A integridade do jogo é garantida através de um esquema criptográfico **Commit-Reveal** com *hashes* SHA-256, impedindo batotas ou antecipação de jogadas. Todas as mensagens trocadas no protocolo incluem um cabeçalho explícito de 2 bytes (em codificação *big-endian*) e um corpo em formato JSON.

## Fases do Jogo (Rodada Única)

A partida está dividida em duas fases consecutivas:

1.  **Fase 1 ("Dois ou Um"):** Disputada simultaneamente pelos três participantes (Peer A, B e C). O jogador que apresentar a jogada minoritária é eliminado e transita para um estado de observador passivo. Em caso de empate absoluto, a rodada repete-se.
2.  **Fase 2 ("Par ou Ímpar"):** Disputada exclusivamente entre os dois sobreviventes da fase anterior. A determinação de quem escolhe "Par" ou "Ímpar" baseia-se na ordem lexicográfica dos seus apelidos.

Ao término da Fase 2, os finalistas comunicam o vencedor de forma descentralizada. Todos os nós registam o resultado final e procedem ao encerramento limpo imediato das conexões TCP (*close*).

## Requisitos e Restrições Técnicas

- As comunicações efetuam-se usando exclusivamente **sockets TCP puros** (as bibliotecas de comunicação de alto nível são proibidas).
- Tratamento explícito de chamadas bloqueantes com recurso a gestão de *threads*.
- Encerramento rigoroso e limpo de todas as conexões TCP após o final do jogo.

## Estrutura do Projeto

*   `rendezvous_server`: Serviço central transitório de registo dos três jogadores iniciais.
*   `game_peer`: Serviço que corre em cada nó para efetuar a lógica de jogo P2P.

## Como Executar

### 1. Iniciar o Servidor Central
Execute o comando abaixo, indicando a porta de escuta pretendida:
```bash
./rendezvous_server <porta_servidor>
```

### 2. Iniciar os Nós Jogadores (Peers)
Inicie três instâncias distintas do nó jogador. Cada instância obriga a seis parâmetros específicos:
```bash
./game_peer <ip_servidor> <porta_servidor> <ip_escuta_p2p> <porta_escuta_p2p> <nome> <timeout_jogada>
```

## Ferramentas para Análise e Depuração

Toda a interação de pacotes TCP e sincronização das fases pode (e deve) ser auditada recorrendo ao **Wireshark**.

