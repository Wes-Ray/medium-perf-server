CXX = g++
CXXFLAGS = -Wall -std=c++17 -I./include

SERVER_DIR = ./server
INCLUDE_DIR = ./include
BIN_DIR = ./bin

SERVER_SRCS = $(SERVER_DIR)/medium_perf_server.cpp $(SERVER_DIR)/json.cpp
HEADERS = $(wildcard $(INCLUDE_DIR)/*.h)

$(shell mkdir -p $(BIN_DIR))

$(BIN_DIR)/server: $(SERVER_SRCS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SERVER_SRCS) -o $@

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean