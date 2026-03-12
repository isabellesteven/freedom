# 3) Reference graph example + expected disassembly

## 3.1 Reference DSL: `examples/gain_chain.dsl`

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

* Block = 48 frames, 1 channel, f32 (4 bytes)
* Buffer size per block = `48 * 1 * 4 = 192 bytes`
* Use IO ping-pong: mic and spk are **slots=2**
* One internal buffer between `g1.out` and `spk` is not needed if you allow `g1` to write directly into the output buffer. For this reference, we’ll keep it explicit and simple:

  * `mic_buf` is IO input
  * `spk_buf` is IO output
  * `Gain` reads from mic_buf and writes to spk_buf (no intermediate)

So buffers:

* `buf 1`: mic (OWNED, heap IO, offset 0, size 192, slots 2)
* `buf 2`: spk (OWNED, heap IO, offset 384, size 192, slots 2)
  (2 slots means 192*2=384 bytes reserved per buffer; offsets chosen accordingly)

Heaps:

* heap 1 (IO) size 768, align 16 (enough for both ping-pong buffers)
* heap 2 (STATE) size e.g. 256, align 16
* heap 3 (PARAM) size e.g. 256, align 16 (or param pages separate)

Node:

* node_id 10: module Gain, state_bytes e.g. 16, align 16, init blob contains `gain_db=-6.0`

Schedule:

* 1 op: CALL node 10 with in=[1], out=[2]

## 3.2 Expected disassembly output (text)

This is what your C disassembler should print for the compiled blob, in a stable deterministic format (example):

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

`state_bytes` and `align` above are example blob fields as printed by the current disassembler.
Current runtime sizing uses module descriptors for actual state sizing.

You don’t have to use these exact IDs—what matters is that:

* it’s deterministic,
* it clearly shows heaps/buffers/nodes/schedule,
* and it matches how the runtime will execute.

---

## 3.3 Reference “slightly richer” graph (future example, not supported by the current compiler)

If you want a better test of buffer reuse, add 2 nodes:

```txt
graph "gain_eq"
  io input  mic : f32@48k block=48 ch=1
  io output spk : f32@48k block=48 ch=1

  node g1 : Gain(gain_db=-6.0)
  node g2 : Gain(gain_db=+3.0)

  connect mic -> g1.in
  connect g1.out -> g2.in
  connect g2.out -> spk
end
```

Expected compiler behavior (v1):

* Allocate one intermediate buffer for `g1.out/g2.in` OR alias/in-place if `Gain` supports it.
* This becomes your first lifetime-reuse/in-place test.

---
