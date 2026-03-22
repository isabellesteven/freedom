#ifndef FREEDOM_RUNTIME_TYPES_H
#define FREEDOM_RUNTIME_TYPES_H

#include <stdint.h>

#include "modules/module_abi.h"

typedef struct RuntimeHeap {
  uint32_t heap_id;
  uint32_t size_bytes;
  void *base;
} RuntimeHeap;

typedef struct RuntimeBufferView {
  uint32_t buffer_id;
  uint32_t heap_index;
  uint32_t offset_bytes;
  uint32_t size_bytes;
  void *data;
  uint16_t channels;
  uint16_t frames;
  uint16_t stride_bytes;
  uint8_t format;
  uint8_t buffer_type;
} RuntimeBufferView;

typedef struct RuntimeBufferBinding {
  RuntimeBufferView *buffer;
} RuntimeBufferBinding;

typedef struct RuntimeNodeInstance {
  uint32_t node_id;
  const AweModuleDescriptor *module;
  void *state;
  const uint8_t *init_blob;
  uint32_t init_bytes;
  const uint8_t *param_defaults;
  uint32_t param_defaults_bytes;
  RuntimeBufferBinding *inputs;
  uint32_t num_inputs;
  RuntimeBufferBinding *outputs;
  uint32_t num_outputs;
  const void **input_ptrs;
  void **output_ptrs;
  uint8_t initialized;
} RuntimeNodeInstance;

typedef struct RuntimeScheduleEntry {
  uint32_t node_id;
  uint32_t node_index;
  RuntimeNodeInstance *node;
} RuntimeScheduleEntry;

#endif
