# Project Summary R1

## Repo purpose

Portable DSP framework with pipeline:

`DSL -> compiler -> blob -> runtime`

Current implementation is still early-stage but now includes:

- Python DSL parser/compiler for a simple single-node Gain graph
- C blob parser and disassembler
- C runtime sizing pass for graph memory requirements
- Gain module ABI implementation
- Focused C tests and a CLI golden test

## Current important files

- `tools/compiler/compiler.py`
- `tools/compiler/dsl_parser.py`
- `tools/cli.py`
- `runtime/loader/blob.h`
- `runtime/loader/blob.c`
- `runtime/loader/blob_cursor.h`
- `runtime/loader/blob_cursor.c`
- `runtime/engine/graph_instance.h`
- `runtime/engine/graph_instance.c`
- `runtime/runtime_types.h`
- `modules/module_abi.h`
- `modules/gain/gain.c`
- `modules/gain/gain.h`
- `tests/test_disasm_golden.c`
- `tests/test_graph_memory_requirements.c`
- `tests/test_blob_cursor.c`
- `tests/golden/gain_chain.disasm.txt`
- `tests/ci_cli_golden.sh`
- `Makefile`

## Implemented behavior

### Blob pipeline

Blob support now includes required sections:

- `REQUIRES`
- `GRAPH_CONFIG`
- `HEAPS`
- `BUFFERS`
- `NODES`
- `SCHEDULE`
- `PARAM_DEFAULTS`

`GRAPH_CONFIG` is explicit and required.

Current payload:

- `u32 sample_rate_hz`
- `u32 block_multiple_N`

Implementation details:

- `runtime/loader/blob.h` defines `GRPH_SECT_GRAPH_CONFIG = 8`
- `runtime/loader/blob.h` exposes `graph_config` and parsed `graph_config_values`
- `runtime/loader/blob.c` validates `GRAPH_CONFIG` as required, payload size `8`, values non-zero
- disassembler prints:
  - `[GRAPH_CONFIG]`
  - `sample_rate_hz=...`
  - `block_multiple_N=...`

### Compiler

`tools/compiler/compiler.py` currently compiles only a simple one-node graph:

- exactly one input IO
- exactly one output IO
- exactly one node
- module must be `Gain`

Compiler behavior:

- emits `GRAPH_CONFIG`
- uses `sample_rate_hz = io.sample_rate_khz * 1000`
- currently hardcodes `block_multiple_N = 1`
- still uses `io.block` directly for buffer frames
- emits deterministic IDs:
  - node id `10`
  - heap ids `1,2,3`
  - buffer ids `1,2`

Important compiler limitation:

- no multi-node compilation
- no multirate
- no runtime engine work
- no binding logic

### Runtime sizing pass

Added:

- `runtime/engine/graph_instance.h`
- `runtime/engine/graph_instance.c`
- `runtime/runtime_types.h`

API:

```c
GraphStatus graph_get_memory_requirements(
    const BlobView *blob,
    const ModuleRegistry *registry,
    GraphMemoryRequirements *out_req,
    uint32_t *heap_required_bytes,
    uint32_t heap_required_count);
```

Current `GraphStatus` values:

- `GRAPH_STATUS_OK = 0`
- `GRAPH_STATUS_BAD_ARG = 1`
- `GRAPH_STATUS_INVALID_BLOB = 2`
- `GRAPH_STATUS_MODULE_NOT_FOUND = 3`
- `GRAPH_STATUS_INSUFFICIENT_HEAP_COUNT = 4`

Current `ModuleRegistry`:

```c
typedef struct ModuleRegistry {
  const AweModuleDescriptor *const *modules;
  uint32_t module_count;
} ModuleRegistry;
```

What the sizing pass does:

- validates args
- requires blob sections:
  - `HEAPS`
  - `BUFFERS`
  - `NODES`
  - `SCHEDULE`
- computes:
  - `num_heaps`
  - `num_buffers`
  - `num_nodes`
  - `schedule_length`
  - `metadata_bytes`
  - `module_state_bytes`
