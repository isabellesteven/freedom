#include "runtime/host/file_io_host.h"

/*
 * Offline file host for exercising compiled blobs against WAV files.
 *
 * The current implementation assumes the existing homogeneous runtime model:
 * one graph-wide sample rate/block size, float32 mono graph I/O buffers
 * exposed as one WAV channel per graph input/output. It is intentionally narrow
 * and explicit so later offline test features can build on a stable path.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/engine/graph_instance.h"
#include "runtime/engine/module_registry.h"

#define HOST_BUFFER_TYPE_ALIAS 2u

typedef struct HostWavFile {
  uint32_t sample_rate_hz;
  uint16_t channels;
  uint32_t frame_count;
  float *samples;
} HostWavFile;

typedef struct HostGraphIo {
  RuntimeBufferView **inputs;
  RuntimeBufferView **outputs;
  uint32_t num_inputs;
  uint32_t num_outputs;
} HostGraphIo;

static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24));
}

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

static void host_diag(FILE *diag, const char *msg, const char *path) {
  if (!diag) {
    return;
  }
  if (path) {
    fprintf(diag, "%s: %s\n", msg, path);
  } else {
    fprintf(diag, "%s\n", msg);
  }
}

static int read_file_bytes(const char *path, uint8_t **out_bytes, size_t *out_size) {
  FILE *f;
  long file_size;
  uint8_t *bytes;
  size_t got;

  if (!path || !out_bytes || !out_size) {
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
  file_size = ftell(f);
  if (file_size < 0) {
    fclose(f);
    return 0;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return 0;
  }
  bytes = (uint8_t *)malloc((size_t)file_size);
  if (!bytes) {
    fclose(f);
    return 0;
  }
  got = fread(bytes, 1u, (size_t)file_size, f);
  fclose(f);
  if (got != (size_t)file_size) {
    free(bytes);
    return 0;
  }
  *out_bytes = bytes;
  *out_size = (size_t)file_size;
  return 1;
}

/* The host derives base_block_frames from the homogeneous buffer shape emitted
   by the current compiler rather than introducing a new runtime API. */
static int derive_base_block_frames(const BlobView *blob, uint32_t *out_base_block_frames) {
  const uint8_t *buffers_payload;
  uint32_t count;
  uint32_t frames;
  uint32_t block_multiple_n;
  if (!blob || !blob->buffers || !blob->graph_config || !out_base_block_frames) {
    return 0;
  }
  buffers_payload = blob->buffers->payload;
  if (!buffers_payload || blob->buffers->payload_bytes < 36u) {
    return 0;
  }
  count = rd_u32(buffers_payload);
  if (count == 0u) {
    return 0;
  }
  frames = rd_u16(buffers_payload + 34u);
  block_multiple_n = blob->graph_config_values.block_multiple_n;
  if (frames == 0u || block_multiple_n == 0u || (frames % block_multiple_n) != 0u) {
    return 0;
  }
  *out_base_block_frames = frames / block_multiple_n;
  return 1;
}

