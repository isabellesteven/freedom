Below is a **project briefing / checkpoint document** you can save in the repo (e.g., `PROJECT_BRIEFING.md`). Its goal is that **months from now you can open it and immediately know the architecture, decisions, and next steps** without rereading this entire conversation.

---

# DSP Framework Project Briefing

## Purpose

Build a **simpler alternative to Audio Weaver** with:

* minimal copying
* simple integration
* dynamic module extensibility
* runtime pipeline loading
* real-time tuning
* portable execution framework

The design separates:

```
Signal Processing Design
        ↓
   Graph Compiler
        ↓
 Executable Graph Blob
        ↓
     Runtime Engine
```

The runtime remains stable while pipelines evolve.

---

# Core Design Principles

### 1. Strict separation

Separate:

**Framework runtime**

* scheduling
* buffer management
* module execution
* tuning protocol

from

**Signal processing design**

* DSL
* compiler
* optimization
* pipeline definition

This allows changing pipelines **without rebuilding firmware/framework**.

---

### 2. Pipeline = Data

Pipelines are distributed as a **binary graph package ("blob")**.

The runtime loads and instantiates the blob.

This avoids step-by-step graph construction like Audio Weaver.

---

### 3. Heap-offset buffer model

Audio buffers are **offsets into heap memory**, not separate allocations.

Benefits:

* zero-copy wiring
* aliasing/in-place support
* simple memory planning
* deterministic memory use

Buffer types:

```
OWNED   -> allocated memory
VIEW    -> slice of another buffer
ALIAS   -> same memory as another buffer
```

---

### 4. Store-and-activate deployment model

Graph loading:

```
LOAD_GRAPH → store blob
ACTIVATE   → instantiate + swap
```

On embedded targets:

```
Flash slot A/B
CRC verified
Swap active graph
```

On PC-native target:

```
File-backed slots
```

---

### 5. Real-time tuning

Protocol supports:

* parameter updates
* metrics
* graph loading

Parameter updates applied at **block boundary**.

---

### 6. Explicit module ABI

Modules are independent DSP components.

Each module implements a standard ABI:

```
init()
process()
set_param()
get_param()
deinit()
```

Modules access runtime services through a **Runtime API table**, not direct linking.

---

### 7. DSL → IR → Blob compilation

Toolchain flow:

```
DSL
 ↓
Graph IR
 ↓
Compiled IR
 ↓
Blob
```

The IR stage enables:

* validation
* scheduling
* buffer reuse
* optimization
* future multi-rate
* future multicore

---

# Technology Stack

## Runtime

Language: **C**

Responsibilities:

* blob loader
* scheduler
* module execution
* protocol endpoint
* heap buffer management

Targets:

* PC-native runtime (initial development)
* embedded runtime (later)

---

## PC Tooling

Language: **Python**

Responsibilities:

* DSL parsing
* graph IR
* compiler passes
* blob generation
* CLI tools

Distribution:

* frozen standalone executable
* no Python installation required

---

## Blob validator / disassembler

Language: **C**

Reasons:

* shared parsing logic with runtime
* prevents format drift
* fuzz-testable

---

# Graph Blob Format (v1)

Binary container with sections.

```
FileHeader
Sections
CRC32
```

Sections include:

```
REQUIRES
HEAPS
BUFFERS
NODES
SCHEDULE
PARAM_DEFAULTS
METADATA_MIN (optional)
```

Key properties:

* little endian
* section-based extensibility
* CRC protection
* unknown sections skipped

---

# Protocol v1

Binary framed protocol.

Frame:

```
magic
version
msg_type
seq
length
payload
crc32
```

Supported messages:

```
HELLO
LIST_MODULES
LOAD_GRAPH
ACTIVATE
SET_PARAM
SET_PARAM_BULK
GET_METRICS
ERROR
```

Transport-independent:

* TCP
* UART
* USB
* stdio

---

