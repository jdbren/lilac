CC=$(TARGET)-gcc
CFLAGS?=-g
OBJS=

SRC_DIR:=.
BIN_DIR:=$(SYSROOT)/bin

SOURCES:=$(wildcard $(SRC_DIR)/*.c)
EXECUTABLES:=$(patsubst $(SRC_DIR)/%.c,$(BIN_DIR)/%,$(SOURCES))

all: $(EXECUTABLES)

$(BIN_DIR)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_DIR)
	$(CC) -o $@ $< $(CFLAGS) $(OBJS)

clean:
	rm -rf $(BIN_DIR)

.PHONY: all clean
