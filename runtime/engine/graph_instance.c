#include "runtime/engine/graph_instance.h"

#include <stddef.h>
#include <string.h>

#include "runtime/loader/blob_cursor.h"
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

/* Keep direct little-endian reads for small, local field access.
   Use BlobCursor for indexed records and variable-length section walks. */
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
  BlobCursor cur;
  BlobCursor record;
  uint32_t count;
  if (!blob || !blob->heaps || !out_heap) {
    return 0;
  }
  if (!cursor_init(&cur, blob->heaps->payload, blob->heaps->payload_bytes) ||
      !cursor_read_u32(&cur, &count) || index >= count) {
    return 0;
  }
  if (!cursor_slice(&cur, index * 16u, 16u, &record)) {
    return 0;
  }
  /* HEAPS record: u32 heap_id, u32 reserved, u32 heap_bytes, u32 heap_align */
  if (!cursor_get_u32_at(&record, 0u, &out_heap->heap_id) ||
      !cursor_get_u32_at(&record, 8u, &out_heap->heap_bytes) ||
      !cursor_get_u32_at(&record, 12u, &out_heap->heap_align)) {
    return 0;
  }
  return 1;
}

static int find_heap_index(const BlobView *blob, uint32_t heap_id,
                           uint32_t *out_index) {
  BlobCursor cur;
  BlobCursor record;
  uint32_t count;
  uint32_t i;
  uint32_t candidate_id;
  if (!blob || !blob->heaps || !out_index) {
    return 0;
  }
  if (!cursor_init(&cur, blob->heaps->payload, blob->heaps->payload_bytes) ||
      !cursor_read_u32(&cur, &count)) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    if (!cursor_slice(&cur, i * 16u, 16u, &record) ||
        !cursor_get_u32_at(&record, 0u, &candidate_id)) {
      return 0;
    }
    if (candidate_id == heap_id) {
      *out_index = i;
      return 1;
    }
  }
  return 0;
}

static int load_buffer_record(const BlobView *blob, uint32_t index,
                              uint32_t *out_heap_id, uint32_t *out_offset,
                              uint32_t *out_size) {
  BlobCursor cur;
  BlobCursor record;
  uint32_t count;
  if (!blob || !blob->buffers || !out_heap_id || !out_offset || !out_size) {
    return 0;
  }
  if (!cursor_init(&cur, blob->buffers->payload, blob->buffers->payload_bytes) ||
      !cursor_read_u32(&cur, &count) || index >= count) {
    return 0;
  }
  if (!cursor_slice(&cur, index * 32u, 32u, &record)) {
    return 0;
  }
  /* BUFFERS record: u32 id, u8/u8/u16, u32 heap_id, u32 offset, u32 size, ... */
  if (!cursor_get_u32_at(&record, 8u, out_heap_id) ||
      !cursor_get_u32_at(&record, 12u, out_offset) ||
      !cursor_get_u32_at(&record, 16u, out_size)) {
    return 0;
  }
  return 1;
}

static int load_node_info(const BlobView *blob, uint32_t index,
                          BlobNodeInfo *out_node) {
  BlobCursor cur;
  BlobCursor header;
  uint32_t count;
  uint32_t i;
  uint32_t init_bytes;
  uint32_t record_bytes;
  if (!blob || !blob->nodes || !out_node) {
    return 0;
  }
  if (!cursor_init(&cur, blob->nodes->payload, blob->nodes->payload_bytes) ||
      !cursor_read_u32(&cur, &count) || index >= count) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    if (!cursor_slice(&cur, 0u, 32u, &header) ||
        !cursor_get_u32_at(&header, 24u, &init_bytes)) {
      return 0;
    }
    record_bytes = 32u + init_bytes;
    if (!cursor_slice(&cur, 0u, record_bytes, &header)) {
      return 0;
    }
    if (i == index) {
      /* NODES header: node_id, module_id, state_heap_id, state_offset,
         state_bytes, state_align, init_bytes, param_block_bytes */
      if (!cursor_get_u32_at(&header, 0u, &out_node->node_id) ||
          !cursor_get_u32_at(&header, 4u, &out_node->module_id) ||
          !cursor_get_u32_at(&header, 20u, &out_node->state_align)) {
        return 0;
      }
      return 1;
    }
    if (!cursor_skip(&cur, record_bytes)) {
      return 0;
    }
  }
  return 0;
}