- resolves modules by `module_id` from the registry
- uses module descriptor:
  - `state_bytes`
  - `state_align`
- derives per-heap required sizes
- validates `heap_required_count` if output array provided
- performs no allocation
- performs no binding

Metadata sizing currently includes:

- `RuntimeHeap[num_heaps]`
- `RuntimeBufferView[num_buffers]`
- `RuntimeNodeInstance[num_nodes]`
- `RuntimeScheduleEntry[schedule_length]`
- per-node input binding arrays
- per-node output binding arrays

### BlobCursor refactor

Added low-level blob parsing helper:

- `runtime/loader/blob_cursor.h`
- `runtime/loader/blob_cursor.c`

API:

- `cursor_init`
- `cursor_set_offset`
- `cursor_skip`
- `cursor_read_u32`
- `cursor_get_u32_at`
- `cursor_slice`

Design rule already adopted:

- keep direct `rd_u32()` for trivial local fixed-field reads
- use `BlobCursor` for indexed records and variable-length section walking

Refactored to use `BlobCursor`:

- `load_heap_info(...)`
- `find_heap_index(...)`
- `load_buffer_record(...)`
- `load_node_info(...)`
- `find_node_index(...)`
- `schedule_op_count(...)`
- `load_schedule_op(...)`
- `accumulate_module_state_bytes(...)`

Comments were added to document record layout assumptions.

## Tests and validation

### Current tests

- `tests/test_disasm_golden.c`
  - builds in-memory reference blob
  - disassembles it
  - compares exactly against golden text

- `tests/test_graph_memory_requirements.c`
  - builds a simple reference blob
  - parses it
  - creates a one-module registry using Gain descriptor
  - checks:
    - `num_heaps == 3`
    - `num_buffers == 2`
    - `num_nodes == 1`
    - `schedule_length == 1`
    - `module_state_bytes > 0`
    - heap sizes match expected values

- `tests/test_blob_cursor.c`
  - focused HEAPS parsing test
  - verifies:
    - valid parse succeeds
    - out-of-range index fails
    - truncated payload fails

- `tests/ci_cli_golden.sh`
  - runs:
    - `python tools/cli.py build examples/gain_chain.dsl -o out.grph`
    - `./bin/disasm out.grph > out.disasm.txt`
    - `diff -u --strip-trailing-cr tests/golden/gain_chain.disasm.txt out.disasm.txt`

### Build/test notes

`Makefile` currently includes:

- `disasm`
- `test-unit`
- `test-cli-golden`
- `test`

Important Windows note:

- `bin/*.exe` was sometimes locked by another process, causing `Permission denied`
- workaround used successfully: build test executables into `temp_validate/`
- when using PowerShell redirection for text comparison, avoid UTF-16 output issues; `cmd /c ... > file` was used when exact ASCII diff mattered

Most recent focused validation succeeded using temporary executables for:

- `tests/test_blob_cursor.c`
- `tests/test_graph_memory_requirements.c`
- `tests/test_disasm_golden.c`

## Current blob/disasm shape

Current golden disassembly includes:

- `Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE PARAM_DEFAULTS`
- `[GRAPH_CONFIG]`
  - `sample_rate_hz=48000`
  - `block_multiple_N=1`

Golden file:

- `tests/golden/gain_chain.disasm.txt`

## Important implementation details and caveats

### Gain init blob mismatch

This is important.

Compiler currently emits Gain init blob as:

```text
u32 param_id (=1) + f32 gain_db
```

See:

- `tools/compiler/compiler.py`

But `modules/gain/gain.c` `gain_init()` currently expects:

- raw 4-byte float payload only

So compiler and module ABI are not aligned for actual init binding yet.

Disassembler currently interprets the compiler-emitted 8-byte init blob and prints:

- `init: gain_db=-6.0`

This means:

- docs/examples/disasm/golden are aligned with compiler/disassembler
- runtime module init is not aligned with that encoding yet

### Heap kind mismatch

This is another important inconsistency.

Current implementation uses:

- heap id 1 kind 4 = IO
- heap id 2 kind 3 = STATE
- heap id 3 kind 2 = PARAM

See:

- `tools/compiler/compiler.py`
- `runtime/loader/blob.c` `heap_kind_name()`

But `spec/blob_v1.md` currently documents different heap kind values.

### State sizing mismatch

Compiler and golden/disasm still show Gain node:

- `state_bytes=16`
- `align=16`

But actual module descriptor in `modules/gain/gain.c` reports:

- `state_bytes = sizeof(GainState)`
- `state_align = _Alignof(GainState)`

And runtime sizing uses the descriptor values, not blob `state_bytes`.

So docs and compiler examples do not currently match actual module descriptor sizing.

### Dynamic allocation policy mismatch

The project/runtime direction now strongly prohibits dynamic allocation for runtime behavior.

But `modules/module_abi.h` still includes:

- `alloc_fn`
- `free_fn`

And `spec/abi_v1.md` still describes them.

This conflicts with `spec/runtime_design.md`, which says no dynamic allocation anywhere in runtime lifecycle.

### Protocol doc status

`spec/protocol_v1.md` describes a protocol including:

- `LOAD_GRAPH`
- `ACTIVATE`

But runtime protocol implementation does not exist yet.

Treat that document as aspirational, not a description of current code.

## Review of spec consistency already performed

A document review was done without changing docs. Main disagreements found:

1. `spec/blob_v1.md` heap kind enum disagrees with compiler/disassembler implementation.
2. `spec/disasm_format_v1.md` is stale and does not include `GRAPH_CONFIG`.
3. `spec/disasm_format_v1.md` and compiler/golden still assume Gain node state `16/16`, but runtime sizing now trusts module descriptor values.
4. `spec/abi_v1.md` allows allocator hooks while `spec/runtime_design.md` forbids dynamic allocation.
5. Gain init blob encoding differs between compiler/disassembler and `modules/gain/gain.c`.
6. `spec/protocol_v1.md` is ahead of implementation.
7. Optional richer two-node example in docs is not supported by current compiler.

No doc changes were made after this review. The user explicitly wanted disagreements presented first.

## Good next steps for a new chat

If continuing implementation:

1. Decide whether to align Gain init blob encoding to:
   - raw `f32`
   - or `param_id + f32`

2. Decide the canonical heap kind enum and update either:
   - docs
   - compiler/disassembler

3. Decide whether compiler should keep emitting placeholder node `state_bytes/state_align` or derive them from module descriptors.

4. Update stale docs only after those decisions are approved.

5. If continuing runtime work, likely next milestone is graph binding:
   - bind metadata arena
   - bind module state arena
   - bind heaps
   - validate buffer ranges
   - instantiate node records

## Useful commands

Build/disasm golden path:

```powershell
python tools/cli.py build examples/gain_chain.dsl -o out.grph
.\bin\disasm.exe out.grph
```

Run focused temp builds if `bin/*.exe` is locked:

```powershell
New-Item -ItemType Directory -Force temp_validate | Out-Null
& 'C:\msys64\ucrt64\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror -I. -Imodules runtime/loader/blob_cursor.c tests/test_blob_cursor.c -o temp_validate\test_blob_cursor.exe
.\temp_validate\test_blob_cursor.exe
```

```powershell
New-Item -ItemType Directory -Force temp_validate | Out-Null
& 'C:\msys64\ucrt64\bin\gcc.exe' -std=c11 -Wall -Wextra -Werror -I. -Imodules runtime/loader/blob.c runtime/loader/blob_cursor.c runtime/engine/graph_instance.c modules/gain/gain.c tests/test_graph_memory_requirements.c -o temp_validate\test_graph_memory_requirements.exe
.\temp_validate\test_graph_memory_requirements.exe
```

## Current principle for parser refactors

Keep this rule:

- use `rd_u32()` for tiny obvious fixed-field reads
- use `BlobCursor` for indexed records and variable-length section walking

This avoids refactoring simple code into ceremony while still improving reviewability where byte walking was repetitive.
