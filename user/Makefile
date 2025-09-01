CC=$(TARGET)-gcc
CXX=$(TARGET)-g++
CFLAGS?=-g
CXXFLAGS?=-g
OBJS:=

SRC_DIR:=.
BIN_DIR:=$(SYSROOT)/bin

C_SOURCES:=$(wildcard $(SRC_DIR)/*.c)
CPP_SOURCES:=$(wildcard $(SRC_DIR)/*.cpp)

C_EXECUTABLES:=$(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(C_SOURCES))
CPP_EXECUTABLES:=$(patsubst $(SRC_DIR)/%.cpp,$(BIN_DIR)/%,$(CPP_SOURCES))

EXECUTABLES:=$(C_EXECUTABLES) $(CPP_EXECUTABLES)

all: $(EXECUTABLES)

$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $< $(CFLAGS) $(OBJS)

$(BIN_DIR)/%: $(SRC_DIR)/%.cpp
	@mkdir -p $(BIN_DIR)
	$(CXX) -o $@ $< $(CXXFLAGS) $(OBJS)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
