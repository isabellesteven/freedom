/* Verifies graph_bind_from_blob() and graph_unbind() using caller-provided static memory.
   It checks that heaps, buffers, nodes, and schedule entries are bound into the expected regions. */
#include "runtime/engine/graph_instance.h"
#include "runtime/engine/module_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void wr_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void wr_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void wr_u64(uint8_t *p, uint64_t v) {
  wr_u32(p, (uint32_t)(v & 0xFFFFFFFFu));
  wr_u32(p + 4, (uint32_t)(v >> 32));
}

static void wr_f32(uint8_t *p, float v) {
  uint32_t u;
  memcpy(&u, &v, sizeof(u));
  wr_u32(p, u);
}

static uint32_t crc32_eth(const uint8_t *data, size_t n) {
  uint32_t crc = 0xFFFFFFFFu;
  size_t i;
  for (i = 0; i < n; ++i) {
    uint32_t x = (crc ^ data[i]) & 0xFFu;
    int bit;
    for (bit = 0; bit < 8; ++bit) {
      x = (x & 1u) ? (0xEDB88320u ^ (x >> 1)) : (x >> 1);
    }
    crc = (crc >> 8) ^ x;
  }
  return ~crc;
}

static size_t add_section(uint8_t *buf, size_t at, uint32_t type,
                          const uint8_t *payload, uint32_t payload_bytes) {
  wr_u32(buf + at + 0, type);
  wr_u32(buf + at + 4, payload_bytes);
  wr_u32(buf + at + 8, 0u);
  wr_u32(buf + at + 12, 0u);
  memcpy(buf + at + 16, payload, payload_bytes);
  return at + 16u + payload_bytes;
}

static size_t build_reference_blob(uint8_t *buf, size_t cap) {
  uint8_t requires[16];
  uint8_t graph_config[8];
  uint8_t heaps[52];
  uint8_t buffers[68];
  uint8_t nodes[40];
  uint8_t schedule[20];
  uint8_t params[16];
  size_t at;
  uint32_t crc;

  if (cap < 512u) {
    return 0u;
  }

  memset(buf, 0, cap);
  memcpy(buf + 0, "GRPH", 4);
  buf[4] = 1;
  buf[5] = 0;
  wr_u16(buf + 6, 32u);
  wr_u32(buf + 12, 0x414E4350u);
  wr_u64(buf + 16, 1u);
  wr_u64(buf + 24, 0u);

  wr_u32(requires + 0, 1u);
  wr_u32(requires + 4, 0x00001001u);
  wr_u16(requires + 8, 1u);
  wr_u16(requires + 10, 0u);
  wr_u32(requires + 12, 0u);

  wr_u32(graph_config + 0, 48000u);
  wr_u32(graph_config + 4, 1u);

  wr_u32(heaps + 0, 3u);
  wr_u32(heaps + 4, 1u);
  wr_u32(heaps + 8, 4u);
  wr_u32(heaps + 12, 768u);
  wr_u32(heaps + 16, 16u);
  wr_u32(heaps + 20, 2u);
  wr_u32(heaps + 24, 3u);
  wr_u32(heaps + 28, 256u);
  wr_u32(heaps + 32, 16u);
  wr_u32(heaps + 36, 3u);
  wr_u32(heaps + 40, 2u);
  wr_u32(heaps + 44, 256u);
  wr_u32(heaps + 48, 16u);

  wr_u32(buffers + 0, 2u);
  wr_u32(buffers + 4, 1u);
  buffers[8] = 0u;
  buffers[9] = 1u;
  wr_u16(buffers + 10, 0u);
  wr_u32(buffers + 12, 1u);
  wr_u32(buffers + 16, 0u);
  wr_u32(buffers + 20, 192u);
  wr_u16(buffers + 24, 2u);
  wr_u16(buffers + 26, 0u);
  wr_u32(buffers + 28, 0u);
  wr_u16(buffers + 32, 1u);
  wr_u16(buffers + 34, 48u);

  wr_u32(buffers + 36, 2u);
  buffers[40] = 0u;
  buffers[41] = 1u;
  wr_u16(buffers + 42, 0u);
  wr_u32(buffers + 44, 1u);
  wr_u32(buffers + 48, 384u);
  wr_u32(buffers + 52, 192u);
  wr_u16(buffers + 56, 2u);
  wr_u16(buffers + 58, 0u);
  wr_u32(buffers + 60, 0u);
  wr_u16(buffers + 64, 1u);
  wr_u16(buffers + 66, 48u);

  wr_u32(nodes + 0, 1u);
  wr_u32(nodes + 4, 10u);
  wr_u32(nodes + 8, 0x00001001u);
  wr_u32(nodes + 12, 2u);
  wr_u32(nodes + 16, 0u);
  wr_u32(nodes + 20, 16u);
  wr_u32(nodes + 24, 16u);
  wr_u32(nodes + 28, 4u);
  wr_u32(nodes + 32, 4u);
  wr_f32(nodes + 36, -6.0f);

  wr_u32(schedule + 0, 1u);
  schedule[4] = 1u;
  schedule[5] = 1u;
  schedule[6] = 1u;
  schedule[7] = 0u;
  wr_u32(schedule + 8, 10u);
  wr_u32(schedule + 12, 1u);
  wr_u32(schedule + 16, 2u);

  wr_u32(params + 0, 1u);
  wr_u32(params + 4, 10u);
  wr_u32(params + 8, 4u);
  wr_f32(params + 12, -6.0f);

  at = 32u;
  at = add_section(buf, at, GRPH_SECT_REQUIRES, requires, sizeof(requires));
  at = add_section(buf, at, GRPH_SECT_GRAPH_CONFIG, graph_config,
                   sizeof(graph_config));
  at = add_section(buf, at, GRPH_SECT_HEAPS, heaps, sizeof(heaps));
  at = add_section(buf, at, GRPH_SECT_BUFFERS, buffers, sizeof(buffers));
  at = add_section(buf, at, GRPH_SECT_NODES, nodes, sizeof(nodes));
  at = add_section(buf, at, GRPH_SECT_SCHEDULE, schedule, sizeof(schedule));
  at = add_section(buf, at, GRPH_SECT_PARAM_DEFAULTS, params, sizeof(params));

  wr_u32(buf + 8, (uint32_t)(at + 4u));
  crc = crc32_eth(buf, at);
  wr_u32(buf + at, crc);
  return at + 4u;
}