static int collect_graph_io(GraphInstance *graph, HostGraphIo *out_io) {
  uint8_t *produced;
  uint8_t *consumed;
  uint8_t *has_later_same_data;
  uint32_t i;
  uint32_t j;
  RuntimeBufferView **inputs;
  RuntimeBufferView **outputs;
  uint32_t num_inputs;
  uint32_t num_outputs;

  if (!graph || !out_io || !graph->buffers || !graph->schedule) {
    return 0;
  }

  produced = (uint8_t *)calloc(graph->num_buffers, 1u);
  consumed = (uint8_t *)calloc(graph->num_buffers, 1u);
  has_later_same_data = (uint8_t *)calloc(graph->num_buffers, 1u);
  inputs = (RuntimeBufferView **)calloc(graph->num_buffers, sizeof(RuntimeBufferView *));
  outputs = (RuntimeBufferView **)calloc(graph->num_buffers, sizeof(RuntimeBufferView *));
  if (!produced || !consumed || !has_later_same_data || !inputs || !outputs) {
    free(produced);
    free(consumed);
    free(has_later_same_data);
    free(inputs);
    free(outputs);
    return 0;
  }

  for (i = 0; i < graph->schedule_length; ++i) {
    RuntimeNodeInstance *node = graph->schedule[i].node;
    if (!node) {
      free(produced);
      free(consumed);
      free(inputs);
      free(outputs);
      return 0;
    }
    for (j = 0; j < node->num_inputs; ++j) {
      RuntimeBufferView *buffer = node->inputs[j].buffer;
      if (!buffer || buffer < graph->buffers ||
          buffer >= graph->buffers + graph->num_buffers) {
        free(produced);
        free(consumed);
        free(has_later_same_data);
        free(inputs);
        free(outputs);
        return 0;
      }
      consumed[(uint32_t)(buffer - graph->buffers)] = 1u;
    }
    for (j = 0; j < node->num_outputs; ++j) {
      RuntimeBufferView *buffer = node->outputs[j].buffer;
      if (!buffer || buffer < graph->buffers ||
          buffer >= graph->buffers + graph->num_buffers) {
        free(produced);
        free(consumed);
        free(has_later_same_data);
        free(inputs);
        free(outputs);
        return 0;
      }
      produced[(uint32_t)(buffer - graph->buffers)] = 1u;
    }
  }

  for (i = 0; i < graph->num_buffers; ++i) {
    for (j = i + 1u; j < graph->num_buffers; ++j) {
      if (graph->buffers[i].data == graph->buffers[j].data) {
        has_later_same_data[i] = 1u;
        break;
      }
    }
  }

  num_inputs = 0u;
  num_outputs = 0u;
  for (i = 0; i < graph->num_buffers; ++i) {
    RuntimeBufferView *buffer = &graph->buffers[i];
    if (!produced[i] && buffer->buffer_type != HOST_BUFFER_TYPE_ALIAS) {
      inputs[num_inputs++] = buffer;
    }
    if (!consumed[i] && !has_later_same_data[i]) {
      outputs[num_outputs++] = buffer;
    }
  }

  free(produced);
  free(consumed);
  free(has_later_same_data);

  if (num_inputs == 0u || num_outputs == 0u) {
    free(inputs);
    free(outputs);
    return 0;
  }

  out_io->inputs = inputs;
  out_io->outputs = outputs;
  out_io->num_inputs = num_inputs;
  out_io->num_outputs = num_outputs;
  return 1;
}

static void free_graph_io(HostGraphIo *graph_io) {
  if (!graph_io) {
    return;
  }
  free(graph_io->inputs);
  free(graph_io->outputs);
  memset(graph_io, 0, sizeof(*graph_io));
}

static int validate_graph_io_shape(const GraphInstance *graph, const HostGraphIo *graph_io,
                                   FILE *diag) {
  uint32_t i;
  if (!graph || !graph_io) {
    return 0;
  }
  for (i = 0; i < graph_io->num_inputs; ++i) {
    RuntimeBufferView *buffer = graph_io->inputs[i];
    if (!buffer || buffer->format != AWE_FMT_F32 || buffer->channels != 1u ||
        buffer->frames != graph->block_frames) {
      host_diag(diag, "unsupported graph input shape", NULL);
      return 0;
    }
  }
  for (i = 0; i < graph_io->num_outputs; ++i) {
    RuntimeBufferView *buffer = graph_io->outputs[i];
    if (!buffer || buffer->format != AWE_FMT_F32 || buffer->channels != 1u ||
        buffer->frames != graph->block_frames) {
      host_diag(diag, "unsupported graph output shape", NULL);
      return 0;
    }
  }
  return 1;
}

/* WAV support intentionally stays narrow: RIFF/WAVE with one fmt chunk, one
   data chunk, and IEEE float32 samples. That matches the current runtime path
   without introducing unrelated media-framework complexity. */
