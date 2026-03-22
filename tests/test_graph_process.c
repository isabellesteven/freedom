/* Verifies graph_process() over bound graphs, including success and failure paths.
   It checks one-node gain, two-node chaining, and early stop when a module process callback fails. */
#include "runtime/engine/graph_instance.h"

#include <math.h>
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

static size_t build_gain_blob(uint8_t *buf, size_t cap, float gain_db) {
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
  wr_u16(buffers + 24, 1u);
  wr_u16(buffers + 26, 0u);
  wr_u32(buffers + 28, 0u);
  wr_u16(buffers + 32, 1u);
  wr_u16(buffers + 34, 48u);

  wr_u32(buffers + 36, 2u);
  buffers[40] = 0u;
  buffers[41] = 1u;
  wr_u16(buffers + 42, 0u);
  wr_u32(buffers + 44, 1u);
  wr_u32(buffers + 48, 192u);
  wr_u32(buffers + 52, 192u);
  wr_u16(buffers + 56, 1u);
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
  wr_f32(nodes + 36, gain_db);

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
  wr_f32(params + 12, gain_db);

  at = 32u;
  at = add_section(buf, at, GRPH_SECT_REQUIRES, requires, sizeof(requires));
  at = add_section(buf, at, GRPH_SECT_GRAPH_CONFIG, graph_config, sizeof(graph_config));
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

static size_t build_gain_chain_blob(uint8_t *buf, size_t cap, float gain1_db,
                                    float gain2_db) {
  uint8_t requires[16];
  uint8_t graph_config[8];
  uint8_t heaps[52];
  uint8_t buffers[100];
  uint8_t nodes[76];
  uint8_t schedule[36];
  uint8_t params[28];
  size_t at;
  uint32_t crc;

  if (cap < 768u) {
    return 0u;
  }

  memset(buf, 0, cap);
  memcpy(buf + 0, "GRPH", 4);
  buf[4] = 1;
  buf[5] = 0;
  wr_u16(buf + 6, 32u);
  wr_u32(buf + 12, 0x414E4350u);
  wr_u64(buf + 16, 2u);
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
  wr_u32(heaps + 12, 1536u);
  wr_u32(heaps + 16, 16u);
  wr_u32(heaps + 20, 2u);
  wr_u32(heaps + 24, 3u);
  wr_u32(heaps + 28, 512u);
  wr_u32(heaps + 32, 16u);
  wr_u32(heaps + 36, 3u);
  wr_u32(heaps + 40, 2u);
  wr_u32(heaps + 44, 256u);
  wr_u32(heaps + 48, 16u);

  wr_u32(buffers + 0, 3u);
  wr_u32(buffers + 4, 1u);
  buffers[8] = 0u;
  buffers[9] = 1u;
  wr_u16(buffers + 10, 0u);
  wr_u32(buffers + 12, 1u);
  wr_u32(buffers + 16, 0u);
  wr_u32(buffers + 20, 192u);
  wr_u16(buffers + 24, 1u);
  wr_u16(buffers + 26, 0u);
  wr_u32(buffers + 28, 0u);
  wr_u16(buffers + 32, 1u);
  wr_u16(buffers + 34, 48u);

  wr_u32(buffers + 36, 2u);
  buffers[40] = 0u;
  buffers[41] = 1u;
  wr_u16(buffers + 42, 0u);
  wr_u32(buffers + 44, 1u);
  wr_u32(buffers + 48, 192u);
  wr_u32(buffers + 52, 192u);
  wr_u16(buffers + 56, 1u);
  wr_u16(buffers + 58, 0u);
  wr_u32(buffers + 60, 0u);
  wr_u16(buffers + 64, 1u);
  wr_u16(buffers + 66, 48u);

  wr_u32(buffers + 68, 3u);
  buffers[72] = 0u;
  buffers[73] = 1u;
  wr_u16(buffers + 74, 0u);
  wr_u32(buffers + 76, 1u);
  wr_u32(buffers + 80, 384u);
  wr_u32(buffers + 84, 192u);
  wr_u16(buffers + 88, 1u);
  wr_u16(buffers + 90, 0u);
  wr_u32(buffers + 92, 0u);
  wr_u16(buffers + 96, 1u);
  wr_u16(buffers + 98, 48u);

  wr_u32(nodes + 0, 2u);
  wr_u32(nodes + 4, 10u);
  wr_u32(nodes + 8, 0x00001001u);
  wr_u32(nodes + 12, 2u);
  wr_u32(nodes + 16, 0u);
  wr_u32(nodes + 20, 16u);
  wr_u32(nodes + 24, 16u);
  wr_u32(nodes + 28, 4u);
  wr_u32(nodes + 32, 4u);
  wr_f32(nodes + 36, gain1_db);

  wr_u32(nodes + 40, 20u);
  wr_u32(nodes + 44, 0x00001001u);
  wr_u32(nodes + 48, 2u);
  wr_u32(nodes + 52, 0u);
  wr_u32(nodes + 56, 16u);
  wr_u32(nodes + 60, 16u);
  wr_u32(nodes + 64, 4u);
  wr_u32(nodes + 68, 4u);
  wr_f32(nodes + 72, gain2_db);

  wr_u32(schedule + 0, 2u);
  schedule[4] = 1u;
  schedule[5] = 1u;
  schedule[6] = 1u;
  schedule[7] = 0u;
  wr_u32(schedule + 8, 10u);
  wr_u32(schedule + 12, 1u);
  wr_u32(schedule + 16, 2u);

  schedule[20] = 1u;
  schedule[21] = 1u;
  schedule[22] = 1u;
  schedule[23] = 0u;
  wr_u32(schedule + 24, 20u);
  wr_u32(schedule + 28, 2u);
  wr_u32(schedule + 32, 3u);

  wr_u32(params + 0, 2u);
  wr_u32(params + 4, 10u);
  wr_u32(params + 8, 4u);
  wr_f32(params + 12, gain1_db);
  wr_u32(params + 16, 20u);
  wr_u32(params + 20, 4u);
  wr_f32(params + 24, gain2_db);

  at = 32u;
  at = add_section(buf, at, GRPH_SECT_REQUIRES, requires, sizeof(requires));
  at = add_section(buf, at, GRPH_SECT_GRAPH_CONFIG, graph_config, sizeof(graph_config));
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

static int fail_init(void *state, const AweRuntimeApi *api, const void *init_blob,
                     uint32_t init_bytes, const AweProcessCtx *ctx) {
  (void)state;
  (void)api;
  (void)init_blob;
  (void)init_bytes;
  (void)ctx;
  return AWE_OK;
}

static int fail_process(void *state, const void * const *inputs, void * const *outputs,
                        const AweProcessCtx *ctx) {
  (void)state;
  (void)inputs;
  (void)outputs;
  (void)ctx;
  return AWE_EINTERNAL;
}

static int fail_set_param(void *state, uint32_t param_id, const void *data,
                          uint32_t size_bytes) {
  (void)state;
  (void)param_id;
  (void)data;
  (void)size_bytes;
  return AWE_EINVAL;
}

static int fail_get_param(void *state, uint32_t param_id, void *data,
                          uint32_t *size_bytes) {
  (void)state;
  (void)param_id;
  (void)data;
  (void)size_bytes;
  return AWE_EINVAL;
}

static void fail_deinit(void *state) {
  (void)state;
}

static const AweModuleDescriptor g_fail_desc = {
    .desc_bytes = sizeof(AweModuleDescriptor),
    .module_id = 0x00002001u,
    .ver_major = 1u,
    .ver_minor = 0u,
    .abi_major = AWE_ABI_MAJOR,
    .abi_minor = AWE_ABI_MINOR,
    .caps = 0u,
    .state_bytes = 4u,
    .state_align = 4u,
    .n_in = 1u,
    .n_out = 1u,
    .fixed_block_frames = 0u,
    .vtable = {
        .init = fail_init,
        .process = fail_process,
        .set_param = fail_set_param,
        .get_param = fail_get_param,
        .deinit = fail_deinit,
    }};

static size_t build_fail_blob(uint8_t *buf, size_t cap) {
  uint8_t requires[16];
  uint8_t graph_config[8];
  uint8_t heaps[52];
  uint8_t buffers[68];
  uint8_t nodes[36];
  uint8_t schedule[20];
  uint8_t params[4];
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
  wr_u64(buf + 16, 3u);
  wr_u64(buf + 24, 0u);

  wr_u32(requires + 0, 1u);
  wr_u32(requires + 4, 0x00002001u);
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
  wr_u16(buffers + 24, 1u);
  wr_u16(buffers + 26, 0u);
  wr_u32(buffers + 28, 0u);
  wr_u16(buffers + 32, 1u);
  wr_u16(buffers + 34, 48u);

  wr_u32(buffers + 36, 2u);
  buffers[40] = 0u;
  buffers[41] = 1u;
  wr_u16(buffers + 42, 0u);
  wr_u32(buffers + 44, 1u);
  wr_u32(buffers + 48, 192u);
  wr_u32(buffers + 52, 192u);
  wr_u16(buffers + 56, 1u);
  wr_u16(buffers + 58, 0u);
  wr_u32(buffers + 60, 0u);
  wr_u16(buffers + 64, 1u);
  wr_u16(buffers + 66, 48u);

  wr_u32(nodes + 0, 1u);
  wr_u32(nodes + 4, 10u);
  wr_u32(nodes + 8, 0x00002001u);
  wr_u32(nodes + 12, 2u);
  wr_u32(nodes + 16, 0u);
  wr_u32(nodes + 20, 4u);
  wr_u32(nodes + 24, 4u);
  wr_u32(nodes + 28, 0u);
  wr_u32(nodes + 32, 0u);

  wr_u32(schedule + 0, 1u);
  schedule[4] = 1u;
  schedule[5] = 1u;
  schedule[6] = 1u;
  schedule[7] = 0u;
  wr_u32(schedule + 8, 10u);
  wr_u32(schedule + 12, 1u);
  wr_u32(schedule + 16, 2u);

  wr_u32(params + 0, 0u);

  at = 32u;
  at = add_section(buf, at, GRPH_SECT_REQUIRES, requires, sizeof(requires));
  at = add_section(buf, at, GRPH_SECT_GRAPH_CONFIG, graph_config, sizeof(graph_config));
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

extern const AweModuleDescriptor *awe_get_module_descriptor(uint32_t abi_major,
                                                            uint32_t abi_minor);

static GraphStatus bind_blob(const uint8_t *blob_bytes, size_t blob_size,
                             const AweModuleDescriptor *const *modules,
                             uint32_t module_count, GraphInstance *out_graph,
                             uint8_t *metadata_mem, uint32_t metadata_size,
                             uint8_t *state_mem, uint32_t state_size,
                             uint8_t *heap0, uint32_t heap0_size,
                             uint8_t *heap1, uint32_t heap1_size,
                             uint8_t *heap2, uint32_t heap2_size) {
  BlobView blob;
  char err[256];
  ModuleRegistry registry;
  RuntimeHostConfig host_cfg;
  RuntimeMemoryConfig mem_cfg;
  void *heap_bases[3];
  uint32_t heap_sizes[3];

  if (grph_blob_parse(blob_bytes, blob_size, &blob, err, sizeof(err)) != GRPH_BLOB_OK) {
    fprintf(stderr, "blob parse failed: %s\n", err);
    return GRAPH_STATUS_INVALID_BLOB;
  }

  registry.modules = modules;
  registry.module_count = module_count;
  host_cfg.base_block_frames = 48u;
  heap_bases[0] = heap0;
  heap_bases[1] = heap1;
  heap_bases[2] = heap2;
  heap_sizes[0] = heap0_size;
  heap_sizes[1] = heap1_size;
  heap_sizes[2] = heap2_size;

  mem_cfg.metadata_mem = metadata_mem;
  mem_cfg.metadata_mem_size = metadata_size;
  mem_cfg.module_state_mem = state_mem;
  mem_cfg.module_state_mem_size = state_size;
  mem_cfg.heap_bases = heap_bases;
  mem_cfg.heap_sizes = heap_sizes;
  mem_cfg.num_heaps = 3u;

  return graph_bind_from_blob(&blob, &registry, &host_cfg, &mem_cfg, out_graph);
}

int main(void) {
  const AweModuleDescriptor *gain_desc;
  const AweModuleDescriptor *gain_modules[1];
  const AweModuleDescriptor *mixed_modules[2];
  uint8_t blob_bytes[768];
  uint8_t metadata_mem[1024];
  uint8_t state_mem[128];
  uint8_t heap0[2048];
  uint8_t heap1[512];
  uint8_t heap2[256];
  GraphInstance graph;
  GraphStatus status;
  size_t blob_size;
  uint32_t i;

  gain_desc = awe_get_module_descriptor(AWE_ABI_MAJOR, AWE_ABI_MINOR);
  if (!gain_desc) {
    fprintf(stderr, "gain descriptor unavailable\n");
    return 1;
  }
  gain_modules[0] = gain_desc;
  mixed_modules[0] = gain_desc;
  mixed_modules[1] = &g_fail_desc;

  blob_size = build_gain_blob(blob_bytes, sizeof(blob_bytes), -6.0f);
  memset(&graph, 0, sizeof(graph));
  memset(metadata_mem, 0, sizeof(metadata_mem));
  memset(state_mem, 0, sizeof(state_mem));
  memset(heap0, 0, sizeof(heap0));
  memset(heap1, 0, sizeof(heap1));
  memset(heap2, 0, sizeof(heap2));
  status = bind_blob(blob_bytes, blob_size, gain_modules, 1u, &graph, metadata_mem,
                     sizeof(metadata_mem), state_mem, sizeof(state_mem), heap0,
                     sizeof(heap0), heap1, sizeof(heap1), heap2, sizeof(heap2));
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "one-node bind failed: %d\n", (int)status);
    return 1;
  }
  for (i = 0; i < 48u; ++i) {
    ((float *)graph.buffers[0].data)[i] = (float)(i + 1u);
  }
  status = graph_process(&graph, 7u);
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "one-node process failed: %d\n", (int)status);
    return 1;
  }
  for (i = 0; i < 48u; ++i) {
    float expected = ((float)(i + 1u)) * powf(10.0f, -6.0f / 20.0f);
    float actual = ((float *)graph.buffers[1].data)[i];
    if (fabsf(actual - expected) > 1e-4f) {
      fprintf(stderr, "one-node output mismatch at %u\n", (unsigned)i);
      return 1;
    }
  }
  graph_unbind(&graph);

  blob_size = build_gain_chain_blob(blob_bytes, sizeof(blob_bytes), -6.0f, 3.0f);
  memset(&graph, 0, sizeof(graph));
  memset(metadata_mem, 0, sizeof(metadata_mem));
  memset(state_mem, 0, sizeof(state_mem));
  memset(heap0, 0, sizeof(heap0));
  memset(heap1, 0, sizeof(heap1));
  memset(heap2, 0, sizeof(heap2));
  status = bind_blob(blob_bytes, blob_size, gain_modules, 1u, &graph, metadata_mem,
                     sizeof(metadata_mem), state_mem, sizeof(state_mem), heap0,
                     sizeof(heap0), heap1, sizeof(heap1), heap2, sizeof(heap2));
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "two-node bind failed: %d\n", (int)status);
    return 1;
  }
  for (i = 0; i < 48u; ++i) {
    ((float *)graph.buffers[0].data)[i] = 0.5f * (float)(i + 1u);
  }
  status = graph_process(&graph, 11u);
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "two-node process failed: %d\n", (int)status);
    return 1;
  }
  for (i = 0; i < 48u; ++i) {
    float expected =
        (0.5f * (float)(i + 1u)) * powf(10.0f, (-6.0f + 3.0f) / 20.0f);
    float actual = ((float *)graph.buffers[2].data)[i];
    if (fabsf(actual - expected) > 1e-4f) {
      fprintf(stderr, "two-node output mismatch at %u\n", (unsigned)i);
      return 1;
    }
  }
  graph_unbind(&graph);

  blob_size = build_fail_blob(blob_bytes, sizeof(blob_bytes));
  memset(&graph, 0, sizeof(graph));
  memset(metadata_mem, 0, sizeof(metadata_mem));
  memset(state_mem, 0, sizeof(state_mem));
  memset(heap0, 0, sizeof(heap0));
  memset(heap1, 0, sizeof(heap1));
  memset(heap2, 0, sizeof(heap2));
  status = bind_blob(blob_bytes, blob_size, mixed_modules, 2u, &graph, metadata_mem,
                     sizeof(metadata_mem), state_mem, sizeof(state_mem), heap0,
                     sizeof(heap0), heap1, sizeof(heap1), heap2, sizeof(heap2));
  if (status != GRAPH_STATUS_OK) {
    fprintf(stderr, "fail-node bind failed: %d\n", (int)status);
    return 1;
  }
  status = graph_process(&graph, 13u);
  if (status != GRAPH_STATUS_PROCESS_FAILED) {
    fprintf(stderr, "expected process failure, got %d\n", (int)status);
    return 1;
  }
  graph_unbind(&graph);

  return 0;
}
