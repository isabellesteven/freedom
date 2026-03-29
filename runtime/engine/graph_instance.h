#ifndef FREEDOM_RUNTIME_ENGINE_GRAPH_INSTANCE_H
#define FREEDOM_RUNTIME_ENGINE_GRAPH_INSTANCE_H

/*
 * Runtime graph instance API.
 *
 * This interface is the host-facing entry point for sizing, binding, executing,
 * and unbinding a compiled graph blob using caller-provided memory only.
 */

#include <stdint.h>

#include "modules/module_abi.h"
#include "runtime/engine/module_registry.h"
#include "runtime/loader/blob.h"
#include "runtime/runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef grph_blob_view BlobView;

/* Public status codes returned by the runtime entry points below. */
typedef enum GraphStatus {
  GRAPH_STATUS_OK = 0,
  GRAPH_STATUS_BAD_ARG = 1,
  GRAPH_STATUS_INVALID_BLOB = 2,
  GRAPH_STATUS_MODULE_NOT_FOUND = 3,
  GRAPH_STATUS_INSUFFICIENT_HEAP_COUNT = 4,
  GRAPH_STATUS_INSUFFICIENT_METADATA_MEMORY = 5,
  GRAPH_STATUS_INSUFFICIENT_STATE_MEMORY = 6,
  GRAPH_STATUS_INSUFFICIENT_HEAP_MEMORY = 7,
  GRAPH_STATUS_MODULE_INIT_FAILED = 8,
  GRAPH_STATUS_MODULE_PARAM_FAILED = 9,
  GRAPH_STATUS_NOT_BOUND = 10,
  GRAPH_STATUS_PROCESS_FAILED = 11
} GraphStatus;

/*
 * Aggregate memory requirements for binding one graph instance.
 *
 * All sizes are minima derived from the blob and the module registry. The host
 * remains responsible for providing the backing memory regions.
 */
typedef struct GraphMemoryRequirements {
  uint32_t metadata_bytes;
  uint32_t module_state_bytes;
  uint32_t num_heaps;
  uint32_t num_buffers;
  uint32_t num_nodes;
  uint32_t schedule_length;
} GraphMemoryRequirements;

/*
 * Host execution parameters that are not stored directly in the blob.
 *
 * base_block_frames is the host integration block size B. The runtime combines
 * it with GRAPH_CONFIG.block_multiple_N to compute graph->block_frames = B * N.
 */
typedef struct RuntimeHostConfig {
  uint32_t base_block_frames;
} RuntimeHostConfig;

/*
 * Caller-owned memory used to bind a graph instance.
 *
 * The runtime never allocates or frees these regions. All memory provided here
 * must remain valid until graph_unbind() returns.
 */
typedef struct RuntimeMemoryConfig {
  void *metadata_mem;
  uint32_t metadata_mem_size;

  void *module_state_mem;
  uint32_t module_state_mem_size;

  void **heap_bases;
  const uint32_t *heap_sizes;
  uint32_t num_heaps;
} RuntimeMemoryConfig;

/*
 * Bound runtime graph instance.
 *
 * Callers may inspect these fields for diagnostics, but the runtime owns their
 * internal bookkeeping after a successful bind. A zeroed instance is unbound.
 */
typedef struct GraphInstance {
  RuntimeHeap *heaps;
  RuntimeBufferView *buffers;
  RuntimeNodeInstance *nodes;
  RuntimeScheduleEntry *schedule;

  uint32_t num_heaps;
  uint32_t num_buffers;
  uint32_t num_nodes;
  uint32_t schedule_length;

  uint32_t sample_rate_hz;
  uint32_t block_multiple_n;
  uint32_t block_frames;

  void *metadata_mem;
  uint32_t metadata_mem_size;
  void *module_state_mem;
  uint32_t module_state_mem_size;
  uint8_t is_bound;
} GraphInstance;

/*
 * Compute the minimum host memory needed to bind a validated graph blob.
 *
 * Preconditions:
 * - blob, registry, and out_req are non-NULL
 * - registry contains every module referenced by the blob
 *
 * On success:
 * - out_req is fully populated
 * - heap_required_bytes, when provided, receives one required size per heap
 *
 * On failure:
 * - no host memory is modified
 */
GraphStatus graph_get_memory_requirements(
    const BlobView *blob,
    const ModuleRegistry *registry,
    GraphMemoryRequirements *out_req,
    uint32_t *heap_required_bytes,
    uint32_t heap_required_count);

/*
 * Bind a parsed graph blob into caller-provided memory and initialize modules.
 *
 * Preconditions:
 * - blob, registry, host_cfg, mem_cfg, and out_graph are non-NULL
 * - mem_cfg points to host-owned metadata/state/heaps with sufficient capacity
 * - out_graph may be uninitialized; it is zeroed deterministically on entry
 *
 * On success:
 * - out_graph describes a runnable graph instance
 * - no graph processing has occurred yet
 *
 * On failure:
 * - already-initialized modules are deinitialized
 * - host memory is not freed or reclaimed
 * - out_graph is left in the unbound state
 */
GraphStatus graph_bind_from_blob(const BlobView *blob,
                                 const ModuleRegistry *registry,
                                 const RuntimeHostConfig *host_cfg,
                                 const RuntimeMemoryConfig *mem_cfg,
                                 GraphInstance *out_graph);

/*
 * Deinitialize a previously bound graph instance.
 *
 * graph_unbind() calls module deinit() where needed, clears all runtime
 * bookkeeping, and never frees caller-owned memory.
 */
GraphStatus graph_unbind(GraphInstance *graph);

/*
 * Execute exactly one graph block using the blob-defined schedule.
 *
 * Preconditions:
 * - graph is non-NULL and successfully bound
 *
 * Semantics:
 * - one call processes one graph block, not one host base block
 * - schedule order is taken directly from the bound blob
 * - execution stops at the first module process failure
 */
GraphStatus graph_process(GraphInstance *graph, uint64_t block_index);

#ifdef __cplusplus
}
#endif

#endif
