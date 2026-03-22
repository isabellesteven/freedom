#include "runtime/engine/graph_instance.h"

#include <stddef.h>
#include <string.h>

#include "runtime/common/mem_arena.h"
#include "runtime/loader/blob_cursor.h"

typedef struct BlobHeapInfo {
  uint32_t heap_id;
  uint32_t heap_bytes;
  uint32_t heap_align;
} BlobHeapInfo;

typedef struct BlobBufferInfo {
  uint32_t buffer_id;
  uint32_t heap_id;
  uint32_t offset_bytes;
  uint32_t size_bytes;
  uint16_t channels;
  uint16_t frames;
  uint16_t stride_bytes;
  uint8_t format;
  uint8_t buffer_type;
} BlobBufferInfo;

typedef struct BlobNodeInfo {
  uint32_t node_id;
  uint32_t module_id;
  uint32_t state_align;
  uint32_t init_bytes;
  const uint8_t *init_blob;
} BlobNodeInfo;

typedef struct BlobScheduleOp {
  uint32_t node_id;
  uint8_t n_in;
  uint8_t n_out;
  const uint8_t *in_buf_ids;
  const uint8_t *out_buf_ids;
} BlobScheduleOp;

typedef struct BlobParamDefaultsInfo {
  uint32_t node_id;
  uint32_t bytes;
  const uint8_t *data;
} BlobParamDefaultsInfo;

/* Keep direct little-endian reads for small, local field access.
   Use BlobCursor for indexed records and variable-length section walks. */
static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24));
}

static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
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

static int load_graph_config(const BlobView *blob, uint32_t *out_sample_rate_hz,
                             uint32_t *out_block_multiple_n) {
  if (!blob || !out_sample_rate_hz || !out_block_multiple_n || !blob->graph_config) {
    return 0;
  }
  *out_sample_rate_hz = blob->graph_config_values.sample_rate_hz;
  *out_block_multiple_n = blob->graph_config_values.block_multiple_n;
  return 1;
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
  /* HEAPS record: u32 heap_id, u32 heap_kind, u32 heap_bytes, u32 heap_align */
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

static int load_buffer_info(const BlobView *blob, uint32_t index,
                            BlobBufferInfo *out_buffer) {
  BlobCursor cur;
  BlobCursor record;
  uint32_t count;
  if (!blob || !blob->buffers || !out_buffer) {
    return 0;
  }
  if (!cursor_init(&cur, blob->buffers->payload, blob->buffers->payload_bytes) ||
      !cursor_read_u32(&cur, &count) || index >= count) {
    return 0;
  }
  if (!cursor_slice(&cur, index * 32u, 32u, &record)) {
    return 0;
  }
  /* BUFFERS record: u32 id, u8 type, u8 format, u16 flags, u32 heap_id, ... */
  if (!cursor_get_u32_at(&record, 0u, &out_buffer->buffer_id) ||
      !cursor_get_u32_at(&record, 8u, &out_buffer->heap_id) ||
      !cursor_get_u32_at(&record, 12u, &out_buffer->offset_bytes) ||
      !cursor_get_u32_at(&record, 16u, &out_buffer->size_bytes)) {
    return 0;
  }
  out_buffer->buffer_type = record.data[4];
  out_buffer->format = record.data[5];
  out_buffer->stride_bytes = rd_u16(record.data + 22u);
  out_buffer->channels = rd_u16(record.data + 28u);
  out_buffer->frames = rd_u16(record.data + 30u);
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
          !cursor_get_u32_at(&header, 20u, &out_node->state_align) ||
          !cursor_get_u32_at(&header, 24u, &out_node->init_bytes)) {
        return 0;
      }
      out_node->init_blob = header.data + 32u;
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

static int load_schedule_entry(const BlobView *blob, uint32_t index,
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
      out_op->node_id = node_id;
      out_op->in_buf_ids = header.data + 8u;
      out_op->out_buf_ids = header.data + 8u + ((uint32_t)out_op->n_in * 4u);
      return 1;
    }
    if (!cursor_skip(&cur, op_bytes)) {
      return 0;
    }
  }
  return 0;
}