static int find_node_index(const BlobView *blob, uint32_t node_id,
                           uint32_t *out_index) {
  BlobCursor cur;
  BlobCursor header;
  uint32_t count;
  uint32_t i;
  uint32_t init_bytes;
  uint32_t record_node_id;
  uint32_t record_bytes;
  if (!blob || !blob->nodes || !out_index) {
    return 0;
  }
  if (!cursor_init(&cur, blob->nodes->payload, blob->nodes->payload_bytes) ||
      !cursor_read_u32(&cur, &count)) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    if (!cursor_slice(&cur, 0u, 32u, &header) ||
        !cursor_get_u32_at(&header, 24u, &init_bytes) ||
        !cursor_get_u32_at(&header, 0u, &record_node_id)) {
      return 0;
    }
    record_bytes = 32u + init_bytes;
    if (!cursor_slice(&cur, 0u, record_bytes, &header)) {
      return 0;
    }
    if (record_node_id == node_id) {
      *out_index = i;
      return 1;
    }
    if (!cursor_skip(&cur, record_bytes)) {
      return 0;
    }
  }
  return 0;
}

static int schedule_op_count(const BlobView *blob, uint32_t *out_count) {
  BlobCursor cur;
  if (!blob || !blob->schedule || !out_count) {
    return 0;
  }
  return cursor_init(&cur, blob->schedule->payload, blob->schedule->payload_bytes) &&
         cursor_read_u32(&cur, out_count);
}

static int load_schedule_op(const BlobView *blob, uint32_t index,
                            BlobScheduleOp *out_op) {
  BlobCursor cur;
  BlobCursor header;
  uint32_t count;
  uint32_t i;
  uint32_t node_id;
  uint32_t op_bytes;
  uint32_t io_words;
  if (!blob || !blob->schedule || !out_op || blob->schedule->payload_bytes < 4u) {
    return 0;
  }
  if (!cursor_init(&cur, blob->schedule->payload, blob->schedule->payload_bytes) ||
      !cursor_read_u32(&cur, &count) || index >= count) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    if (!cursor_slice(&cur, 0u, 8u, &header)) {
      return 0;
    }
    out_op->n_in = header.data[1];
    out_op->n_out = header.data[2];
    if (!cursor_get_u32_at(&header, 4u, &node_id)) {
      return 0;
    }
    io_words = (uint32_t)out_op->n_in + (uint32_t)out_op->n_out;
    op_bytes = 8u + (io_words * 4u);
    if (!cursor_slice(&cur, 0u, op_bytes, &header)) {
      return 0;
    }
    if (i == index) {
      /* SCHEDULE op header: op_type, n_in, n_out, flags, node_id */
      out_op->node_id = node_id;
      return 1;
    }
    if (!cursor_skip(&cur, op_bytes)) {
      return 0;
    }
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
  BlobCursor cur;
  BlobCursor header;
  uint32_t count;
  uint32_t total;
  uint32_t i;
  uint32_t module_id;
  uint32_t state_align;
  uint32_t init_bytes;
  uint32_t record_bytes;
  if (!blob || !blob->nodes || !registry || !out_node_count || !out_state_bytes ||
      blob->nodes->payload_bytes < 4u) {
    return GRAPH_STATUS_INVALID_BLOB;
  }

  if (!cursor_init(&cur, blob->nodes->payload, blob->nodes->payload_bytes) ||
      !cursor_read_u32(&cur, &count)) {
    return GRAPH_STATUS_INVALID_BLOB;
  }
  total = 0u;

  for (i = 0; i < count; ++i) {
    const AweModuleDescriptor *desc;
    if (!cursor_slice(&cur, 0u, 32u, &header) ||
        !cursor_get_u32_at(&header, 4u, &module_id) ||
        !cursor_get_u32_at(&header, 20u, &state_align) ||
        !cursor_get_u32_at(&header, 24u, &init_bytes)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    record_bytes = 32u + init_bytes;
    if (!cursor_slice(&cur, 0u, record_bytes, &header)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }

    desc = lookup_module(registry, module_id);
    if (!desc) {
      return GRAPH_STATUS_MODULE_NOT_FOUND;
    }

    total = align_up_u32(total,
                         desc->state_align != 0u ? desc->state_align : state_align);
    total += desc->state_bytes;
    if (!cursor_skip(&cur, record_bytes)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
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
