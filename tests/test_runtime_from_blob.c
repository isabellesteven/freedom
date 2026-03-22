/* End-to-end compiler/runtime harness for valid and invalid blob scenarios.
   It binds compiled blobs into static memory and checks expected numeric results or deterministic failures. */
#include "modules/module_abi.h"
#include "modules/sum2/sum2.h"
#include "runtime/engine/graph_instance.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLOB_CAP 8192u
#define METADATA_CAP 8192u
#define STATE_CAP 2048u
#define IO_HEAP_CAP 8192u
#define STATE_HEAP_CAP 512u
#define PARAM_HEAP_CAP 512u
#define BLOCK_FRAMES 48u

typedef struct ScenarioSpec {
  const char *name;
  uint32_t num_inputs;
  uint32_t num_outputs;
  int expect_inplace_alias;
} ScenarioSpec;

static const ScenarioSpec g_specs[] = {
    {"gain_chain_2", 1u, 1u, 0},
    {"gain_chain_4", 1u, 1u, 0},
    {"split_two_gains", 1u, 2u, 0},
    {"dry_wet_mix", 1u, 1u, 0},
    {"sum_two_inputs", 2u, 1u, 0},
    {"inplace_gain", 1u, 1u, 1},
    {"heap_too_small", 1u, 1u, 0},
};

static const ScenarioSpec *find_spec(const char *name) {
  uint32_t i;
  for (i = 0; i < (uint32_t)(sizeof(g_specs) / sizeof(g_specs[0])); ++i) {
    if (strcmp(name, g_specs[i].name) == 0) {
      return &g_specs[i];
    }
  }
  return NULL;
}

static float gain_db_to_lin(float gain_db) {
  return powf(10.0f, gain_db / 20.0f);
}

static int read_file_bytes(const char *path, uint8_t *out, size_t cap, size_t *out_size) {
  FILE *f;
  long n;
  size_t rn;

  if (!path || !out || !out_size) {
    return 0;
  }
  f = fopen(path, "rb");
  if (!f) {
    return 0;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 0;
  }
  n = ftell(f);
  if (n < 0 || (size_t)n > cap) {
    fclose(f);
    return 0;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return 0;
  }
  rn = fread(out, 1u, (size_t)n, f);
  fclose(f);
  if (rn != (size_t)n) {
    return 0;
  }
  *out_size = (size_t)n;
  return 1;
}

extern const AweModuleDescriptor *awe_get_module_descriptor(uint32_t abi_major,
                                                            uint32_t abi_minor);

static int bind_graph_from_blob(const uint8_t *blob_bytes, size_t blob_size,
                                uint32_t io_heap_size, GraphInstance *out_graph,
                                uint8_t *io_heap) {
  static uint8_t metadata_mem[METADATA_CAP];
  static uint8_t state_mem[STATE_CAP];
  static uint8_t state_heap[STATE_HEAP_CAP];
  static uint8_t param_heap[PARAM_HEAP_CAP];
  const AweModuleDescriptor *modules[2];
  ModuleRegistry registry;
  RuntimeHostConfig host_cfg;
  RuntimeMemoryConfig mem_cfg;
  BlobView blob;
  char err[256];
  void *heap_bases[3];
  uint32_t heap_sizes[3];

  if (grph_blob_parse(blob_bytes, blob_size, &blob, err, sizeof(err)) != GRPH_BLOB_OK) {
    fprintf(stderr, "blob parse failed: %s\n", err);
    return 0;
  }

  modules[0] = awe_get_module_descriptor(AWE_ABI_MAJOR, AWE_ABI_MINOR);
  modules[1] = awe_get_sum2_module_descriptor(AWE_ABI_MAJOR, AWE_ABI_MINOR);
  if (!modules[0] || !modules[1]) {
    fprintf(stderr, "module registry incomplete\n");
    return 0;
  }

  memset(metadata_mem, 0, sizeof(metadata_mem));
  memset(state_mem, 0, sizeof(state_mem));
  memset(io_heap, 0, IO_HEAP_CAP);
  memset(state_heap, 0, sizeof(state_heap));
  memset(param_heap, 0, sizeof(param_heap));
  memset(out_graph, 0, sizeof(*out_graph));

  registry.modules = modules;
  registry.module_count = 2u;
  host_cfg.base_block_frames = BLOCK_FRAMES;

  heap_bases[0] = io_heap;
  heap_bases[1] = state_heap;
  heap_bases[2] = param_heap;
  heap_sizes[0] = io_heap_size;
  heap_sizes[1] = sizeof(state_heap);
  heap_sizes[2] = sizeof(param_heap);

  mem_cfg.metadata_mem = metadata_mem;
  mem_cfg.metadata_mem_size = sizeof(metadata_mem);
  mem_cfg.module_state_mem = state_mem;
  mem_cfg.module_state_mem_size = sizeof(state_mem);
  mem_cfg.heap_bases = heap_bases;
  mem_cfg.heap_sizes = heap_sizes;
  mem_cfg.num_heaps = 3u;

  return graph_bind_from_blob(&blob, &registry, &host_cfg, &mem_cfg, out_graph) ==
         GRAPH_STATUS_OK;
}

static int check_buffer_values(const float *actual, const float *expected, uint32_t frames,
                               const char *label) {
  uint32_t i;
  for (i = 0; i < frames; ++i) {
    if (fabsf(actual[i] - expected[i]) > 1e-4f) {
      fprintf(stderr, "%s mismatch at frame %u: got=%f expected=%f\n", label,
              (unsigned)i, actual[i], expected[i]);
      return 0;
    }
  }
  return 1;
}

