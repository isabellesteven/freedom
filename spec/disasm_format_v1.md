# 3) Textual Graph Views: Human Disassembly and Canonical Lowered IR

This document defines two textual views of a compiled graph blob:

1. a human-readable disassembly format for inspection, and
2. a canonical lowered IR text format for deterministic equivalence checking.

The canonical IR is a normalized lowered view derived from the compiled graph and the blob. It is not a source-level DSL representation, and it is not intended to reconstruct original DSL names or formatting.

## 3.1 Purpose

The canonical lowered IR text exists to support:

- compiler inspection after lowering
- blob disassembly into a normalized semantic form
- round-trip equivalence checking of `IR -> blob -> disassemble -> IR1`

Two canonical IR texts are equivalent only if they match exactly after normalization, including all canonical IDs and fields defined below.

## 3.2 Human Disassembly vs Canonical IR

Human disassembly may include comments, annotations, decoded convenience renderings, or spacing chosen for readability.

Canonical IR must omit non-semantic comments and must follow the exact formatting, ordering, and field rules defined in this document.

In particular:

- comments are non-canonical
- original DSL names are non-canonical and are not preserved in the blob
- decoded `init:` lines are informative only unless promoted by a future spec revision
- canonical IDs are semantic and must match exactly

## 3.3 Canonical Section Order

Canonical IR must always contain the following sections in this exact order:

1. Header line
2. `Sections:` line
3. `[REQUIRES]`
4. `[GRAPH_CONFIG]`
5. `[HEAPS]`
6. `[BUFFERS]`
7. `[NODES]`
8. `[SCHEDULE]`
9. `[PARAM_DEFAULTS]`
10. `[CRC32]`

All sections must be printed even when empty. Empty sections must use deterministic zero-count rendering.

## 3.4 Normalization Rules

Canonical IR normalization rules:

- Section order is fixed.
- Record order within a section is fixed and deterministic.
- `heap id`, `buf id`, and `node id` are canonical and must match exactly.
- Integer counts, sizes, offsets, alignments, slot counts, frame counts, and channel counts are printed in decimal unless otherwise specified.
- Module IDs are printed in hexadecimal as `0x%08X`.
- Field names and keywords are fixed and case-sensitive.
- Canonical IR contains no comments.
- Canonical IR contains no DSL symbol names.
- Whitespace is normalized and must not carry semantic meaning beyond token separation.

## 3.5 Canonical Equivalence

Canonical IR equivalence is exact textual equivalence after normalization.

The following are semantic and must match:

- required module set
- graph config values
- heap declarations
- buffer declarations
- alias relationships
- node declarations
- schedule operations and buffer bindings
- parameter default payloads
- CRC32 status

The following are non-semantic and must not appear in canonical IR:

- comments
- explanatory annotations
- original DSL names
- source formatting choices

## 3.6 Canonical IR Record Forms

The canonical IR must use the following section forms.

### Header

```txt
GRPH v<major>.<minor> abi=<abi_tag> uuid=<uuid>
```

### Sections Line

```txt
Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE PARAM_DEFAULTS
```

### REQUIRES

```txt
[REQUIRES]
count=<N>
module <module_id> ver=<major>.<minor> caps=<caps_hex>
...
```

Where:

- `<module_id>` is printed as `0x%08X`
- `<caps_hex>` is printed as `0x%08X`

### GRAPH_CONFIG

```txt
[GRAPH_CONFIG]
sample_rate_hz=<hz>
block_multiple_N=<N>
```

### HEAPS

```txt
[HEAPS]
count=<N>
heap id=<id> kind=<kind> bytes=<bytes> align=<align>
...
```

Where:

- `kind` is a symbolic heap kind such as `IO`, `STATE`, or `PARAM`

### BUFFERS

```txt
[BUFFERS]
count=<N>
buf id=<id> type=<OWNED|ALIAS> alias_of=<buf_id_or_0> fmt=<fmt> heap=<heap_id> off=<offset> size=<size> slots=<slots> stride=<stride> base=<base> ch=<channels> frames=<frames>
...
```

