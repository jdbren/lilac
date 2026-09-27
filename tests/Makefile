CC=$(TARGET)-gcc
CFLAGS?=-g -O1
CFLAGS:=$(CFLAGS) -Wall -Wextra -Wno-unused-parameter -Wno-unused-result

SRC_DIR:=.
BIN_DIR:=$(SYSROOT)/tests

SUITE_SOURCES:=$(wildcard $(SRC_DIR)/t_*.c)
SUITES:=$(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(SUITE_SOURCES))
TOOLS:=$(BIN_DIR)/run $(BIN_DIR)/helper

all: $(SUITES) $(TOOLS)

$(BIN_DIR)/t_%: $(SRC_DIR)/t_%.c ktest.c ktest.h
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $< ktest.c $(CFLAGS) -lpthread

$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $< $(CFLAGS)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
