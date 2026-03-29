#ifndef FREEDOM_RUNTIME_LOADER_BLOB_H
#define FREEDOM_RUNTIME_LOADER_BLOB_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

enum grph_blob_status {
  GRPH_BLOB_OK = 0,
  GRPH_BLOB_ERR_ARG = 1,
  GRPH_BLOB_ERR_FORMAT = 2,
  GRPH_BLOB_ERR_CRC = 3,
};

enum grph_sect_type {
  GRPH_SECT_REQUIRES = 1,
  GRPH_SECT_HEAPS = 2,
  GRPH_SECT_BUFFERS = 3,
  GRPH_SECT_NODES = 4,
  GRPH_SECT_SCHEDULE = 5,
  GRPH_SECT_PARAM_DEFAULTS = 6,
  GRPH_SECT_METADATA_MIN = 7,
  GRPH_SECT_GRAPH_CONFIG = 8,
};

typedef struct grph_graph_config {
  uint32_t sample_rate_hz;
  uint32_t block_multiple_n;
} grph_graph_config;

typedef struct grph_blob_section {
  uint32_t type;
  uint32_t flags;
  uint32_t crc32;
  /* Points into the caller-supplied blob bytes; payload storage is never copied. */
  const uint8_t *payload;
  uint32_t payload_bytes;
} grph_blob_section;

typedef struct grph_blob_view {
  /* Non-owning reference to the original immutable blob bytes. */
  const uint8_t *data;
  size_t data_bytes;

  uint8_t version_major;
  uint8_t version_minor;
  uint16_t header_bytes;
  uint32_t file_bytes;
  uint32_t target_abi;
  uint64_t graph_uuid_lo;
  uint64_t graph_uuid_hi;
  uint32_t file_crc32;

  /* Parsed section table stored by value with a fixed maximum capacity. */
  grph_blob_section sections[64];
  size_t section_count;

  /* These point to entries inside sections[] after successful parsing. */
  const grph_blob_section *requires;
  const grph_blob_section *heaps;
  const grph_blob_section *buffers;
  const grph_blob_section *nodes;
  const grph_blob_section *schedule;
  const grph_blob_section *param_defaults;
  const grph_blob_section *metadata_min;
  const grph_blob_section *graph_config;

  /* Decoded typed cache of GRAPH_CONFIG for downstream runtime use. */
  grph_graph_config graph_config_values;
} grph_blob_view;

typedef enum grph_blob_text_mode {
  GRPH_BLOB_TEXT_HUMAN = 0,
  GRPH_BLOB_TEXT_CANONICAL = 1,
} grph_blob_text_mode;

/*
 * Parse and structurally validate an immutable graph blob.
 *
 * On success, out receives a non-owning grph_blob_view into the caller-supplied
 * memory. The input blob bytes must remain valid and unchanged while the view is
 * in use. Successful parsing implies file structure, required sections, CRCs,
 * and GRAPH_CONFIG have been validated, and graph_config_values has been
 * decoded for downstream callers.
 */
int grph_blob_parse(const uint8_t *data, size_t data_bytes, grph_blob_view *out,
                    char *err, size_t err_cap);

/*
 * Render a validated blob as text.
 *
 * This is the main text output API. mode selects either the human-oriented
 * disassembly or the canonical normalized form used for equivalence checks.
 * The input blob memory follows the same ownership rules as grph_blob_parse().
 */
int grph_blob_dump(FILE *out, const uint8_t *data, size_t data_bytes,
                   grph_blob_text_mode mode, char *err, size_t err_cap);

/* Convenience wrapper for grph_blob_dump(..., GRPH_BLOB_TEXT_HUMAN, ...). */
int grph_blob_disassemble(FILE *out, const uint8_t *data, size_t data_bytes,
                          char *err, size_t err_cap);

/* Convenience wrapper for grph_blob_dump(..., GRPH_BLOB_TEXT_CANONICAL, ...). */
int grph_blob_disassemble_canonical(FILE *out, const uint8_t *data,
                                    size_t data_bytes, char *err,
                                    size_t err_cap);

#ifdef __cplusplus
}
#endif

#endif
