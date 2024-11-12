CXX = g++
CXXFLAGS = -Wall -std=c++17 -I ./include

SERVER_DIR = ./server
BIN_DIR = ./bin
SERVER_SRC = $(SERVER_DIR)/medium_perf_server.cpp

$(shell mkdir -p $(BIN_DIR))

$(BIN_DIR)/server: $(SERVER_SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean