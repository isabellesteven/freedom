Key alignments with the blob spec:

* `block_frames = B × N`
* modules must support **variable block sizes**
* buffers defined by the blob
* runtime allocates node state
* parameters applied at **block boundary**
* heap-offset buffers allowed to alias
* pins are positional
* minimal assumptions about formats

This document is written to be **copy-paste ready for the repo**.

---

# Module ABI v1

## Purpose

Module ABI v1 defines the binary interface between the DSP runtime and signal-processing modules.

The ABI specifies:

* module discovery
* module descriptor structure
* module execution entry points
* buffer interfaces
* parameter handling
* runtime services available to modules

The ABI allows modules to be reused across runtimes and hardware targets.

---

# 1. ABI Version

```c
#define AWE_ABI_MAJOR 1
#define AWE_ABI_MINOR 0
```

Compatibility rule:

* Runtime **must reject** modules with mismatched `abi_major`.
* Runtime may accept modules where:

```text
module.abi_minor ≤ runtime.abi_minor
```

---

# 2. Runtime Block Model

The runtime host integration layer defines a **base block size**:

```
B = base block frames
```

Examples:

* DMA interrupt size
* audio callback size

The graph blob defines a block multiple:

```
N = block_multiple_N
```

Graph execution block size:

```
block_frames = B × N
```

Modules receive this value through the process context.

Modules **must not assume a fixed block size**.

---

# 3. Fundamental Types

All ABI structures use standard C types.

| Type       | Meaning                   |
| ---------- | ------------------------- |
| `uint32_t` | 32-bit unsigned integer   |
| `int32_t`  | 32-bit signed integer     |
| `float`    | IEEE-754 single precision |

---

# 4. Audio Formats

```c
typedef enum
{
    AWE_FMT_INVALID = 0,
    AWE_FMT_F32     = 1,
    AWE_FMT_S16     = 2

} AweSampleFormat;
```

---

# 5. Buffer Layout

Buffers are described by the runtime using **buffer views**.

Buffers may:

* reference heap storage
* alias other buffers
* be views into larger buffers

Modules must not assume exclusive ownership of buffer memory.

---

# 6. Buffer View

```c
typedef struct AweBufView
{
    void*    data;

    uint32_t frames;
    uint16_t channels;

    uint8_t  format;
    uint8_t  flags;

    uint16_t stride_bytes;
    uint16_t reserved;

} AweBufView;
```

Field descriptions:

| Field          | Meaning                                   |
| -------------- | ----------------------------------------- |
| `data`         | pointer to buffer memory                  |
| `frames`       | number of frames in this processing block |
| `channels`     | channel count                             |
| `format`       | audio format                              |
| `stride_bytes` | distance between consecutive samples      |

---

# 7. Process Context

The runtime provides execution information via a process context.

```c
typedef struct AweProcessCtx
{
    uint32_t sample_rate_hz;

    uint32_t block_frames;

    uint64_t block_index;

} AweProcessCtx;
```

Field descriptions:

| Field            | Meaning                              |
| ---------------- | ------------------------------------ |
| `sample_rate_hz` | graph sample rate                    |
| `block_frames`   | frames per graph execution (`B × N`) |
| `block_index`    | incrementing execution counter       |

---

# 8. Runtime API Table

Modules must not link directly against runtime symbols.

Runtime services are provided via an API table.

```c
typedef struct AweRuntimeApi
{
    uint32_t api_bytes;

    uint32_t abi_major;
    uint32_t abi_minor;

    void* (*memcpy_fn)(void*, const void*, uint32_t);
    void* (*memset_fn)(void*, int, uint32_t);

    void (*log_fn)(uint32_t level, const char* msg);

    void* (*alloc_fn)(uint32_t bytes, uint32_t align);
    void  (*free_fn)(void*);

} AweRuntimeApi;
```

Usage rules:

| Function  | Allowed in process() |
| --------- | -------------------- |
| memcpy_fn | yes                  |
| memset_fn | yes                  |
| log_fn    | no                   |
| alloc_fn  | no                   |
| free_fn   | no                   |

Memory allocation may be used during `init()`.

---

# 9. Module Capabilities

Modules declare capabilities using flags.

```c
typedef enum
{
    AWE_CAP_NONE                = 0,
    AWE_CAP_INPLACE_IO0         = 1 << 0,
    AWE_CAP_OUT0_ALIASES_ANY_IN = 1 << 1,
    AWE_CAP_FIXED_BLOCK_ONLY    = 1 << 2

} AweModuleCaps;
```