int main(void) {
  uint8_t blob_bytes[512];
  BlobView blob;
  char err[256];
  const ModuleRegistry *registry;
  GraphInstance graph;
  RuntimeHostConfig host_cfg;
  RuntimeMemoryConfig mem_cfg;
  uint8_t metadata_mem[512];
  uint8_t module_state_mem[64];
  uint8_t heap0[768];
  uint8_t heap1[256];
  uint8_t heap2[256];
  void *heap_bases[3];
  uint32_t heap_sizes[3];
  size_t blob_size;
  GraphStatus status;

  blob_size = build_reference_blob(blob_bytes, sizeof(blob_bytes));
  if (blob_size == 0u) {
    fprintf(stderr, "failed to build test blob\n");
    return 1;
  }
  if (grph_blob_parse(blob_bytes, blob_size, &blob, err, sizeof(err)) != GRPH_BLOB_OK) {
    fprintf(stderr, "blob parse failed: %s\n", err);
    return 1;
  }

  registry = grph_builtin_module_registry();
  if (!registry || !grph_module_registry_validate(registry)) {
    fprintf(stderr, "built-in registry unavailable\n");
    return 1;
  }

  memset(&graph, 0xA5, sizeof(graph));
  memset(metadata_mem, 0xCC, sizeof(metadata_mem));
  memset(module_state_mem, 0xDD, sizeof(module_state_mem));
  memset(heap0, 0, sizeof(heap0));
  memset(heap1, 0, sizeof(heap1));
  memset(heap2, 0, sizeof(heap2));

  host_cfg.base_block_frames = 48u;
  heap_bases[0] = heap0;
  heap_bases[1] = heap1;
  heap_bases[2] = heap2;
  heap_sizes[0] = sizeof(heap0);
  heap_sizes[1] = sizeof(heap1);
  heap_sizes[2] = sizeof(heap2);

  mem_cfg.metadata_mem = metadata_mem;
  mem_cfg.metadata_mem_size = sizeof(metadata_mem);
  mem_cfg.module_state_mem = module_state_mem;
  mem_cfg.module_state_mem_size = sizeof(module_state_mem);
  mem_cfg.heap_bases = heap_bases;
  mem_cfg.heap_sizes = heap_sizes;
  mem_cfg.num_heaps = 3u;

  status = graph_bind_from_blob(&blob, registry, &host_cfg, &mem_cfg, &graph);
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "graph_bind_from_blob failed: %d\n", (int)status);
    return 1;
  }

  if (graph.sample_rate_hz != 48000u || graph.block_frames != 48u ||
      graph.num_heaps != 3u || graph.num_buffers != 2u || graph.num_nodes != 1u ||
      graph.schedule_length != 1u) {
    fprintf(stderr, "unexpected bound graph counts\n");
    return 1;
  }
  if (graph.buffers[0].data != heap0 || graph.buffers[1].data != (heap0 + 384u)) {
    fprintf(stderr, "buffer data pointers not bound into supplied heaps\n");
    return 1;
  }
  if ((uint8_t *)graph.nodes[0].state < module_state_mem ||
      (uint8_t *)graph.nodes[0].state >= module_state_mem + sizeof(module_state_mem)) {
    fprintf(stderr, "node state pointer not bound into supplied state memory\n");
    return 1;
  }
  if (!graph.nodes[0].inputs || !graph.nodes[0].outputs ||
      graph.nodes[0].inputs[0].buffer != &graph.buffers[0] ||
      graph.nodes[0].outputs[0].buffer != &graph.buffers[1] ||
      graph.nodes[0].input_ptrs[0] != graph.buffers[0].data ||
      graph.nodes[0].output_ptrs[0] != graph.buffers[1].data) {
    fprintf(stderr, "node buffer bindings not established\n");
    return 1;
  }
  if (graph.schedule[0].node != &graph.nodes[0] || graph.schedule[0].node_index != 0u) {
    fprintf(stderr, "schedule entry not bound to node\n");
    return 1;
  }
  if (!graph.is_bound || graph.block_multiple_n != 1u) {
    fprintf(stderr, "graph bookkeeping not marked bound\n");
    return 1;
  }

  status = graph_unbind(&graph);
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "graph_unbind failed: %d\n", (int)status);
    return 1;
  }
  if (graph.heaps != NULL || graph.buffers != NULL || graph.nodes != NULL ||
      graph.schedule != NULL || graph.num_nodes != 0u || graph.block_frames != 0u) {
    fprintf(stderr, "graph bookkeeping not cleared after unbind\n");
    return 1;
  }

  return 0;
}
