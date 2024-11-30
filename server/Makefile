# Export LD_LIBRARY_PATH
export LD_LIBRARY_PATH := $(LD_LIBRARY_PATH):/opt/homebrew/lib

CXX = g++
CXXFLAGS = -std=c++17 -Wall -I/opt/homebrew/include -I/opt/homebrew/include/nlohmann -I/opt/homebrew/opt/openssl@3/include -DCPPHTTPLIB_OPENSSL_SUPPORT
LDFLAGS = -L/opt/homebrew/lib -L/opt/homebrew/opt/openssl@3/lib -lssl -lcrypto -lboost_system -lpthread -lfmt

TARGET = server
SRCS = server.cpp

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean


