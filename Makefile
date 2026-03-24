SHELL := C:/msys64/usr/bin/sh.exe
CC := C:/msys64/ucrt64/bin/gcc.exe
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -I. -Imodules
BIN_DIR := bin

EXE_EXT := .exe

DISASM_BIN := $(BIN_DIR)/disasm$(EXE_EXT)
TEST_BIN := $(BIN_DIR)/test_disasm_golden$(EXE_EXT)
GRAPH_REQ_TEST_BIN := $(BIN_DIR)/test_graph_memory_requirements$(EXE_EXT)
BLOB_CURSOR_TEST_BIN := $(BIN_DIR)/test_blob_cursor$(EXE_EXT)
GRAPH_BIND_TEST_BIN := $(BIN_DIR)/test_graph_bind$(EXE_EXT)
GRAPH_PROCESS_TEST_BIN := $(BIN_DIR)/test_graph_process$(EXE_EXT)
RUNTIME_FROM_BLOB_TEST_BIN := $(BIN_DIR)/test_runtime_from_blob$(EXE_EXT)

.PHONY: all disasm test test-unit test-cli-golden clean

all: disasm

disasm: $(DISASM_BIN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(DISASM_BIN): runtime/loader/blob.c runtime/loader/blob.h tools/disasm_main.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c tools/disasm_main.c -o $(DISASM_BIN)

$(TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h tests/test_disasm_golden.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c tests/test_disasm_golden.c -o $(TEST_BIN)

$(GRAPH_REQ_TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h runtime/loader/blob_cursor.h runtime/loader/blob_cursor.c runtime/common/mem_arena.h runtime/common/mem_arena.c runtime/runtime_types.h runtime/engine/graph_instance.h runtime/engine/graph_instance.c modules/module_abi.h modules/gain/gain.c tests/test_graph_memory_requirements.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c runtime/loader/blob_cursor.c runtime/common/mem_arena.c runtime/engine/graph_instance.c modules/gain/gain.c tests/test_graph_memory_requirements.c -o $(GRAPH_REQ_TEST_BIN)

$(BLOB_CURSOR_TEST_BIN): runtime/loader/blob_cursor.h runtime/loader/blob_cursor.c tests/test_blob_cursor.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob_cursor.c tests/test_blob_cursor.c -o $(BLOB_CURSOR_TEST_BIN)

$(GRAPH_BIND_TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h runtime/loader/blob_cursor.h runtime/loader/blob_cursor.c runtime/common/mem_arena.h runtime/common/mem_arena.c runtime/runtime_types.h runtime/engine/graph_instance.h runtime/engine/graph_instance.c modules/module_abi.h modules/gain/gain.c tests/test_graph_bind.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c runtime/loader/blob_cursor.c runtime/common/mem_arena.c runtime/engine/graph_instance.c modules/gain/gain.c tests/test_graph_bind.c -o $(GRAPH_BIND_TEST_BIN)

$(GRAPH_PROCESS_TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h runtime/loader/blob_cursor.h runtime/loader/blob_cursor.c runtime/common/mem_arena.h runtime/common/mem_arena.c runtime/runtime_types.h runtime/engine/graph_instance.h runtime/engine/graph_instance.c modules/module_abi.h modules/gain/gain.c tests/test_graph_process.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c runtime/loader/blob_cursor.c runtime/common/mem_arena.c runtime/engine/graph_instance.c modules/gain/gain.c tests/test_graph_process.c -lm -o $(GRAPH_PROCESS_TEST_BIN)

$(RUNTIME_FROM_BLOB_TEST_BIN): runtime/loader/blob.c runtime/loader/blob.h runtime/loader/blob_cursor.h runtime/loader/blob_cursor.c runtime/common/mem_arena.h runtime/common/mem_arena.c runtime/runtime_types.h runtime/engine/graph_instance.h runtime/engine/graph_instance.c modules/module_abi.h modules/gain/gain.c modules/sum2/sum2.h modules/sum2/sum2.c tests/test_runtime_from_blob.c | $(BIN_DIR)
	$(CC) $(CFLAGS) runtime/loader/blob.c runtime/loader/blob_cursor.c runtime/common/mem_arena.c runtime/engine/graph_instance.c modules/gain/gain.c modules/sum2/sum2.c tests/test_runtime_from_blob.c -lm -o $(RUNTIME_FROM_BLOB_TEST_BIN)

test-unit: $(DISASM_BIN) $(TEST_BIN) $(GRAPH_REQ_TEST_BIN) $(BLOB_CURSOR_TEST_BIN) $(GRAPH_BIND_TEST_BIN) $(GRAPH_PROCESS_TEST_BIN) $(RUNTIME_FROM_BLOB_TEST_BIN)
	$(TEST_BIN)
	$(GRAPH_REQ_TEST_BIN)
	$(BLOB_CURSOR_TEST_BIN)
	$(GRAPH_BIND_TEST_BIN)
	$(GRAPH_PROCESS_TEST_BIN)
	python tests/test_compiler_runtime.py --runner $(RUNTIME_FROM_BLOB_TEST_BIN) --disasm $(DISASM_BIN)

test-cli-golden: disasm
	$(SHELL) tests/ci_cli_golden.sh

test: test-unit test-cli-golden

clean:
	rm -rf $(BIN_DIR) out.grph out.disasm.txt