static int read_wav_file(const char *path, HostWavFile *out_wav, FILE *diag) {
  uint8_t *bytes;
  size_t size;
  uint32_t fmt_tag;
  uint16_t channels = 0u;
  uint32_t sample_rate_hz = 0u;
  uint16_t bits_per_sample = 0u;
  uint32_t data_offset = 0u;
  uint32_t data_bytes = 0u;
  uint32_t offset;

  if (!path || !out_wav) {
    return 0;
  }
  memset(out_wav, 0, sizeof(*out_wav));
  if (!read_file_bytes(path, &bytes, &size)) {
    host_diag(diag, "failed to read wav", path);
    return 0;
  }
  if (size < 44u || memcmp(bytes, "RIFF", 4u) != 0 || memcmp(bytes + 8u, "WAVE", 4u) != 0) {
    host_diag(diag, "bad wav file", path);
    free(bytes);
    return 0;
  }

  fmt_tag = 0u;
  offset = 12u;
  while (offset + 8u <= (uint32_t)size) {
    uint32_t chunk_size = rd_u32(bytes + offset + 4u);
    uint32_t chunk_data = offset + 8u;
    uint32_t padded_size = chunk_size + (chunk_size & 1u);
    if (chunk_data + chunk_size > (uint32_t)size) {
      host_diag(diag, "bad wav file", path);
      free(bytes);
      return 0;
    }
    if (memcmp(bytes + offset, "fmt ", 4u) == 0) {
      if (chunk_size < 16u) {
        host_diag(diag, "bad wav file", path);
        free(bytes);
        return 0;
      }
      fmt_tag = rd_u16(bytes + chunk_data);
      channels = rd_u16(bytes + chunk_data + 2u);
      sample_rate_hz = rd_u32(bytes + chunk_data + 4u);
      bits_per_sample = rd_u16(bytes + chunk_data + 14u);
    } else if (memcmp(bytes + offset, "data", 4u) == 0) {
      data_offset = chunk_data;
      data_bytes = chunk_size;
    }
    offset = chunk_data + padded_size;
  }

  if (fmt_tag != 3u || channels == 0u || bits_per_sample != 32u || data_bytes == 0u) {
    host_diag(diag, "unsupported wav format", path);
    free(bytes);
    return 0;
  }
  if ((data_bytes % ((uint32_t)channels * 4u)) != 0u) {
    host_diag(diag, "bad wav file", path);
    free(bytes);
    return 0;
  }

  out_wav->frame_count = data_bytes / ((uint32_t)channels * 4u);
  out_wav->channels = channels;
  out_wav->sample_rate_hz = sample_rate_hz;
  out_wav->samples = (float *)malloc((size_t)data_bytes);
  if (!out_wav->samples) {
    free(bytes);
    return 0;
  }
  memcpy(out_wav->samples, bytes + data_offset, (size_t)data_bytes);
  free(bytes);
  return 1;
}

static void free_wav_file(HostWavFile *wav) {
  if (!wav) {
    return;
  }
  free(wav->samples);
  memset(wav, 0, sizeof(*wav));
}

static int write_wav_file(const char *path, const HostWavFile *wav, FILE *diag) {
  FILE *f;
  uint32_t data_bytes;
  uint32_t riff_bytes;
  uint8_t header[44];

  if (!path || !wav || !wav->samples) {
    return 0;
  }

  data_bytes = wav->frame_count * (uint32_t)wav->channels * 4u;
  riff_bytes = 36u + data_bytes;
  memcpy(header + 0u, "RIFF", 4u);
  wr_u32(header + 4u, riff_bytes);
  memcpy(header + 8u, "WAVE", 4u);
  memcpy(header + 12u, "fmt ", 4u);
  wr_u32(header + 16u, 16u);
  wr_u16(header + 20u, 3u);
  wr_u16(header + 22u, wav->channels);
  wr_u32(header + 24u, wav->sample_rate_hz);
  wr_u32(header + 28u, wav->sample_rate_hz * (uint32_t)wav->channels * 4u);
  wr_u16(header + 32u, (uint16_t)(wav->channels * 4u));
  wr_u16(header + 34u, 32u);
  memcpy(header + 36u, "data", 4u);
  wr_u32(header + 40u, data_bytes);

  f = fopen(path, "wb");
  if (!f) {
    host_diag(diag, "failed to write wav", path);
    return 0;
  }
  if (fwrite(header, 1u, sizeof(header), f) != sizeof(header) ||
      fwrite(wav->samples, 1u, (size_t)data_bytes, f) != (size_t)data_bytes) {
    fclose(f);
    host_diag(diag, "failed to write wav", path);
    return 0;
  }
  fclose(f);
  return 1;
}

