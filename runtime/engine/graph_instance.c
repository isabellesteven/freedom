#include "runtime/engine/graph_instance.h"

#include <stddef.h>
#include <string.h>

#include "runtime/runtime_types.h"

typedef struct BlobHeapInfo {
  uint32_t heap_id;
  uint32_t heap_bytes;
  uint32_t heap_align;
} BlobHeapInfo;

typedef struct BlobNodeInfo {
  uint32_t node_id;
  uint32_t module_id;
  uint32_t state_align;
} BlobNodeInfo;

typedef struct BlobScheduleOp {
  uint32_t node_id;
  uint8_t n_in;
  uint8_t n_out;
} BlobScheduleOp;

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24));
}

static uint32_t align_up_u32(uint32_t value, uint32_t align) {
  uint32_t mask;
  if (align <= 1u) {
    return value;
  }
  mask = align - 1u;
  return (value + mask) & ~mask;
}

static uint32_t metadata_align(void) {
  return (uint32_t)sizeof(void *);
}

static int load_heap_info(const BlobView *blob, uint32_t index,
                          BlobHeapInfo *out_heap) {
  const uint8_t *p;
  uint32_t count;
  if (!blob || !blob->heaps || !out_heap || blob->heaps->payload_bytes < 4u) {
    return 0;
  }
  p = blob->heaps->payload;
  count = rd_u32(p);
  if (index >= count) {
    return 0;
  }
  p += 4u + (size_t)index * 16u;
  if (blob->heaps->payload_bytes < 4u + ((index + 1u) * 16u)) {
    return 0;
  }
  out_heap->heap_id = rd_u32(p + 0);
  out_heap->heap_bytes = rd_u32(p + 8);
  out_heap->heap_align = rd_u32(p + 12);
  return 1;
}

static int find_heap_index(const BlobView *blob, uint32_t heap_id,
                           uint32_t *out_index) {
  const uint8_t *p;
  uint32_t count;
  uint32_t i;
  if (!blob || !blob->heaps || !out_index || blob->heaps->payload_bytes < 4u) {
    return 0;
  }
  p = blob->heaps->payload;
  count = rd_u32(p);
  p += 4;
  for (i = 0; i < count; ++i) {
    if ((size_t)(p - blob->heaps->payload) + 16u > blob->heaps->payload_bytes) {
      return 0;
    }
    if (rd_u32(p) == heap_id) {
      *out_index = i;
      return 1;
    }
    p += 16;
  }
  return 0;
}

static int load_buffer_record(const BlobView *blob, uint32_t index,
                              uint32_t *out_heap_id, uint32_t *out_offset,
                              uint32_t *out_size) {
  const uint8_t *p;
  uint32_t count;
  if (!blob || !blob->buffers || !out_heap_id || !out_offset || !out_size ||
      blob->buffers->payload_bytes < 4u) {
    return 0;
  }
  p = blob->buffers->payload;
  count = rd_u32(p);
  if (index >= count) {
    return 0;
  }
  p += 4u + (size_t)index * 32u;
  if (blob->buffers->payload_bytes < 4u + ((index + 1u) * 32u)) {
    return 0;
  }
  *out_heap_id = rd_u32(p + 8);
  *out_offset = rd_u32(p + 12);
  *out_size = rd_u32(p + 16);
  return 1;
}

static int load_node_info(const BlobView *blob, uint32_t index,
                          BlobNodeInfo *out_node) {
  const uint8_t *p;
  const uint8_t *end;
  uint32_t count;
  uint32_t i;
  if (!blob || !blob->nodes || !out_node || blob->nodes->payload_bytes < 4u) {
    return 0;
  }
  p = blob->nodes->payload;
  end = blob->nodes->payload + blob->nodes->payload_bytes;
  count = rd_u32(p);
  p += 4;
  if (index >= count) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    uint32_t init_bytes;
    if ((size_t)(end - p) < 32u) {
      return 0;
    }
    init_bytes = rd_u32(p + 24);
    if ((size_t)(end - p) < 32u + init_bytes) {
      return 0;
    }
    if (i == index) {
      out_node->node_id = rd_u32(p + 0);
      out_node->module_id = rd_u32(p + 4);
      out_node->state_align = rd_u32(p + 20);
      return 1;
    }
    p += 32u + init_bytes;
  }
  return 0;
}

static int find_node_index(const BlobView *blob, uint32_t node_id,
                           uint32_t *out_index) {
  const uint8_t *p;
  const uint8_t *end;
  uint32_t count;
  uint32_t i;
  if (!blob || !blob->nodes || !out_index || blob->nodes->payload_bytes < 4u) {
    return 0;
  }
  p = blob->nodes->payload;
  end = blob->nodes->payload + blob->nodes->payload_bytes;
  count = rd_u32(p);
  p += 4;
  for (i = 0; i < count; ++i) {
    uint32_t init_bytes;
    if ((size_t)(end - p) < 32u) {
      return 0;
    }
    init_bytes = rd_u32(p + 24);
    if ((size_t)(end - p) < 32u + init_bytes) {
      return 0;
    }
    if (rd_u32(p + 0) == node_id) {
      *out_index = i;
      return 1;
    }
    p += 32u + init_bytes;
  }
  return 0;
}

