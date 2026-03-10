SHELL := C:/msys64/usr/bin/sh.exe
CC := C:/msys64/ucrt64/bin/gcc.exe
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I. -Imodules
BIN_DIR := bin

DISASM_BIN := $(BIN_DIR)/disasm
TEST_BIN := $(BIN_DIR)/test_disasm_golden
GRAPH_REQ_TEST_BIN := $(BIN_DIR)/test_graph_memory_requirements

.PHONY: all disasm test test-unit test-cli-golden clean

all: disasm

disasm: $(DISASM_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(DISASM_BIN): runtime/loader/blob.c runtime/loader/blob.h tools/disasm_main.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c tools/disasm_main.c -o $(DISASM_BIN)

$(TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h tests/test_disasm_golden.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c tests/test_disasm_golden.c -o $(TEST_BIN)

$(GRAPH_REQ_TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h runtime/runtime_types.h runtime/engine/graph_instance.h runtime/engine/graph_instance.c modules/module_abi.h modules/gain/gain.c tests/test_graph_memory_requirements.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c runtime/engine/graph_instance.c modules/gain/gain.c tests/test_graph_memory_requirements.c -o $(GRAPH_REQ_TEST_BIN)

test-unit: $(TEST_BIN) $(GRAPH_REQ_TEST_BIN)
	$(TEST_BIN)
	$(GRAPH_REQ_TEST_BIN)

test-cli-golden: disasm
	$(SHELL) tests/ci_cli_golden.sh

test: test-unit test-cli-golden

clean:
	rm -rf $(BIN_DIR) out.grph out.disasm.txt