static grph_file_host_status process_wav(GraphInstance *graph, const HostGraphIo *graph_io,
                                         const HostWavFile *input_wav,
                                         HostWavFile *output_wav, FILE *diag) {
  uint32_t block_frames;
  uint32_t frame_offset;
  uint64_t block_index;
  uint32_t channel;

  if (!graph || !graph_io || !input_wav || !output_wav) {
    return GRPH_FILE_HOST_BAD_ARG;
  }
  if (input_wav->sample_rate_hz != graph->sample_rate_hz ||
      input_wav->channels != graph_io->num_inputs) {
    host_diag(diag, "wav properties do not match graph input shape", NULL);
    return GRPH_FILE_HOST_UNSUPPORTED_WAV;
  }

  output_wav->sample_rate_hz = graph->sample_rate_hz;
  output_wav->channels = (uint16_t)graph_io->num_outputs;
  output_wav->frame_count = input_wav->frame_count;
  output_wav->samples = (float *)calloc(
      (size_t)output_wav->frame_count * (size_t)output_wav->channels, sizeof(float));
  if (!output_wav->samples) {
    return GRPH_FILE_HOST_MEMORY_FAILED;
  }

  block_frames = graph->block_frames;
  block_index = 0u;
  for (frame_offset = 0u; frame_offset < input_wav->frame_count; frame_offset += block_frames) {
    uint32_t i;
    uint32_t valid_frames = input_wav->frame_count - frame_offset;
    if (valid_frames > block_frames) {
      valid_frames = block_frames;
    }

    /* Final partial blocks are zero-padded on input and truncated back to the
       original frame count when copied into the output WAV. */
    for (channel = 0u; channel < graph_io->num_inputs; ++channel) {
      float *dst = (float *)graph_io->inputs[channel]->data;
      memset(dst, 0, (size_t)block_frames * sizeof(float));
      for (i = 0u; i < valid_frames; ++i) {
        dst[i] = input_wav->samples[((size_t)(frame_offset + i) * input_wav->channels) + channel];
      }
    }
    if (graph_process(graph, block_index) != GRAPH_STATUS_OK) {
      host_diag(diag, "graph processing failed", NULL);
      return GRPH_FILE_HOST_PROCESS_FAILED;
    }

    for (channel = 0u; channel < graph_io->num_outputs; ++channel) {
      const float *src = (const float *)graph_io->outputs[channel]->data;
      for (i = 0u; i < valid_frames; ++i) {
        output_wav->samples[((size_t)(frame_offset + i) * output_wav->channels) + channel] =
            src[i];
      }
    }
    ++block_index;
  }

  return GRPH_FILE_HOST_OK;
}