int main(int argc, char **argv) {
  static uint8_t blob_bytes[BLOB_CAP];
  static uint8_t io_heap[IO_HEAP_CAP];
  static float expected_a[BLOCK_FRAMES];
  static float expected_b[BLOCK_FRAMES];
  GraphInstance graph;
  const ScenarioSpec *spec;
  size_t blob_size;
  uint32_t i;

  if (argc != 3) {
    fprintf(stderr, "usage: test_runtime_from_blob <scenario> <blob>\n");
    return 2;
  }

  spec = find_spec(argv[1]);
  if (!spec) {
    fprintf(stderr, "unknown scenario: %s\n", argv[1]);
    return 2;
  }
  if (!read_file_bytes(argv[2], blob_bytes, sizeof(blob_bytes), &blob_size)) {
    fprintf(stderr, "failed to read blob: %s\n", argv[2]);
    return 1;
  }

  if (strcmp(spec->name, "heap_too_small") == 0) {
    if (bind_graph_from_blob(blob_bytes, blob_size, 256u, &graph, io_heap)) {
      fprintf(stderr, "expected heap_too_small bind failure\n");
      graph_unbind(&graph);
      return 1;
    }
    return 0;
  }

  if (!bind_graph_from_blob(blob_bytes, blob_size, IO_HEAP_CAP, &graph, io_heap)) {
    fprintf(stderr, "graph bind failed for scenario %s\n", spec->name);
    return 1;
  }

  for (i = 0; i < BLOCK_FRAMES; ++i) {
    ((float *)graph.buffers[0].data)[i] = (float)(i + 1u);
  }
  if (spec->num_inputs > 1u) {
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      ((float *)graph.buffers[1].data)[i] = 0.25f * (float)(i + 1u);
    }
  }

  if (graph_process(&graph, 5u) != GRAPH_STATUS_OK) {
    fprintf(stderr, "graph_process failed for scenario %s\n", spec->name);
    graph_unbind(&graph);
    return 1;
  }

  if (strcmp(spec->name, "gain_chain_2") == 0) {
    float gain = gain_db_to_lin(-6.0f) * gain_db_to_lin(3.0f);
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      expected_a[i] = (float)(i + 1u) * gain;
    }
    if (!check_buffer_values((const float *)graph.buffers[graph.num_buffers - 1u].data,
                             expected_a, BLOCK_FRAMES, spec->name)) {
      graph_unbind(&graph);
      return 1;
    }
  } else if (strcmp(spec->name, "gain_chain_4") == 0) {
    float gain = gain_db_to_lin(-6.0f) * gain_db_to_lin(3.0f) *
                 gain_db_to_lin(-3.0f) * gain_db_to_lin(6.0f);
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      expected_a[i] = (float)(i + 1u) * gain;
    }
    if (!check_buffer_values((const float *)graph.buffers[graph.num_buffers - 1u].data,
                             expected_a, BLOCK_FRAMES, spec->name)) {
      graph_unbind(&graph);
      return 1;
    }
  } else if (strcmp(spec->name, "split_two_gains") == 0) {
    float gain_a = gain_db_to_lin(-6.0f);
    float gain_b = gain_db_to_lin(6.0f);
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      expected_a[i] = (float)(i + 1u) * gain_a;
      expected_b[i] = (float)(i + 1u) * gain_b;
    }
    if (!check_buffer_values((const float *)graph.buffers[graph.num_buffers - 2u].data,
                             expected_a, BLOCK_FRAMES, "split_two_gains.a") ||
        !check_buffer_values((const float *)graph.buffers[graph.num_buffers - 1u].data,
                             expected_b, BLOCK_FRAMES, "split_two_gains.b")) {
      graph_unbind(&graph);
      return 1;
    }
  } else if (strcmp(spec->name, "dry_wet_mix") == 0) {
    float wet = gain_db_to_lin(-6.0f);
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      expected_a[i] = (float)(i + 1u) * (1.0f + wet);
    }
    if (!check_buffer_values((const float *)graph.buffers[graph.num_buffers - 1u].data,
                             expected_a, BLOCK_FRAMES, spec->name)) {
      graph_unbind(&graph);
      return 1;
    }
  } else if (strcmp(spec->name, "sum_two_inputs") == 0) {
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      expected_a[i] = (float)(i + 1u) + (0.25f * (float)(i + 1u));
    }
    if (!check_buffer_values((const float *)graph.buffers[graph.num_buffers - 1u].data,
                             expected_a, BLOCK_FRAMES, spec->name)) {
      graph_unbind(&graph);
      return 1;
    }
  } else if (strcmp(spec->name, "inplace_gain") == 0) {
    float gain = gain_db_to_lin(6.0f);
    for (i = 0; i < BLOCK_FRAMES; ++i) {
      expected_a[i] = (float)(i + 1u) * gain;
    }
    if (graph.num_buffers < 3u || graph.buffers[0].data != graph.buffers[1].data ||
        graph.buffers[0].data != graph.buffers[2].data) {
      fprintf(stderr, "expected inplace buffer aliasing was not preserved\n");
      graph_unbind(&graph);
      return 1;
    }
    if (!check_buffer_values((const float *)graph.buffers[graph.num_buffers - 1u].data,
                             expected_a, BLOCK_FRAMES, spec->name)) {
      graph_unbind(&graph);
      return 1;
    }
  } else {
    fprintf(stderr, "missing scenario handler: %s\n", spec->name);
    graph_unbind(&graph);
    return 1;
  }

  graph_unbind(&graph);
  return 0;
}