static int schedule_op_count(const BlobView *blob, uint32_t *out_count) {
  if (!blob || !blob->schedule || !out_count || blob->schedule->payload_bytes < 4u) {
    return 0;
  }
  *out_count = rd_u32(blob->schedule->payload);
  return 1;
}

static int load_schedule_op(const BlobView *blob, uint32_t index,
                            BlobScheduleOp *out_op) {
  const uint8_t *p;
  const uint8_t *end;
  uint32_t count;
  uint32_t i;
  if (!blob || !blob->schedule || !out_op || blob->schedule->payload_bytes < 4u) {
    return 0;
  }
  p = blob->schedule->payload;
  end = blob->schedule->payload + blob->schedule->payload_bytes;
  count = rd_u32(p);
  p += 4;
  if (index >= count) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    uint8_t n_in;
    uint8_t n_out;
    size_t op_bytes;
    if ((size_t)(end - p) < 8u) {
      return 0;
    }
    n_in = p[1];
    n_out = p[2];
    op_bytes = 8u + (size_t)(n_in + n_out) * 4u;
    if ((size_t)(end - p) < op_bytes) {
      return 0;
    }
    if (i == index) {
      out_op->n_in = n_in;
      out_op->n_out = n_out;
      out_op->node_id = rd_u32(p + 4);
      return 1;
    }
    p += op_bytes;
  }
  return 0;
}

static const AweModuleDescriptor *lookup_module(const ModuleRegistry *registry,
                                                uint32_t module_id) {
  uint32_t i;
  if (!registry || !registry->modules) {
    return NULL;
  }
  for (i = 0; i < registry->module_count; ++i) {
    const AweModuleDescriptor *desc = registry->modules[i];
    if (desc && desc->module_id == module_id) {
      return desc;
    }
  }
  return NULL;
}

static GraphStatus validate_required_sections(const BlobView *blob) {
  if (!blob || !blob->heaps || !blob->buffers || !blob->nodes || !blob->schedule) {
    return GRAPH_STATUS_INVALID_BLOB;
  }
  return GRAPH_STATUS_OK;
}