Where:

- `alias_of=0` means the buffer owns its storage
- `alias_of=<buf_id>` means the buffer is a logical view into storage originating from another buffer
- alias buffers retain their own logical view metadata, including `size`, `stride`, `ch`, and `frames`
- future versions may permit alias buffers whose logical shape differs from the underlying storage owner

### NODES

```txt
[NODES]
count=<N>
node id=<id> module=<module_id> state_heap=<heap_id> state_bytes=<bytes> align=<align> init_bytes=<bytes> param_block_bytes=<bytes>
...
```

Human disassembly may additionally print decoded `init:` lines under a node record.

Decoded `init:` lines are informative only and are not part of canonical IR in v1.

### SCHEDULE

```txt
[SCHEDULE]
op_count=<N>
<index>: CALL node=<node_id> in=[<buf_id> ...] out=[<buf_id> ...]
...
```

Where:

- input list order is module input pin order after lowering
- output list order is module output pin order after lowering

### PARAM_DEFAULTS

```txt
[PARAM_DEFAULTS]
count=<N>
node=<node_id> bytes=<bytes> data=<normalized_data>
...
```

In v1, `data=<normalized_data>` must use a deterministic textual rendering. When a typed rendering is defined for the payload, that typed rendering should be used. Otherwise unknown payloads must be rendered as raw hexadecimal bytes.

### CRC32

```txt
[CRC32]
status=<ok|bad>
```

## 3.7 Reference DSL Example

Reference DSL examples may still be included in this document for illustration, but they are informative only. The canonical IR definition is the normative representation for equivalence checking.

### Reference DSL: `examples/gain_chain.dsl`

```txt
graph "gain_chain"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)

  connect mic -> g1.in
  connect g1.out -> spk
end
```

### Assumptions for v1 reference compilation

- Block = 48 frames, 1 channel, f32 (4 bytes)
- Buffer size per block = `48 * 1 * 4 = 192 bytes`
- Use IO ping-pong: mic and spk are `slots=2`
- One internal buffer between `g1.out` and `spk` is not needed if you allow `g1` to write directly into the output buffer. For this reference, keep it explicit and simple:
  - `mic_buf` is IO input
  - `spk_buf` is IO output
  - `Gain` reads from mic_buf and writes to spk_buf

So buffers:

- `buf 1`: mic (OWNED, heap IO, offset 0, size 192, slots 2)
- `buf 2`: spk (OWNED, heap IO, offset 384, size 192, slots 2)

Heaps:

- heap 1 (IO) size 768, align 16
- heap 2 (STATE) size e.g. 256, align 16
- heap 3 (PARAM) size e.g. 256, align 16

Node:

- node_id 10: module Gain, state_bytes e.g. 16, align 16, init blob contains `gain_db=-6.0`

Schedule:

- 1 op: CALL node 10 with in=[1], out=[2]

## 3.8 Human Disassembly Example

This is an example of a readable disassembly view. Comments and decoded convenience renderings are allowed here and are not part of canonical IR.

```txt
GRPH v1.0  abi=PCNA  uuid=00000000-0000-0000-0000-000000000001
Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE PARAM_DEFAULTS

[REQUIRES]
  count=1
  module 0x00001001 ver=1.0 caps=0x00000000   ; Gain

[GRAPH_CONFIG]
  sample_rate_hz=48000
  block_multiple_N=1

[HEAPS]
  heap id=1 kind=IO    bytes=768 align=16
  heap id=2 kind=STATE bytes=256 align=16
  heap id=3 kind=PARAM bytes=256 align=16

[BUFFERS]
  buf id=1 type=OWNED fmt=F32 heap=1 off=0   size=192 slots=2 stride=0 base=0 ch=1 frames=48   ; mic
  buf id=2 type=OWNED fmt=F32 heap=1 off=384 size=192 slots=2 stride=0 base=0 ch=1 frames=48   ; spk

[NODES]
  node id=10 module=0x00001001 state_heap=2 state_bytes=16 align=16 init_bytes=8 param_block_bytes=4
    init: gain_db=-6.0
    params_default: (param_id=1 f32 -6.0)

[SCHEDULE]
  op_count=1
  0: CALL node=10 in=[1] out=[2]

[PARAM_DEFAULTS]
  node=10 bytes=4  data=f32(-6.0)

[CRC32]
  ok
```