static int find_param_defaults_for_node(const BlobView *blob, uint32_t node_id,
                                        BlobParamDefaultsInfo *out_info) {
  BlobCursor cur;
  BlobCursor entry;
  uint32_t count;
  uint32_t i;
  uint32_t bytes;
  uint32_t candidate_id;
  if (!blob || !blob->param_defaults || !out_info) {
    return 0;
  }
  if (!cursor_init(&cur, blob->param_defaults->payload,
                   blob->param_defaults->payload_bytes) ||
      !cursor_read_u32(&cur, &count)) {
    return 0;
  }
  for (i = 0; i < count; ++i) {
    if (!cursor_slice(&cur, 0u, 8u, &entry) ||
        !cursor_get_u32_at(&entry, 0u, &candidate_id) ||
        !cursor_get_u32_at(&entry, 4u, &bytes)) {
      return 0;
    }
    if (!cursor_slice(&cur, 0u, 8u + bytes, &entry)) {
      return 0;
    }
    if (candidate_id == node_id) {
      out_info->node_id = candidate_id;
      out_info->bytes = bytes;
      out_info->data = entry.data + 8u;
      return 1;
    }
    if (!cursor_skip(&cur, 8u + bytes)) {
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

static int find_runtime_buffer(RuntimeBufferView *buffers, uint32_t buffer_count,
                               uint32_t buffer_id, RuntimeBufferView **out_buffer) {
  uint32_t i;
  if (!buffers || !out_buffer) {
    return 0;
  }
  for (i = 0; i < buffer_count; ++i) {
    if (buffers[i].buffer_id == buffer_id) {
      *out_buffer = &buffers[i];
      return 1;
    }
  }
  return 0;
}

static GraphStatus validate_required_sections(const BlobView *blob) {
  if (!blob || !blob->heaps || !blob->buffers || !blob->nodes || !blob->schedule ||
      !blob->param_defaults || !blob->graph_config) {
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
    BlobBufferInfo buffer_info;
    uint32_t target_index;
    uint32_t required_bytes;
    BlobHeapInfo heap_info;

    if (!load_buffer_info(blob, heap_index, &buffer_info)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (!find_heap_index(blob, buffer_info.heap_id, &target_index)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (!load_heap_info(blob, target_index, &heap_info)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    required_bytes = buffer_info.offset_bytes + buffer_info.size_bytes;
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
      if (!load_schedule_entry(blob, op_index, &op)) {
        return GRAPH_STATUS_INVALID_BLOB;
      }
      if (!find_node_index(blob, op.node_id, &referenced_node_index)) {
        return GRAPH_STATUS_INVALID_BLOB;
      }
      if (referenced_node_index == node_index) {
        binding_bytes = align_up_u32(binding_bytes, metadata_align());
        binding_bytes += (uint32_t)op.n_in * (uint32_t)sizeof(RuntimeBufferBinding);
        binding_bytes = align_up_u32(binding_bytes, metadata_align());
        binding_bytes += (uint32_t)op.n_in * (uint32_t)sizeof(void *);
        binding_bytes = align_up_u32(binding_bytes, metadata_align());
        binding_bytes += (uint32_t)op.n_out * (uint32_t)sizeof(RuntimeBufferBinding);
        binding_bytes = align_up_u32(binding_bytes, metadata_align());
        binding_bytes += (uint32_t)op.n_out * (uint32_t)sizeof(void *);
        break;
      }
    }
  }

  *out_binding_bytes = binding_bytes;
  *out_schedule_count = schedule_count;
  return GRAPH_STATUS_OK;
}

static void unbind_initialized_nodes(GraphInstance *graph) {
  uint32_t i;
  if (!graph || !graph->nodes) {
    return;
  }
  for (i = 0; i < graph->num_nodes; ++i) {
    RuntimeNodeInstance *node = &graph->nodes[i];
    if (node->initialized && node->module && node->module->vtable.deinit) {
      node->module->vtable.deinit(node->state);
      node->initialized = 0u;
    }
  }
}

static GraphStatus bind_heaps(const BlobView *blob, const RuntimeMemoryConfig *mem_cfg,
                              GraphInstance *graph, uint32_t *heap_required_bytes) {
  uint32_t i;
  if (!blob || !mem_cfg || !graph || !graph->heaps || !heap_required_bytes) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (mem_cfg->num_heaps != graph->num_heaps || !mem_cfg->heap_bases ||
      !mem_cfg->heap_sizes) {
    return GRAPH_STATUS_INSUFFICIENT_HEAP_COUNT;
  }
  for (i = 0; i < graph->num_heaps; ++i) {
    BlobHeapInfo heap_info;
    if (!load_heap_info(blob, i, &heap_info)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (!mem_cfg->heap_bases[i] || mem_cfg->heap_sizes[i] < heap_required_bytes[i]) {
      return GRAPH_STATUS_INSUFFICIENT_HEAP_MEMORY;
    }
    graph->heaps[i].heap_id = heap_info.heap_id;
    graph->heaps[i].size_bytes = mem_cfg->heap_sizes[i];
    graph->heaps[i].base = mem_cfg->heap_bases[i];
  }
  return GRAPH_STATUS_OK;
}

static GraphStatus bind_buffers(const BlobView *blob, GraphInstance *graph) {
  uint32_t i;
  if (!blob || !graph || !graph->buffers || !graph->heaps) {
    return GRAPH_STATUS_BAD_ARG;
  }
  for (i = 0; i < graph->num_buffers; ++i) {
    BlobBufferInfo buffer_info;
    uint32_t heap_index;
    RuntimeBufferView *buffer;
    if (!load_buffer_info(blob, i, &buffer_info) ||
        !find_heap_index(blob, buffer_info.heap_id, &heap_index) ||
        heap_index >= graph->num_heaps) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    buffer = &graph->buffers[i];
    if (buffer_info.offset_bytes > graph->heaps[heap_index].size_bytes ||
        buffer_info.size_bytes >
            graph->heaps[heap_index].size_bytes - buffer_info.offset_bytes) {
      return GRAPH_STATUS_INSUFFICIENT_HEAP_MEMORY;
    }
    buffer->buffer_id = buffer_info.buffer_id;
    buffer->heap_index = heap_index;
    buffer->offset_bytes = buffer_info.offset_bytes;
    buffer->size_bytes = buffer_info.size_bytes;
    buffer->data = (uint8_t *)graph->heaps[heap_index].base + buffer_info.offset_bytes;
    buffer->channels = buffer_info.channels;
    buffer->frames = buffer_info.frames;
    buffer->stride_bytes = buffer_info.stride_bytes;
    buffer->format = buffer_info.format;
    buffer->buffer_type = buffer_info.buffer_type;
  }
  return GRAPH_STATUS_OK;
}

static GraphStatus bind_nodes(const BlobView *blob, const ModuleRegistry *registry,
                              MemArena *state_arena, GraphInstance *graph) {
  uint32_t i;
  if (!blob || !registry || !state_arena || !graph || !graph->nodes) {
    return GRAPH_STATUS_BAD_ARG;
  }
  for (i = 0; i < graph->num_nodes; ++i) {
    BlobNodeInfo node_info;
    BlobParamDefaultsInfo defaults_info;
    RuntimeNodeInstance *node;
    const AweModuleDescriptor *module;
    uint32_t state_align;
    if (!load_node_info(blob, i, &node_info)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    module = lookup_module(registry, node_info.module_id);
    if (!module) {
      return GRAPH_STATUS_MODULE_NOT_FOUND;
    }
    node = &graph->nodes[i];
    state_align = module->state_align != 0u ? module->state_align : node_info.state_align;
    node->state = mem_arena_alloc(state_arena, module->state_bytes, state_align);
    if (!node->state) {
      return GRAPH_STATUS_INSUFFICIENT_STATE_MEMORY;
    }
    memset(node->state, 0, module->state_bytes);
    node->node_id = node_info.node_id;
    node->module = module;
    node->init_blob = node_info.init_blob;
    node->init_bytes = node_info.init_bytes;
    node->initialized = 0u;
    if (find_param_defaults_for_node(blob, node_info.node_id, &defaults_info)) {
      node->param_defaults = defaults_info.data;
      node->param_defaults_bytes = defaults_info.bytes;
    }
  }
  return GRAPH_STATUS_OK;
}

static GraphStatus bind_schedule(const BlobView *blob, MemArena *metadata_arena,
                                 GraphInstance *graph) {
  uint32_t i;
  if (!blob || !metadata_arena || !graph || !graph->schedule || !graph->nodes ||
      !graph->buffers) {
    return GRAPH_STATUS_BAD_ARG;
  }
  for (i = 0; i < graph->schedule_length; ++i) {
    BlobScheduleOp op;
    uint32_t node_index;
    RuntimeNodeInstance *node;
    uint32_t j;
    if (!load_schedule_entry(blob, i, &op) ||
        !find_node_index(blob, op.node_id, &node_index) || node_index >= graph->num_nodes) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    node = &graph->nodes[node_index];
    if (node->module &&
        (node->module->n_in != op.n_in || node->module->n_out != op.n_out)) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    if (!node->inputs && op.n_in != 0u) {
      node->inputs = (RuntimeBufferBinding *)mem_arena_alloc(
          metadata_arena, (uint32_t)op.n_in * (uint32_t)sizeof(RuntimeBufferBinding),
          metadata_align());
      node->input_ptrs = (const void **)mem_arena_alloc(
          metadata_arena, (uint32_t)op.n_in * (uint32_t)sizeof(void *),
          metadata_align());
      if (!node->inputs || !node->input_ptrs) {
        return GRAPH_STATUS_INSUFFICIENT_METADATA_MEMORY;
      }
      node->num_inputs = op.n_in;
      for (j = 0; j < op.n_in; ++j) {
        RuntimeBufferView *buffer;
        if (!find_runtime_buffer(graph->buffers, graph->num_buffers,
                                 rd_u32(op.in_buf_ids + ((uint32_t)j * 4u)),
                                 &buffer)) {
          return GRAPH_STATUS_INVALID_BLOB;
        }
        node->inputs[j].buffer = buffer;
        node->input_ptrs[j] = buffer->data;
      }
    }
    if (!node->outputs && op.n_out != 0u) {
      node->outputs = (RuntimeBufferBinding *)mem_arena_alloc(
          metadata_arena, (uint32_t)op.n_out * (uint32_t)sizeof(RuntimeBufferBinding),
          metadata_align());
      node->output_ptrs = (void **)mem_arena_alloc(
          metadata_arena, (uint32_t)op.n_out * (uint32_t)sizeof(void *),
          metadata_align());
      if (!node->outputs || !node->output_ptrs) {
        return GRAPH_STATUS_INSUFFICIENT_METADATA_MEMORY;
      }
      node->num_outputs = op.n_out;
      for (j = 0; j < op.n_out; ++j) {
        RuntimeBufferView *buffer;
        if (!find_runtime_buffer(graph->buffers, graph->num_buffers,
                                 rd_u32(op.out_buf_ids + ((uint32_t)j * 4u)),
                                 &buffer)) {
          return GRAPH_STATUS_INVALID_BLOB;
        }
        node->outputs[j].buffer = buffer;
        node->output_ptrs[j] = buffer->data;
      }
    }
    graph->schedule[i].node_id = op.node_id;
    graph->schedule[i].node_index = node_index;
    graph->schedule[i].node = node;
  }
  return GRAPH_STATUS_OK;
}

static void *rt_memcpy(void *dst, const void *src, uint32_t bytes) {
  return memcpy(dst, src, (size_t)bytes);
}

static void *rt_memset(void *dst, int value, uint32_t bytes) {
  return memset(dst, value, (size_t)bytes);
}

static GraphStatus apply_param_defaults(RuntimeNodeInstance *node) {
  uint32_t param_id;
  if (!node) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (!node->param_defaults || node->param_defaults_bytes == 0u) {
    return GRAPH_STATUS_OK;
  }
  if (!node->module || !node->module->vtable.set_param) {
    return GRAPH_STATUS_MODULE_PARAM_FAILED;
  }
  switch (node->module->module_id) {
    case 0x00001001u:
      param_id = 1u;
      break;
    default:
      return GRAPH_STATUS_MODULE_PARAM_FAILED;
  }
  if (node->module->vtable.set_param(node->state, param_id, node->param_defaults,
                                     node->param_defaults_bytes) != AWE_OK) {
    return GRAPH_STATUS_MODULE_PARAM_FAILED;
  }
  return GRAPH_STATUS_OK;
}

static GraphStatus init_nodes(GraphInstance *graph) {
  AweRuntimeApi api;
  AweProcessCtx init_ctx;
  uint32_t i;
  if (!graph || !graph->nodes) {
    return GRAPH_STATUS_BAD_ARG;
  }

  memset(&api, 0, sizeof(api));
  api.api_bytes = sizeof(api);
  api.abi_major = AWE_ABI_MAJOR;
  api.abi_minor = AWE_ABI_MINOR;
  api.memcpy_fn = rt_memcpy;
  api.memset_fn = rt_memset;

  memset(&init_ctx, 0, sizeof(init_ctx));
  init_ctx.sample_rate_hz = graph->sample_rate_hz;
  init_ctx.block_frames = graph->block_frames;

  for (i = 0; i < graph->num_nodes; ++i) {
    GraphStatus status;
    RuntimeNodeInstance *node = &graph->nodes[i];
    if (node->module && node->module->vtable.init) {
      if (node->module->vtable.init(node->state, &api, node->init_blob, node->init_bytes,
                                    &init_ctx) != AWE_OK) {
        return GRAPH_STATUS_MODULE_INIT_FAILED;
      }
    }
    node->initialized = 1u;
    status = apply_param_defaults(node);
    if (status != GRAPH_STATUS_OK) {
      return status;
    }
  }

  return GRAPH_STATUS_OK;
}

static GraphStatus process_one_node(RuntimeNodeInstance *node,
                                    const AweProcessCtx *ctx) {
  if (!node || !ctx) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (!node->module || !node->module->vtable.process || !node->initialized) {
    return GRAPH_STATUS_INVALID_BLOB;
  }
  if (node->module->vtable.process(node->state, node->input_ptrs, node->output_ptrs,
                                   ctx) != AWE_OK) {
    return GRAPH_STATUS_PROCESS_FAILED;
  }
  return GRAPH_STATUS_OK;
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

GraphStatus graph_bind_from_blob(const BlobView *blob,
                                 const ModuleRegistry *registry,
                                 const RuntimeHostConfig *host_cfg,
                                 const RuntimeMemoryConfig *mem_cfg,
                                 GraphInstance *out_graph) {
  GraphStatus status;
  GraphMemoryRequirements req;
  uint32_t sample_rate_hz;
  uint32_t block_multiple_n;
  uint32_t *heap_required_bytes;
  MemArena metadata_arena;
  MemArena state_arena;

  if (!blob || !registry || !host_cfg || !mem_cfg || !out_graph) {
    return GRAPH_STATUS_BAD_ARG;
  }
  memset(out_graph, 0, sizeof(*out_graph));
  if (host_cfg->base_block_frames == 0u || !mem_cfg->metadata_mem ||
      !mem_cfg->module_state_mem) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (!load_graph_config(blob, &sample_rate_hz, &block_multiple_n) ||
      sample_rate_hz == 0u || block_multiple_n == 0u) {
    return GRAPH_STATUS_INVALID_BLOB;
  }

  status = graph_get_memory_requirements(blob, registry, &req, NULL, 0u);
  if (status != GRAPH_STATUS_OK) {
    return status;
  }
  if (mem_cfg->metadata_mem_size < req.metadata_bytes) {
    return GRAPH_STATUS_INSUFFICIENT_METADATA_MEMORY;
  }
  if (mem_cfg->module_state_mem_size < req.module_state_bytes) {
    return GRAPH_STATUS_INSUFFICIENT_STATE_MEMORY;
  }
  if (mem_cfg->num_heaps != req.num_heaps || !mem_cfg->heap_bases || !mem_cfg->heap_sizes) {
    return GRAPH_STATUS_INSUFFICIENT_HEAP_COUNT;
  }

  {
    uint32_t heap_required_stack[req.num_heaps];
    heap_required_bytes = heap_required_stack;
    status = graph_get_memory_requirements(blob, registry, &req, heap_required_bytes,
                                           req.num_heaps);
    if (status != GRAPH_STATUS_OK) {
      return status;
    }

    memset(mem_cfg->metadata_mem, 0, mem_cfg->metadata_mem_size);
    memset(mem_cfg->module_state_mem, 0, mem_cfg->module_state_mem_size);

    if (!mem_arena_init(&metadata_arena, mem_cfg->metadata_mem,
                        mem_cfg->metadata_mem_size) ||
        !mem_arena_init(&state_arena, mem_cfg->module_state_mem,
                        mem_cfg->module_state_mem_size)) {
      return GRAPH_STATUS_BAD_ARG;
    }

    out_graph->heaps = (RuntimeHeap *)mem_arena_alloc(
        &metadata_arena, req.num_heaps * (uint32_t)sizeof(RuntimeHeap),
        metadata_align());
    out_graph->buffers = (RuntimeBufferView *)mem_arena_alloc(
        &metadata_arena, req.num_buffers * (uint32_t)sizeof(RuntimeBufferView),
        metadata_align());
    out_graph->nodes = (RuntimeNodeInstance *)mem_arena_alloc(
        &metadata_arena, req.num_nodes * (uint32_t)sizeof(RuntimeNodeInstance),
        metadata_align());
    out_graph->schedule = (RuntimeScheduleEntry *)mem_arena_alloc(
        &metadata_arena,
        req.schedule_length * (uint32_t)sizeof(RuntimeScheduleEntry),
        metadata_align());
    if (!out_graph->heaps || !out_graph->buffers || !out_graph->nodes ||
        !out_graph->schedule) {
      memset(out_graph, 0, sizeof(*out_graph));
      return GRAPH_STATUS_INSUFFICIENT_METADATA_MEMORY;
    }

    out_graph->num_heaps = req.num_heaps;
    out_graph->num_buffers = req.num_buffers;
    out_graph->num_nodes = req.num_nodes;
    out_graph->schedule_length = req.schedule_length;
    out_graph->sample_rate_hz = sample_rate_hz;
    out_graph->block_multiple_n = block_multiple_n;
    out_graph->block_frames = host_cfg->base_block_frames * block_multiple_n;
    out_graph->metadata_mem = mem_cfg->metadata_mem;
    out_graph->metadata_mem_size = mem_cfg->metadata_mem_size;
    out_graph->module_state_mem = mem_cfg->module_state_mem;
    out_graph->module_state_mem_size = mem_cfg->module_state_mem_size;

    status = bind_heaps(blob, mem_cfg, out_graph, heap_required_bytes);
    if (status != GRAPH_STATUS_OK) {
      memset(out_graph, 0, sizeof(*out_graph));
      return status;
    }
    status = bind_buffers(blob, out_graph);
    if (status != GRAPH_STATUS_OK) {
      memset(out_graph, 0, sizeof(*out_graph));
      return status;
    }
    status = bind_nodes(blob, registry, &state_arena, out_graph);
    if (status != GRAPH_STATUS_OK) {
      memset(out_graph, 0, sizeof(*out_graph));
      return status;
    }
    status = bind_schedule(blob, &metadata_arena, out_graph);
    if (status != GRAPH_STATUS_OK) {
      memset(out_graph, 0, sizeof(*out_graph));
      return status;
    }
    status = init_nodes(out_graph);
    if (status != GRAPH_STATUS_OK) {
      unbind_initialized_nodes(out_graph);
      memset(out_graph, 0, sizeof(*out_graph));
      return status;
    }

    out_graph->is_bound = 1u;
    return GRAPH_STATUS_OK;
  }
}

GraphStatus graph_unbind(GraphInstance *graph) {
  if (!graph) {
    return GRAPH_STATUS_BAD_ARG;
  }
  unbind_initialized_nodes(graph);
  memset(graph, 0, sizeof(*graph));
  return GRAPH_STATUS_OK;
}

GraphStatus graph_process(GraphInstance *graph, uint64_t block_index) {
  AweProcessCtx ctx;
  uint32_t i;

  if (!graph) {
    return GRAPH_STATUS_BAD_ARG;
  }
  if (!graph->is_bound || !graph->nodes || !graph->schedule) {
    return GRAPH_STATUS_NOT_BOUND;
  }

  memset(&ctx, 0, sizeof(ctx));
  ctx.sample_rate_hz = graph->sample_rate_hz;
  ctx.block_frames = graph->block_frames;
  ctx.block_index = block_index;

  /* graph_process() executes exactly one graph block, not one base block. */
  for (i = 0; i < graph->schedule_length; ++i) {
    RuntimeScheduleEntry *entry = &graph->schedule[i];
    if (entry->node_index >= graph->num_nodes || !entry->node) {
      return GRAPH_STATUS_INVALID_BLOB;
    }
    {
      GraphStatus status = process_one_node(entry->node, &ctx);
      if (status != GRAPH_STATUS_OK) {
        return status;
      }
    }
  }

  return GRAPH_STATUS_OK;
}