static GraphStatus derive_heap_sizes(const BlobView *blob,
                                     uint32_t *heap_required_bytes,
                                     uint32_t heap_required_count,
                                     uint32_t *out_heap_count) {
  uint32_t heap_count;
  uint32_t buffer_count;
  uint32_t heap_index;
  if (!blob || !blob->heaps || !blob->buffers || !out_heap_count ||
      blob->heaps->payload_bytes < 4u || blob->buffers->payload_bytes < 4u) {
    return GRAPH_STATUS_INVALID_BLOB;
  }

  heap_count = rd_u32(blob->heaps->payload);
  *out_heap_count = heap_count;

  if (heap_required_bytes != NULL) {
    uint32_t i;
    if (heap_required_count < heap_count) {
      return GRAPH_STATUS_INSUFFICIENT_HEAP_COUNT;
    }
    for (i = 0; i < heap_count; ++i) {
      BlobHeapInfo heap_info;
      if (!load_heap_info(blob, i, &heap_info)) {
        return GRAPH_STATUS_INVALID_BLOB;
      }
      heap_required_bytes[i] = heap_info.heap_bytes;
    }
  }

  buffer_count = rd_u32(blob->buffers->payload);
  for (heap_index = 0; heap_index < buffer_count; ++heap_index) {
    uint32_t heap_id;
    uint32_t offset_bytes;
    uint32_t size_bytes;
    uint32_t target_index;
    uint32_t required_bytes;
    BlobHeapInfo heap_info;

    if (!load_buffer_record(blob, heap_index, &heap_id, &offset_bytes, &size_bytes)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (!find_heap_index(blob, heap_id, &target_index)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (!load_heap_info(blob, target_index, &heap_info)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    required_bytes = offset_bytes + size_bytes;
    if (required_bytes > heap_info.heap_bytes) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (heap_required_bytes != NULL && required_bytes > heap_required_bytes[target_index]) {
      heap_required_bytes[target_index] = required_bytes;
    }
  }

  return GRAPH_STATUS_OK;
}

static GraphStatus accumulate_module_state_bytes(const BlobView *blob,
                                                 const ModuleRegistry *registry,
                                                 uint32_t *out_node_count,
                                                 uint32_t *out_state_bytes) {
  const uint8_t *p;
  const uint8_t *end;
  uint32_t count;
  uint32_t total;
  uint32_t i;
  if (!blob || !blob->nodes || !registry || !out_node_count || !out_state_bytes ||
      blob->nodes->payload_bytes < 4u) {
    return GRAPH_STATUS_INVALID_BLOB;
  }

  p = blob->nodes->payload;
  end = blob->nodes->payload + blob->nodes->payload_bytes;
  count = rd_u32(p);
  p += 4;
  total = 0u;

  for (i = 0; i < count; ++i) {
    uint32_t module_id;
    uint32_t state_align;
    uint32_t init_bytes;
    const AweModuleDescriptor *desc;

    if ((size_t)(end - p) < 32u) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    module_id = rd_u32(p + 4);
    state_align = rd_u32(p + 20);
    init_bytes = rd_u32(p + 24);
    if ((size_t)(end - p) < 32u + init_bytes) {
      return GRAPH_STATUS_INVALID_BLOB;
    }

    desc = lookup_module(registry, module_id);
    if (!desc) {
      return GRAPH_STATUS_MODULE_NOT_FOUND;
    }

    total = align_up_u32(total,
                         desc->state_align != 0u ? desc->state_align : state_align);
    total += desc->state_bytes;
    p += 32u + init_bytes;
  }

  if (p != end) {
    return GRAPH_STATUS_INVALID_BLOB;
  }

  *out_node_count = count;
  *out_state_bytes = total;
  return GRAPH_STATUS_OK;
}

static GraphStatus compute_binding_bytes(const BlobView *blob,
                                         uint32_t node_count,
                                         uint32_t *out_binding_bytes,
                                         uint32_t *out_schedule_count) {
  uint32_t schedule_count;
  uint32_t node_index;
  uint32_t binding_bytes;
  uint32_t op_index;
  GraphStatus status;
  if (!blob || !out_binding_bytes || !out_schedule_count) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (!schedule_op_count(blob, &schedule_count)) {
    return GRAPH_STATUS_INVALID_BLOB;
  }

  binding_bytes = 0u;
  for (node_index = 0; node_index < node_count; ++node_index) {
    BlobNodeInfo node_info;
    if (!load_node_info(blob, node_index, &node_info)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    for (op_index = 0; op_index < schedule_count; ++op_index) {
      BlobScheduleOp op;
      uint32_t referenced_node_index;
      if (!load_schedule_op(blob, op_index, &op)) {
        return GRAPH_STATUS_INVALID_BLOB;
      }
      if (!find_node_index(blob, op.node_id, &referenced_node_index)) {
        return GRAPH_STATUS_INVALID_BLOB;
      }
      if (referenced_node_index == node_index) {
        binding_bytes = align_up_u32(binding_bytes, metadata_align());
        binding_bytes += op.n_in * (uint32_t)sizeof(RuntimeBufferBinding);
        binding_bytes = align_up_u32(binding_bytes, metadata_align());
        binding_bytes += op.n_out * (uint32_t)sizeof(RuntimeBufferBinding);
        break;
      }
    }
  }

  status = GRAPH_STATUS_OK;
  *out_binding_bytes = binding_bytes;
  *out_schedule_count = schedule_count;
  return status;
}

GraphStatus graph_get_memory_requirements(
    const BlobView *blob,
    const ModuleRegistry *registry,
    GraphMemoryRequirements *out_req,
    uint32_t *heap_required_bytes,
    uint32_t heap_required_count) {
  GraphStatus status;
  uint32_t heap_count;
  uint32_t node_count;
  uint32_t buffer_count;
  uint32_t schedule_count;
  uint32_t state_bytes;
  uint32_t binding_bytes;
  uint32_t metadata_bytes;

  if (!blob || !registry || !out_req) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (!registry->modules || registry->module_count == 0u) {
    return GRAPH_STATUS_BAD_ARG;
  }

  status = validate_required_sections(blob);
  if (status != GRAPH_STATUS_OK) {
    return status;
  }

  status = derive_heap_sizes(blob, heap_required_bytes, heap_required_count, &heap_count);
  if (status != GRAPH_STATUS_OK) {
    return status;
  }

  status = accumulate_module_state_bytes(blob, registry, &node_count, &state_bytes);
  if (status != GRAPH_STATUS_OK) {
    return status;
  }

  buffer_count = rd_u32(blob->buffers->payload);
  status = compute_binding_bytes(blob, node_count, &binding_bytes, &schedule_count);
  if (status != GRAPH_STATUS_OK) {
    return status;
  }

  metadata_bytes = 0u;
  metadata_bytes = align_up_u32(metadata_bytes, metadata_align());
  metadata_bytes += heap_count * (uint32_t)sizeof(RuntimeHeap);
  metadata_bytes = align_up_u32(metadata_bytes, metadata_align());
  metadata_bytes += buffer_count * (uint32_t)sizeof(RuntimeBufferView);
  metadata_bytes = align_up_u32(metadata_bytes, metadata_align());
  metadata_bytes += node_count * (uint32_t)sizeof(RuntimeNodeInstance);
  metadata_bytes = align_up_u32(metadata_bytes, metadata_align());
  metadata_bytes += schedule_count * (uint32_t)sizeof(RuntimeScheduleEntry);
  metadata_bytes = align_up_u32(metadata_bytes, metadata_align());
  metadata_bytes += binding_bytes;

  memset(out_req, 0, sizeof(*out_req));
  out_req->metadata_bytes = metadata_bytes;
  out_req->module_state_bytes = state_bytes;
  out_req->num_heaps = heap_count;
  out_req->num_buffers = buffer_count;
  out_req->num_nodes = node_count;
  out_req->schedule_length = schedule_count;

  return GRAPH_STATUS_OK;
}
