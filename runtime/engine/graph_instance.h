#ifndef FREEDOM_RUNTIME_ENGINE_GRAPH_INSTANCE_H
#define FREEDOM_RUNTIME_ENGINE_GRAPH_INSTANCE_H

#include <stdint.h>

#include "modules/module_abi.h"
#include "runtime/loader/blob.h"
#include "runtime/runtime_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef grph_blob_view BlobView;

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

typedef struct GraphMemoryRequirements {
  uint32_t metadata_bytes;
  uint32_t module_state_bytes;
  uint32_t num_heaps;
  uint32_t num_buffers;
  uint32_t num_nodes;
  uint32_t schedule_length;
} GraphMemoryRequirements;

typedef struct ModuleRegistry {
  const AweModuleDescriptor *const *modules;
  uint32_t module_count;
} ModuleRegistry;

typedef struct RuntimeHostConfig {
  uint32_t base_block_frames;
} RuntimeHostConfig;

typedef struct RuntimeMemoryConfig {
  void *metadata_mem;
  uint32_t metadata_mem_size;

  void *module_state_mem;
  uint32_t module_state_mem_size;

  void **heap_bases;
  const uint32_t *heap_sizes;
  uint32_t num_heaps;
} RuntimeMemoryConfig;

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

GraphStatus graph_get_memory_requirements(
    const BlobView *blob,
    const ModuleRegistry *registry,
    GraphMemoryRequirements *out_req,
    uint32_t *heap_required_bytes,
    uint32_t heap_required_count);

GraphStatus graph_bind_from_blob(const BlobView *blob,
                                 const ModuleRegistry *registry,
                                 const RuntimeHostConfig *host_cfg,
                                 const RuntimeMemoryConfig *mem_cfg,
                                 GraphInstance *out_graph);

GraphStatus graph_unbind(GraphInstance *graph);

GraphStatus graph_process(GraphInstance *graph, uint64_t block_index);

#ifdef __cplusplus
}
#endif

#endif
