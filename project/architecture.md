System Architecture

DSL
    ↓
IR
    ↓
Compiler
    ↓
Binary Blob
    ↓
Runtime Engine


Good decision. The **Audio Weaver–style base block `B` with graph multiple `N`** is one of the most portable schemes across embedded and desktop systems. It keeps **platform I/O stable** while still allowing **algorithm-friendly block sizes** inside the framework.

Below is a precise restatement of the decision and its architectural consequences so it’s unambiguous when you later update `blob_v1.md` and `abi_v1.md`.

---

# Block Size Model (Design Decision)

## Definitions

* **Base block size `B`**

  * Defined by the **host integration layer**.
  * Corresponds to the DMA / audio callback size.
  * Fixed for the lifetime of the runtime.

Example:

```
DMA block = 48 frames
B = 48
```

---

* **Graph block multiple `N`**

  * Defined in the **graph blob**.
  * Specifies how many base blocks are accumulated before graph execution.

```
Graph block size = N × B
```

Example:

```
B = 48
N = 4

Graph block = 192 samples
```

---

# Runtime Execution Model

## Base callback

The host integration layer receives audio every `B` frames:

```
callback()
{
    receive B frames
    accumulate

    if accumulated == N*B
        run graph
}
```

Graph execution frequency:

```
graph_rate = sample_rate / (B × N)
```

Example:

```
48kHz
B = 48
N = 4

graph_rate = 48000 / 192 = 250 Hz
```

---

# Data Flow

### Input

```
DMA → base buffer → accumulator → graph input buffer
```

### Output

```
graph output buffer → splitter → DMA
```

Buffering logic will be implemented in runtime later.

---

# Blob Representation

The blob must include **graph configuration parameters**.

Add a new section or header fields:

```
GRAPH_CONFIG
    sample_rate_hz
    block_multiple_N
```

The runtime already knows `B`.

Therefore:

```
graph_block_frames = B × N
```

Modules receive this value through `ProcessCtx`.

---

# ABI Behavior

Modules will see:

```
ctx->block_frames = B × N
```

Modules must:

* accept **variable block sizes**
* optionally declare constraints

Example descriptor fields already defined:

```
AWE_CAP_FIXED_BLOCK_ONLY
fixed_block_frames
```

---

# Benefits of This Design

### 1. Host integration stays simple

DMA / driver configuration **never changes** when a graph is loaded.

This is extremely important for:

* I2S
* USB audio
* ALSA
* CoreAudio
* embedded codec drivers

---

### 2. Portable across platforms

Works on:

* embedded MCU
* Linux ALSA
* Windows WASAPI
* macOS CoreAudio

No platform-specific graph requirements.

---

### 3. DSP algorithms can use larger blocks

Many algorithms prefer larger blocks:

```
FFT
AEC
noise reduction
ML inference
beamforming
```

---

### 4. Deterministic timing

The runtime still runs on the base interrupt/callback cadence.

---

# Known Tradeoffs

These are accepted by design.

### Latency increases with N

Latency contribution:

```
algorithmic latency = (N × B) / sample_rate
```

Example:

```
B=48
N=4
48kHz

latency = 192 / 48000 ≈ 4 ms
```

---

### Memory increases

Accumulator buffers required:

```
input_accum
output_split
```

---

### Some modules may prefer different sizes

Example:

```
FFT size = 256
B = 48
```

Not divisible → requires internal buffering.

This is acceptable and common.

---

# Runtime Components Affected

This decision mainly impacts:

```
IO adapter
block accumulator
block splitter
scheduler trigger
```

The **graph scheduler itself remains unchanged**.

---

# What Does NOT Change

* heap-offset buffer model
* module ABI
* graph schedule
* blob structure (except graph config fields)
* protocol

This is an **orthogonal change**.

---

# Future Extensions

This design naturally supports future features.

## Multi-rate (v2)

Domains may use:

```
domain_block = k × graph_block
```

---

## Multicore (v2)

Work units remain whole blocks:

```
node execution units
```

---

# Implementation Note

Runtime needs two small helpers:

### Input accumulator

```
B blocks → accumulate → graph block
```

### Output splitter

```
graph block → emit B blocks
```

These can be implemented using:

```
ring buffers
or
ping-pong buffers
```

---

# Next Step

When we rewrite `blob_v1.md`, we will add:

```
GRAPH_CONFIG
    sample_rate_hz
    block_multiple_N
```

and remove any implicit assumptions about fixed block size.

---

If you'd like, the **next useful step** would be to design the **IO Adapter layer** in the runtime, because it is the component that actually implements this `B` → `N×B` accumulation behavior and it touches several parts of the system (buffers, scheduling, latency accounting).