# Module ABI v1

Defines runtime ↔ module contract.

Includes:

```
ModuleDescriptor
ModuleVTable
BufferView
ProcessCtx
RuntimeApi
```

Process function:

```
process(state, inputs[], outputs[], ctx)
```

Real-time safety rules:

```
NO heap allocation
NO locks
NO I/O
NO unbounded work
```

---

# DSL

Text pipeline format.

Example:

```
graph "gain_chain"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)

  connect mic -> g1.in
  connect g1.out -> spk
end
```

Grammar defined in:

```
dsl_v1.ebnf
```

---

# Scheduling Model

v1:

* single-rate
* block-based execution
* flat schedule list

Example:

```
CALL node 10
CALL node 20
CALL node 30
```

Future:

```
multi-rate domains
multicore islands
```

---

# Buffer Strategy

Buffers stored in heaps:

```
audio_heap
state_heap
param_heap
```

Compiler performs:

```
lifetime analysis
buffer reuse
in-place aliasing
```

Double buffering used only where required:

* IO ping-pong
* async boundaries
* future multicore

---

# Block Size Design Decision

Block size **must not be hardcoded**.

Graph config includes:

```
sample_rate_hz
block_frames
```

Modules may optionally declare:

```
AWE_CAP_FIXED_BLOCK_ONLY
```

---

# Known v1 Assumptions

Explicit limitations:

```
single rate
single execution thread
planar audio buffers
graph immutable while active
```

All extendable later.

---

# Multi-Rate Support (planned v2)

Requires:

```
domain-aware IR
rate boundary modules
hierarchical scheduler
ring buffers
```

Blob extension:

```
DOMAINS section
node.domain_id
```

---

# Multicore Support (planned v2)

Requires:

```
graph partitioning
core assignment
barriers
handoff buffers
```

---

# Repository Structure

```
spec/
  blob_v1.md
  protocol_v1.md
  abi_v1.md
  dsl_v1.ebnf

runtime/
  engine/
  loader/
  protocol/
  platform/

modules/
  builtin/

tools/
  compiler/
  cli/

tests/
  golden/

examples/
```

---

# First Implementation Milestones

### M1 — Blob parser + disassembler

Implement:

```
blob_v1 parser
CRC validation
disassembly output
```

Golden test:

```
gain_chain.dsl
gain_chain.disasm.txt
```

---

### M2 — DSL → blob compiler

Python tool:

```
parse DSL
build IR
emit blob
```

---

### M3 — Native runtime

Implement:

```
heap allocator
node instantiation
schedule execution
```

---

### M4 — Parameter tuning

Implement:

```
SET_PARAM
block-boundary updates
```

---

### M5 — Store-and-activate

Implement:

```
A/B slots
LOAD_GRAPH
ACTIVATE
```

---

# Immediate Next Step

Use Codex to implement:

```
C blob parser + disassembler
```

Because it provides:

* stable reference
* validation for compiler output
* test harness

---

# Long-Term Vision

A small, portable DSP framework with:

```
runtime ≈ 20–40k LOC
compiler tooling
dynamic module ecosystem
graph-based design
```

Goals:

* easier than Audio Weaver
* easier integration
* smaller footprint
* flexible pipeline design

---

# Status Summary

| Area         | Status          |
| ------------ | --------------- |
| Architecture | Defined         |
| Blob format  | Defined         |
| Protocol     | Defined         |
| ABI          | Defined         |
| DSL grammar  | Defined         |
| IR design    | Agreed          |
| Runtime      | Not implemented |
| Compiler     | Not implemented |

---

# If Restarting Later

Resume by:

1. Implement **blob parser + disassembler**
2. Create **gain module**
3. Build **DSL → blob compiler**
4. Run pipeline on **native runtime**

---

If you'd like, I can also create a **much shorter “10-minute reorientation briefing”** that lets you re-enter the project even faster when you return to it.
