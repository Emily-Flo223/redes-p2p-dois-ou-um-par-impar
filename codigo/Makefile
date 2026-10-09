# Makefile — Trabalho Prático "Dois ou Um" + "Par ou Ímpar" P2P (C++17, Linux)
# Uso:  make          compila rendezvous_server e game_peer
#       make test     compila e roda os testes de framing/protocolo/cripto
#       make clean    apaga binários e objetos
# Dependências: apenas g++ (>= 7) e make. JSON e SHA-256 vêm em third_party/ (headers únicos).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
CPPFLAGS += -Iinclude -Ithird_party
LDFLAGS  += -pthread
CXXFLAGS += -pthread

BUILD := build
COMMON_SRC := src/framing.cpp src/protocol.cpp src/crypto.cpp
COMMON_OBJ := $(COMMON_SRC:src/%.cpp=$(BUILD)/%.o)

.PHONY: all test clean

all: rendezvous_server game_peer

rendezvous_server: $(BUILD)/rendezvous_server.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

JOGO_OBJ := $(BUILD)/mesh.o $(BUILD)/commit_reveal.o $(BUILD)/fase1.o $(BUILD)/entrada.o

game_peer: $(BUILD)/game_peer.o $(JOGO_OBJ) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_framing: $(BUILD)/test_framing.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(BUILD)/test_fase1: $(BUILD)/test_fase1.o $(BUILD)/fase1.o $(BUILD)/commit_reveal.o $(BUILD)/mesh.o $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

# Peer de teste que trapaceia de propósito (revela jogada diferente da do COMMIT).
$(BUILD)/peer_trapaceiro: $(BUILD)/peer_trapaceiro.o $(JOGO_OBJ) $(COMMON_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: $(BUILD)/test_framing $(BUILD)/test_fase1
	./$(BUILD)/test_framing
	./$(BUILD)/test_fase1

$(BUILD)/%.o: src/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD)/%.o: tests/%.cpp | $(BUILD)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -MMD -MP -c $< -o $@

$(BUILD):
	mkdir -p $@

clean:
	rm -rf $(BUILD) rendezvous_server game_peer

-include $(wildcard $(BUILD)/*.d)