Meaning:

| Flag                | Meaning                             |
| ------------------- | ----------------------------------- |
| INPLACE_IO0         | output 0 may alias input 0          |
| OUT0_ALIASES_ANY_IN | output may alias any input          |
| FIXED_BLOCK_ONLY    | module requires specific block size |

---

# 10. Status Codes

```c
typedef enum
{
    AWE_OK        = 0,
    AWE_EINVAL    = -1,
    AWE_ENOTSUP   = -2,
    AWE_ESTATE    = -3,
    AWE_EINTERNAL = -4

} AweStatus;
```

---

# 11. Module VTable

Modules expose a function table.

```c
typedef struct AweModuleVTable
{
    AweStatus (*init)(
        void* state,
        const AweRuntimeApi* api,
        const void* init_blob,
        uint32_t init_bytes,
        const AweProcessCtx* ctx);

    AweStatus (*process)(
        void* state,
        const AweBufView* inputs,
        uint32_t n_in,
        AweBufView* outputs,
        uint32_t n_out,
        const AweProcessCtx* ctx);

    AweStatus (*set_param)(
        void* state,
        uint32_t param_id,
        const void* data,
        uint32_t size_bytes);

    AweStatus (*get_param)(
        void* state,
        uint32_t param_id,
        void* data,
        uint32_t* size_bytes);

    void (*deinit)(
        void* state);

} AweModuleVTable;
```

---

# 12. Module Descriptor

Each module exposes a descriptor describing its properties.

```c
typedef struct AweModuleDescriptor
{
    uint32_t desc_bytes;

    uint32_t module_id;

    uint16_t ver_major;
    uint16_t ver_minor;

    uint16_t abi_major;
    uint16_t abi_minor;

    uint32_t caps;

    uint32_t state_bytes;
    uint32_t state_align;

    uint16_t n_in;
    uint16_t n_out;

    uint32_t fixed_block_frames;

    AweModuleVTable vtable;

} AweModuleDescriptor;
```

Field descriptions:

| Field              | Meaning                                   |
| ------------------ | ----------------------------------------- |
| module_id          | unique module identifier                  |
| state_bytes        | per-instance state size                   |
| state_align        | required alignment                        |
| n_in               | number of input pins                      |
| n_out              | number of output pins                     |
| fixed_block_frames | required block size if `FIXED_BLOCK_ONLY` |

---

# 13. Module Discovery

Modules export a descriptor lookup function.

```c
const AweModuleDescriptor*
awe_get_module_descriptor(
    uint32_t abi_major,
    uint32_t abi_minor);
```

Returns:

* descriptor pointer if compatible
* NULL otherwise

---

# 14. Node State

The runtime allocates module state memory.

Blob values for `state_offset` are ignored.

State memory is allocated sequentially from the **state heap**.

---

# 15. Parameters

Parameters are identified by a module-defined identifier:

```
param_id : uint32_t
```

Parameter encoding:

| Type  | Encoding         |
| ----- | ---------------- |
| f32   | IEEE float       |
| i32   | signed integer   |
| u32   | unsigned integer |
| bytes | raw              |

---

# 16. Parameter Update Model

Parameter updates occur at **graph block boundaries**.

Execution order:

```
apply queued parameter updates
execute graph schedule
```

Modules must ensure `set_param()` is **real-time safe**.

---

# 17. Pin Model

Pins are positional.

Inputs passed to process:

```
inputs[0 .. n_in-1]
```

Outputs:

```
outputs[0 .. n_out-1]
```

DSL shorthand:

```
node.out → outputs[0]
node.in  → inputs[0]
```

---

# 18. Real-Time Safety Rules

Inside `process()` modules must not:

* allocate memory
* acquire locks
* perform blocking I/O
* call operating system APIs

Modules may:

* update internal state
* perform bounded computation
* call RT-safe runtime helpers

---

# 19. Minimal Example

Gain module:

```
inputs  = 1
outputs = 1
param_id 1 = gain_db
```

Processing:

```
out[n] = in[n] × gain
```

---

# 20. ABI Compliance

A module is ABI compliant if:

* `abi_major` matches runtime
* descriptor fields are valid
* `process()` obeys RT rules
* parameters validate sizes
* aliasing rules follow declared capabilities

---
