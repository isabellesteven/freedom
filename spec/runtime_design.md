## Static Runtime Memory Model

The runtime uses a **static, host-provided memory model** suitable for embedded, real-time, and safety-oriented systems.

The runtime performs **no dynamic memory allocation** during graph sizing, graph binding, initialization, processing, parameter access, or teardown. In particular, the runtime must not call `malloc()`, `calloc()`, `realloc()`, `free()`, `alloca()`, or any equivalent hidden allocator.

All memory required for graph execution is supplied by the host environment.

### Memory classes

Runtime memory is divided into three classes:

1. **Metadata memory**
   Used for runtime control structures derived from the blob, including:

   * heap descriptors
   * buffer views
   * node instance records
   * schedule entries
   * per-node input/output binding arrays

2. **Module state memory**
   Used for per-instance module state blocks.
   State memory is carved from a host-provided state region during graph binding.

3. **Signal processing heaps**
   Used for audio buffers and working memory referenced by the graph blob.
   These heaps are provided explicitly by the host and bound to the logical heaps declared by the blob.

### Ownership and lifetime

The host owns all runtime memory.

The runtime may:

* validate host-provided memory regions
* partition metadata memory into internal structures
* partition module state memory into per-node state blocks
* bind logical heaps to host-provided heap regions
* zero or initialize bound memory deterministically as required

The runtime does not:

* acquire memory from the system allocator
* free memory
* resize memory regions
* move bound objects after binding

`graph_unbind()` or equivalent teardown logic may call module `deinit()` functions and clear internal bookkeeping, but must not release host-owned memory.

### Deterministic arena allocation

Within host-provided metadata memory and module state memory, the runtime may use a simple deterministic arena allocator.

This allocator:

* operates only on caller-supplied memory
* advances monotonically
* does not support free or compaction
* has deterministic time behavior
* fails cleanly when capacity is exceeded

This arena-based partitioning is considered part of static memory binding and is not dynamic allocation.

### Two-phase runtime initialization

Runtime initialization is split into two phases.

#### 1. Memory requirement query

The runtime inspects the graph blob and reports the minimum memory required to bind and execute the graph.

This includes:

* metadata bytes required
* module state bytes required
* number of logical heaps
* required bytes for each logical heap

Representative API:

```c
typedef struct {
    uint32_t metadata_bytes;
    uint32_t module_state_bytes;
    uint32_t num_heaps;
    uint32_t num_buffers;
    uint32_t num_nodes;
    uint32_t schedule_length;
} GraphMemoryRequirements;

GraphStatus graph_get_memory_requirements(
    const BlobView *blob,
    const ModuleRegistry *registry,
    GraphMemoryRequirements *out_req,
    uint32_t *heap_required_bytes,
    uint32_t heap_required_count);
```

#### 2. Graph binding

The runtime binds the blob into host-supplied memory and produces a runnable graph instance.

Representative API:

```c
typedef struct {
    uint32_t base_block_frames;
} RuntimeHostConfig;

typedef struct {
    void *metadata_mem;
    uint32_t metadata_mem_size;

    void *module_state_mem;
    uint32_t module_state_mem_size;

    void **heap_bases;
    const uint32_t *heap_sizes;
    uint32_t num_heaps;
} RuntimeMemoryConfig;

GraphStatus graph_bind_from_blob(
    const BlobView *blob,
    const ModuleRegistry *registry,
    const RuntimeHostConfig *host_cfg,
    const RuntimeMemoryConfig *mem_cfg,
    GraphInstance *out_graph);
```

Implementation status:

* currently implemented: `graph_get_memory_requirements(...)`
* planned, not yet implemented in this repo: `graph_bind_from_blob(...)`

During binding, the runtime:

* validates blob sections and counts
* validates that host memory satisfies reported requirements
* partitions metadata memory
* partitions module state memory
* binds logical heaps to host-provided heap regions
* validates buffer ranges against heap bounds
* initializes module instances
* loads the compiled schedule

### Block size contract

The graph blob provides `block_multiple_N` in `GRAPH_CONFIG`.

The host provides the base block size `B` through `RuntimeHostConfig`.

The runtime computes:

```text
block_frames = B × block_multiple_N
```

The runtime must not infer or allocate block-sized memory implicitly. Any required working memory must be accounted for explicitly in the blob-defined heaps and host-provided memory regions.

### Module requirements

Modules participating in this runtime model must comply with the following rules:

* `init()`, `process()`, `set_param()`, `get_param()`, and `deinit()` must not perform dynamic memory allocation.
* Module instance state must reside entirely within the state block assigned during graph binding, unless explicitly bound to a blob-defined heap region.
* `process()` must be deterministic with respect to memory behavior.
* Modules must not retain pointers to temporary stack objects supplied by the runtime.
* Any internal working memory required by a module must be accounted for either:

  * in the module state block, or
  * in explicitly assigned heap-backed memory defined by the graph/runtime contract

### Failure behavior

If the runtime detects insufficient metadata memory, insufficient module state memory, insufficient heap capacity, invalid alignment, invalid section contents, or out-of-range buffer references, graph binding must fail deterministically with an explicit error code.

Such failures must occur before graph processing begins.

### Runtime guarantees

Under this memory model, the runtime guarantees:

* no dynamic memory allocation
* no hidden background allocation
* no internal memory resizing
* deterministic memory binding behavior
* explicit failure on insufficient memory
* compatibility with statically allocated and linker-placed memory regions

### Notes for embedded integration

This model is intended to support integration patterns such as:

* statically declared arrays
* memory placed into specific linker sections
* separation of fast and slow SRAM regions
* external RAM used only for selected heaps
* startup-time graph binding followed by steady-state real-time execution

A representative host setup may provide:

* one metadata array
* one module state array
* one or more heap arrays placed in target-specific memory segments

The runtime binds the graph onto these regions without allocating memory itself.
