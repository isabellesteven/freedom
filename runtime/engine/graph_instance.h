#ifndef FREEDOM_RUNTIME_ENGINE_GRAPH_INSTANCE_H
#define FREEDOM_RUNTIME_ENGINE_GRAPH_INSTANCE_H

#include <stdint.h>

#include "modules/module_abi.h"
#include "runtime/loader/blob.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef grph_blob_view BlobView;

typedef enum GraphStatus {
  GRAPH_STATUS_OK = 0,
  GRAPH_STATUS_BAD_ARG = 1,
  GRAPH_STATUS_INVALID_BLOB = 2,
  GRAPH_STATUS_MODULE_NOT_FOUND = 3,
  GRAPH_STATUS_INSUFFICIENT_HEAP_COUNT = 4
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

GraphStatus graph_get_memory_requirements(
    const BlobView *blob,
    const ModuleRegistry *registry,
    GraphMemoryRequirements *out_req,
    uint32_t *heap_required_bytes,
    uint32_t heap_required_count);

#ifdef __cplusplus
}
#endif

#endif