grph_file_host_status grph_file_host_run(const grph_file_host_options *options,
                                         FILE *diag) {
  grph_file_host_status host_status;
  uint8_t *blob_bytes;
  size_t blob_size;
  BlobView blob;
  char blob_err[256];
  uint32_t base_block_frames;
  const ModuleRegistry *registry;
  GraphMemoryRequirements req;
  uint32_t *heap_sizes;
  RuntimeHostConfig host_cfg;
  RuntimeMemoryConfig mem_cfg;
  GraphInstance graph;
  HostGraphIo graph_io;
  HostWavFile input_wav;
  HostWavFile output_wav;
  void *metadata_mem;
  void *state_mem;
  void **heap_bases;
  uint32_t i;

  if (!options || !options->blob_path || !options->input_wav_path ||
      !options->output_wav_path) {
    return GRPH_FILE_HOST_BAD_ARG;
  }

  memset(&graph, 0, sizeof(graph));
  memset(&graph_io, 0, sizeof(graph_io));
  memset(&input_wav, 0, sizeof(input_wav));
  memset(&output_wav, 0, sizeof(output_wav));
  memset(&req, 0, sizeof(req));
  blob_bytes = NULL;
  metadata_mem = NULL;
  state_mem = NULL;
  heap_bases = NULL;
  heap_sizes = NULL;
  host_status = GRPH_FILE_HOST_OK;

  if (!read_file_bytes(options->blob_path, &blob_bytes, &blob_size)) {
    host_diag(diag, "failed to read blob", options->blob_path);
    return GRPH_FILE_HOST_IO_ERROR;
  }
  if (grph_blob_parse(blob_bytes, blob_size, &blob, blob_err, sizeof(blob_err)) !=
      GRPH_BLOB_OK) {
    host_diag(diag, blob_err, options->blob_path);
    host_status = GRPH_FILE_HOST_INVALID_BLOB;
    goto cleanup;
  }
  if (!derive_base_block_frames(&blob, &base_block_frames)) {
    host_diag(diag, "failed to derive host block size from blob", NULL);
    host_status = GRPH_FILE_HOST_INVALID_BLOB;
    goto cleanup;
  }

  registry = grph_builtin_module_registry();
  if (!registry || !grph_module_registry_validate(registry)) {
    host_diag(diag, "built-in module registry is unavailable", NULL);
    host_status = GRPH_FILE_HOST_BIND_FAILED;
    goto cleanup;
  }

  if (graph_get_memory_requirements(&blob, registry, &req, NULL, 0u) != GRAPH_STATUS_OK) {
    host_diag(diag, "failed to size graph memory", NULL);
    host_status = GRPH_FILE_HOST_BIND_FAILED;
    goto cleanup;
  }

  heap_sizes = (uint32_t *)calloc(req.num_heaps, sizeof(uint32_t));
  heap_bases = (void **)calloc(req.num_heaps, sizeof(void *));
  metadata_mem = calloc(1u, req.metadata_bytes);
  state_mem = calloc(1u, req.module_state_bytes);
  if (!heap_sizes || !heap_bases || !metadata_mem || !state_mem) {
    host_status = GRPH_FILE_HOST_MEMORY_FAILED;
    goto cleanup;
  }
  if (graph_get_memory_requirements(&blob, registry, &req, heap_sizes, req.num_heaps) !=
      GRAPH_STATUS_OK) {
    host_diag(diag, "failed to size graph heaps", NULL);
    host_status = GRPH_FILE_HOST_BIND_FAILED;
    goto cleanup;
  }
  for (i = 0u; i < req.num_heaps; ++i) {
    heap_bases[i] = calloc(1u, heap_sizes[i]);
    if (!heap_bases[i]) {
      host_status = GRPH_FILE_HOST_MEMORY_FAILED;
      goto cleanup;
    }
  }

  host_cfg.base_block_frames = base_block_frames;
  mem_cfg.metadata_mem = metadata_mem;
  mem_cfg.metadata_mem_size = req.metadata_bytes;
  mem_cfg.module_state_mem = state_mem;
  mem_cfg.module_state_mem_size = req.module_state_bytes;
  mem_cfg.heap_bases = heap_bases;
  mem_cfg.heap_sizes = heap_sizes;
  mem_cfg.num_heaps = req.num_heaps;

  if (graph_bind_from_blob(&blob, registry, &host_cfg, &mem_cfg, &graph) != GRAPH_STATUS_OK) {
    host_diag(diag, "graph bind failed", NULL);
    host_status = GRPH_FILE_HOST_BIND_FAILED;
    goto cleanup;
  }
  if (!collect_graph_io(&graph, &graph_io) || !validate_graph_io_shape(&graph, &graph_io, diag)) {
    host_status = GRPH_FILE_HOST_BIND_FAILED;
    goto cleanup;
  }
  if (!read_wav_file(options->input_wav_path, &input_wav, diag)) {
    host_status = GRPH_FILE_HOST_UNSUPPORTED_WAV;
    goto cleanup;
  }

  host_status = process_wav(&graph, &graph_io, &input_wav, &output_wav, diag);
  if (host_status != GRPH_FILE_HOST_OK) {
    goto cleanup;
  }
  if (!write_wav_file(options->output_wav_path, &output_wav, diag)) {
    host_status = GRPH_FILE_HOST_IO_ERROR;
    goto cleanup;
  }

cleanup:
  graph_unbind(&graph);
  free_graph_io(&graph_io);
  free_wav_file(&input_wav);
  free_wav_file(&output_wav);
  if (heap_bases) {
    for (i = 0u; i < req.num_heaps; ++i) {
      free(heap_bases[i]);
    }
  }
  free(heap_bases);
  free(heap_sizes);
  free(metadata_mem);
  free(state_mem);
  free(blob_bytes);
  return host_status;
}