`state_bytes` and `align` above are example blob fields as printed by the current disassembler. Current runtime sizing uses module descriptors for actual state sizing.

## 3.9 Canonical IR Example

This is the corresponding canonical lowered IR view for the same graph.

```txt
GRPH v1.0 abi=PCNA uuid=00000000-0000-0000-0000-000000000001
Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE PARAM_DEFAULTS

[REQUIRES]
count=1
module 0x00001001 ver=1.0 caps=0x00000000

[GRAPH_CONFIG]
sample_rate_hz=48000
block_multiple_N=1

[HEAPS]
count=3
heap id=1 kind=IO bytes=768 align=16
heap id=2 kind=STATE bytes=256 align=16
heap id=3 kind=PARAM bytes=256 align=16

[BUFFERS]
count=2
buf id=1 type=OWNED alias_of=0 fmt=F32 heap=1 off=0 size=192 slots=2 stride=0 base=0 ch=1 frames=48
buf id=2 type=OWNED alias_of=0 fmt=F32 heap=1 off=384 size=192 slots=2 stride=0 base=0 ch=1 frames=48

[NODES]
count=1
node id=10 module=0x00001001 state_heap=2 state_bytes=16 align=16 init_bytes=8 param_block_bytes=4

[SCHEDULE]
op_count=1
0: CALL node=10 in=[1] out=[2]

[PARAM_DEFAULTS]
count=1
node=10 bytes=4 data=f32(-6.0)

[CRC32]
status=ok
```

## 3.10 Multi-Node Canonical Example

This example is illustrative and shows canonical schedule formatting for a multi-node graph with fan-in.

```txt
GRPH v1.0 abi=PCNA uuid=00000000-0000-0000-0000-000000000002
Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE PARAM_DEFAULTS

[REQUIRES]
count=2
module 0x00001001 ver=1.0 caps=0x00000000
module 0x00001002 ver=1.0 caps=0x00000000

[GRAPH_CONFIG]
sample_rate_hz=48000
block_multiple_N=1

[HEAPS]
count=3
heap id=1 kind=IO bytes=1152 align=16
heap id=2 kind=STATE bytes=256 align=16
heap id=3 kind=PARAM bytes=256 align=16

[BUFFERS]
count=4
buf id=1 type=OWNED alias_of=0 fmt=F32 heap=1 off=0 size=192 slots=2 stride=0 base=0 ch=1 frames=48
buf id=2 type=OWNED alias_of=0 fmt=F32 heap=1 off=384 size=192 slots=2 stride=0 base=0 ch=1 frames=48
buf id=3 type=OWNED alias_of=0 fmt=F32 heap=1 off=768 size=192 slots=2 stride=0 base=0 ch=1 frames=48
buf id=4 type=ALIAS alias_of=3 fmt=F32 heap=1 off=768 size=192 slots=2 stride=0 base=0 ch=1 frames=48

[NODES]
count=2
node id=10 module=0x00001001 state_heap=2 state_bytes=16 align=16 init_bytes=8 param_block_bytes=4
node id=20 module=0x00001002 state_heap=2 state_bytes=4 align=4 init_bytes=0 param_block_bytes=0

[SCHEDULE]
op_count=2
0: CALL node=10 in=[1] out=[2]
1: CALL node=20 in=[1 2] out=[3]

[PARAM_DEFAULTS]
count=1
node=10 bytes=4 data=f32(-6.0)

[CRC32]
status=ok
```

Current compiler/runtime limits do not constrain the canonical IR design. In particular, canonical alias records already permit future logical-view aliasing where aliased buffers may differ in shape from the underlying storage owner.
