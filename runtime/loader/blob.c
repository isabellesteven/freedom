#include "runtime/loader/blob.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define GRPH_FILE_HEADER_BYTES 32u
#define GRPH_SECTION_HEADER_BYTES 16u
#define GRPH_TRAILER_CRC_BYTES 4u

static uint16_t rd_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_u32(const uint8_t *p) {
  return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24));
}

static uint64_t rd_u64(const uint8_t *p) {
  return (uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32);
}

static float rd_f32(const uint8_t *p) {
  uint32_t u = rd_u32(p);
  float f = 0.0f;
  memcpy(&f, &u, sizeof(float));
  return f;
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

static int fail(char *err, size_t err_cap, int code, const char *fmt, ...) {
  if (err && err_cap > 0) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, err_cap, fmt, ap);
    va_end(ap);
  }
  return code;
}

static const char *sect_name(uint32_t type) {
  switch (type) {
    case GRPH_SECT_REQUIRES:
      return "REQUIRES";
    case GRPH_SECT_HEAPS:
      return "HEAPS";
    case GRPH_SECT_BUFFERS:
      return "BUFFERS";
    case GRPH_SECT_NODES:
      return "NODES";
    case GRPH_SECT_SCHEDULE:
      return "SCHEDULE";
    case GRPH_SECT_PARAM_DEFAULTS:
      return "PARAM_DEFAULTS";
    case GRPH_SECT_METADATA_MIN:
      return "METADATA_MIN";
    case GRPH_SECT_GRAPH_CONFIG:
      return "GRAPH_CONFIG";
    default:
      return "UNKNOWN";
  }
}

static const char *heap_kind_name(uint32_t kind) {
  switch (kind) {
    case 0:
      return "SRAM";
    case 1:
      return "PSRAM";
    case 2:
      return "PARAM";
    case 3:
      return "STATE";
    case 4:
      return "IO";
    default:
      return "UNK";
  }
}

static const char *buf_type_name(uint8_t t) {
  switch (t) {
    case 0:
      return "OWNED";
    case 1:
      return "VIEW";
    case 2:
      return "ALIAS";
    default:
      return "UNK";
  }
}

static const char *fmt_name(uint8_t f) {
  switch (f) {
    case 1:
      return "F32";
    case 2:
      return "S16";
    default:
      return "UNK";
  }
}

static const char *module_name(uint32_t module_id) {
  switch (module_id) {
    case 0x00001001u:
      return "Gain";
    case 0x00001002u:
      return "Sum2";
    default:
      return "?";
  }
}

static void abi_to_str(uint32_t abi, char out[5]) {
  int i;
  for (i = 0; i < 4; ++i) {
    uint8_t c = (uint8_t)((abi >> (8 * i)) & 0xFFu);
    out[i] = (c >= 32u && c <= 126u) ? (char)c : '?';
  }
  out[4] = '\0';
}

static int is_canonical_mode(grph_blob_text_mode mode) {
  return mode == GRPH_BLOB_TEXT_CANONICAL;
}

static void print_hex_bytes(FILE *out, const uint8_t *data, uint32_t bytes) {
  uint32_t i;
  fprintf(out, "hex(");
  for (i = 0; i < bytes; ++i) {
    fprintf(out, "%02X", data[i]);
  }
  fprintf(out, ")");
}

int grph_blob_parse(const uint8_t *data, size_t data_bytes, grph_blob_view *out,
                    char *err, size_t err_cap) {
  size_t at;
  if (!data || !out) {
    return fail(err, err_cap, GRPH_BLOB_ERR_ARG, "null argument");
  }
  if (data_bytes < (GRPH_FILE_HEADER_BYTES + GRPH_TRAILER_CRC_BYTES)) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "blob too small");
  }
  if (memcmp(data, "GRPH", 4) != 0) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "bad magic");
  }

  memset(out, 0, sizeof(*out));
  out->data = data;
  out->data_bytes = data_bytes;
  out->version_major = data[4];
  out->version_minor = data[5];
  out->header_bytes = rd_u16(data + 6);
  out->file_bytes = rd_u32(data + 8);
  out->target_abi = rd_u32(data + 12);
  out->graph_uuid_lo = rd_u64(data + 16);
  out->graph_uuid_hi = rd_u64(data + 24);

  if (out->header_bytes != GRPH_FILE_HEADER_BYTES) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                "header_bytes=%u expected 32", out->header_bytes);
  }
  if (out->file_bytes != data_bytes) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "file_bytes=%u actual=%zu",
                out->file_bytes, data_bytes);
  }

  out->file_crc32 = rd_u32(data + data_bytes - GRPH_TRAILER_CRC_BYTES);
  {
    uint32_t got = crc32_eth(data, data_bytes - GRPH_TRAILER_CRC_BYTES);
    if (got != out->file_crc32) {
      return fail(err, err_cap, GRPH_BLOB_ERR_CRC,
                  "file crc mismatch expected=0x%08" PRIX32
                  " actual=0x%08" PRIX32,
                  out->file_crc32, got);
    }
  }

  at = GRPH_FILE_HEADER_BYTES;
  while (at < data_bytes - GRPH_TRAILER_CRC_BYTES) {
    grph_blob_section s;
    const uint8_t *hdr;
    size_t payload_end;

    if ((data_bytes - GRPH_TRAILER_CRC_BYTES) - at < GRPH_SECTION_HEADER_BYTES) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "truncated section header at %zu", at);
    }
    if (out->section_count >= (sizeof(out->sections) / sizeof(out->sections[0]))) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "too many sections");
    }

    hdr = data + at;
    s.type = rd_u32(hdr + 0);
    s.payload_bytes = rd_u32(hdr + 4);
    s.flags = rd_u32(hdr + 8);
    s.crc32 = rd_u32(hdr + 12);
    at += GRPH_SECTION_HEADER_BYTES;

    payload_end = at + s.payload_bytes;
    if (payload_end > (data_bytes - GRPH_TRAILER_CRC_BYTES)) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "section %s overruns file", sect_name(s.type));
    }
    s.payload = data + at;
    at = payload_end;

    if (s.crc32 != 0u) {
      uint32_t got = crc32_eth(s.payload, s.payload_bytes);
      if (got != s.crc32) {
        return fail(err, err_cap, GRPH_BLOB_ERR_CRC, "section %s crc mismatch",
                    sect_name(s.type));
      }
    }

    out->sections[out->section_count++] = s;
    switch (s.type) {
      case GRPH_SECT_REQUIRES:
        if (out->requires) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "duplicate REQUIRES");
        }
        out->requires = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_HEAPS:
        if (out->heaps) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "duplicate HEAPS");
        }
        out->heaps = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_BUFFERS:
        if (out->buffers) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "duplicate BUFFERS");
        }
        out->buffers = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_NODES:
        if (out->nodes) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "duplicate NODES");
        }
        out->nodes = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_SCHEDULE:
        if (out->schedule) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "duplicate SCHEDULE");
        }
        out->schedule = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_PARAM_DEFAULTS:
        if (out->param_defaults) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                      "duplicate PARAM_DEFAULTS");
        }
        out->param_defaults = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_METADATA_MIN:
        if (out->metadata_min) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                      "duplicate METADATA_MIN");
        }
        out->metadata_min = &out->sections[out->section_count - 1];
        break;
      case GRPH_SECT_GRAPH_CONFIG:
        if (out->graph_config) {
          return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                      "duplicate GRAPH_CONFIG");
        }
        out->graph_config = &out->sections[out->section_count - 1];
        break;
      default:
        break;
    }
  }

  if (at != data_bytes - GRPH_TRAILER_CRC_BYTES) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "trailing data before crc");
  }
  if (!out->requires || !out->heaps || !out->buffers || !out->nodes ||
      !out->schedule || !out->param_defaults || !out->graph_config) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                "missing required section");
  }

  if (out->graph_config->payload_bytes != 8u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                "GRAPH_CONFIG payload_bytes=%" PRIu32 " expected 8",
                out->graph_config->payload_bytes);
  }
  out->graph_config_values.sample_rate_hz = rd_u32(out->graph_config->payload + 0);
  out->graph_config_values.block_multiple_n =
      rd_u32(out->graph_config->payload + 4);
  if (out->graph_config_values.sample_rate_hz == 0u ||
      out->graph_config_values.block_multiple_n == 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                "GRAPH_CONFIG values must be non-zero");
  }

  return GRPH_BLOB_OK;
}

static int disasm_graph_config(FILE *out, const grph_graph_config *cfg,
                               grph_blob_text_mode mode, char *err,
                               size_t err_cap) {
  if (!cfg) {
    return fail(err, err_cap, GRPH_BLOB_ERR_ARG, "null graph config");
  }

  fprintf(out, "[GRAPH_CONFIG]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "sample_rate_hz=%" PRIu32 "\n", cfg->sample_rate_hz);
    fprintf(out, "block_multiple_N=%" PRIu32 "\n", cfg->block_multiple_n);
  } else {
    fprintf(out, "  sample_rate_hz=%" PRIu32 "\n", cfg->sample_rate_hz);
    fprintf(out, "  block_multiple_N=%" PRIu32 "\n", cfg->block_multiple_n);
  }
  return GRPH_BLOB_OK;
}

static int disasm_requires(FILE *out, const grph_blob_section *s,
                           grph_blob_text_mode mode, char *err,
                           size_t err_cap) {
  const uint8_t *p = s->payload;
  size_t n = s->payload_bytes;
  uint32_t count;
  uint32_t i;
  if (n < 4u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "REQUIRES truncated");
  }
  count = rd_u32(p);
  p += 4;
  n -= 4;

  fprintf(out, "[REQUIRES]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "count=%" PRIu32 "\n", count);
  } else {
    fprintf(out, "  count=%" PRIu32 "\n", count);
  }
  for (i = 0; i < count; ++i) {
    uint32_t module_id;
    uint16_t vmaj;
    uint16_t vmin;
    uint32_t caps;
    if (n < 12u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "REQUIRES entry truncated");
    }
    module_id = rd_u32(p + 0);
    vmaj = rd_u16(p + 4);
    vmin = rd_u16(p + 6);
    caps = rd_u32(p + 8);
    if (is_canonical_mode(mode)) {
      fprintf(out, "module 0x%08" PRIX32 " ver=%u.%u caps=0x%08" PRIX32 "\n",
              module_id, (unsigned)vmaj, (unsigned)vmin, caps);
    } else {
      fprintf(out, "  module 0x%08" PRIX32 " ver=%u.%u caps=0x%08" PRIX32
                   "   ; %s\n",
              module_id, (unsigned)vmaj, (unsigned)vmin, caps,
              module_name(module_id));
    }
    p += 12;
    n -= 12;
  }
  if (n != 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "REQUIRES trailing bytes");
  }
  return GRPH_BLOB_OK;
}

static int disasm_heaps(FILE *out, const grph_blob_section *s,
                        grph_blob_text_mode mode, char *err,
                        size_t err_cap) {
  const uint8_t *p = s->payload;
  size_t n = s->payload_bytes;
  uint32_t count;
  uint32_t i;
  if (n < 4u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "HEAPS truncated");
  }
  count = rd_u32(p);
  p += 4;
  n -= 4;

  fprintf(out, "[HEAPS]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "count=%" PRIu32 "\n", count);
  }
  for (i = 0; i < count; ++i) {
    uint32_t heap_id;
    uint32_t kind;
    uint32_t bytes;
    uint32_t align;
    if (n < 16u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "HEAPS entry truncated");
    }
    heap_id = rd_u32(p + 0);
    kind = rd_u32(p + 4);
    bytes = rd_u32(p + 8);
    align = rd_u32(p + 12);
    if (is_canonical_mode(mode)) {
      fprintf(out, "heap id=%" PRIu32 " kind=%s bytes=%" PRIu32 " align=%" PRIu32
                   "\n",
              heap_id, heap_kind_name(kind), bytes, align);
    } else {
      fprintf(out, "  heap id=%" PRIu32 " kind=%-5s bytes=%" PRIu32
                   " align=%" PRIu32 "\n",
              heap_id, heap_kind_name(kind), bytes, align);
    }
    p += 16;
    n -= 16;
  }
  if (n != 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "HEAPS trailing bytes");
  }
  return GRPH_BLOB_OK;
}

static const char *buf_comment(uint32_t id) {
  if (id == 1u) {
    return "mic";
  }
  if (id == 2u) {
    return "spk";
  }
  return NULL;
}

static int disasm_buffers(FILE *out, const grph_blob_section *s,
                          grph_blob_text_mode mode, char *err,
                          size_t err_cap) {
  const uint8_t *p = s->payload;
  size_t n = s->payload_bytes;
  uint32_t count;
  uint32_t i;
  if (n < 4u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "BUFFERS truncated");
  }
  count = rd_u32(p);
  p += 4;
  n -= 4;

  fprintf(out, "[BUFFERS]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "count=%" PRIu32 "\n", count);
  }
  for (i = 0; i < count; ++i) {
    uint32_t buf_id;
    uint8_t buf_type;
    uint8_t fmt;
    uint32_t heap_id;
    uint32_t off;
    uint32_t size;
    uint16_t slots;
    uint16_t stride;
    uint32_t base_buf_id;
    uint16_t ch;
    uint16_t frames;
    const char *comment;
    if (n < 32u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "BUFFERS entry truncated");
    }
    buf_id = rd_u32(p + 0);
    buf_type = p[4];
    fmt = p[5];
    heap_id = rd_u32(p + 8);
    off = rd_u32(p + 12);
    size = rd_u32(p + 16);
    slots = rd_u16(p + 20);
    stride = rd_u16(p + 22);
    base_buf_id = rd_u32(p + 24);
    ch = rd_u16(p + 28);
    frames = rd_u16(p + 30);

    if (is_canonical_mode(mode)) {
      fprintf(out,
              "buf id=%" PRIu32
              " type=%s alias_of=%" PRIu32
              " fmt=%s heap=%" PRIu32
              " off=%" PRIu32
              " size=%" PRIu32
              " slots=%u stride=%u base=0 ch=%u frames=%u\n",
              buf_id, buf_type_name(buf_type), base_buf_id, fmt_name(fmt),
              heap_id, off, size, (unsigned)slots, (unsigned)stride,
              (unsigned)ch, (unsigned)frames);
    } else {
      fprintf(out, "  buf id=%" PRIu32 " type=%-5s fmt=%-3s heap=%" PRIu32
                   " off=%-3" PRIu32 " size=%" PRIu32
                   " slots=%u stride=%u base=%" PRIu32 " ch=%u frames=%u",
              buf_id, buf_type_name(buf_type), fmt_name(fmt), heap_id, off,
              size, (unsigned)slots, (unsigned)stride, base_buf_id,
              (unsigned)ch, (unsigned)frames);
      comment = buf_comment(buf_id);
      if (comment) {
        fprintf(out, "   ; %s", comment);
      }
      fprintf(out, "\n");
    }
    p += 32;
    n -= 32;
  }
  if (n != 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "BUFFERS trailing bytes");
  }
  return GRPH_BLOB_OK;
}

static int disasm_nodes(FILE *out, const grph_blob_section *nodes,
                        const grph_blob_section *params,
                        grph_blob_text_mode mode, char *err,
                        size_t err_cap) {
  const uint8_t *p = nodes->payload;
  size_t n = nodes->payload_bytes;
  uint32_t count;
  uint32_t i;

  if (n < 4u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "NODES truncated");
  }
  count = rd_u32(p);
  p += 4;
  n -= 4;

  fprintf(out, "[NODES]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "count=%" PRIu32 "\n", count);
  }
  for (i = 0; i < count; ++i) {
    uint32_t node_id;
    uint32_t module_id;
    uint32_t state_heap;
    uint32_t state_bytes;
    uint32_t state_align;
    uint32_t init_bytes;
    uint32_t param_block_bytes;
    float gain_db = 0.0f;

    if (n < 32u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "NODES entry truncated");
    }
    node_id = rd_u32(p + 0);
    module_id = rd_u32(p + 4);
    state_heap = rd_u32(p + 8);
    state_bytes = rd_u32(p + 16);
    state_align = rd_u32(p + 20);
    init_bytes = rd_u32(p + 24);
    param_block_bytes = rd_u32(p + 28);

    p += 32;
    n -= 32;
    if (n < init_bytes) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "NODES init truncated");
    }

    if (is_canonical_mode(mode)) {
      fprintf(out, "node id=%" PRIu32 " module=0x%08" PRIX32
                   " state_heap=%" PRIu32 " state_bytes=%" PRIu32
                   " align=%" PRIu32 " init_bytes=%" PRIu32
                   " param_block_bytes=%" PRIu32 "\n",
              node_id, module_id, state_heap, state_bytes, state_align,
              init_bytes, param_block_bytes);
    } else {
      fprintf(out, "  node id=%" PRIu32 " module=0x%08" PRIX32
                   " state_heap=%" PRIu32 " state_bytes=%" PRIu32
                   " align=%" PRIu32 " init_bytes=%" PRIu32
                   " param_block_bytes=%" PRIu32 "\n",
              node_id, module_id, state_heap, state_bytes, state_align,
              init_bytes, param_block_bytes);
    }

    if (!is_canonical_mode(mode) && module_id == 0x00001001u && init_bytes >= 8u) {
      gain_db = rd_f32(p + 4);
      fprintf(out, "    init: gain_db=%.1f\n", gain_db);
      fprintf(out, "    params_default: (param_id=1 f32 %.1f)\n", gain_db);
    }

    p += init_bytes;
    n -= init_bytes;
  }

  if (n != 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "NODES trailing bytes");
  }

  (void)params;
  return GRPH_BLOB_OK;
}

static int disasm_schedule(FILE *out, const grph_blob_section *s,
                           grph_blob_text_mode mode, char *err,
                           size_t err_cap) {
  const uint8_t *p = s->payload;
  size_t n = s->payload_bytes;
  uint32_t count;
  uint32_t i;
  if (n < 4u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "SCHEDULE truncated");
  }
  count = rd_u32(p);
  p += 4;
  n -= 4;

  fprintf(out, "[SCHEDULE]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "op_count=%" PRIu32 "\n", count);
  } else {
    fprintf(out, "  op_count=%" PRIu32 "\n", count);
  }
  for (i = 0; i < count; ++i) {
    uint8_t op_type;
    uint8_t n_in;
    uint8_t n_out;
    uint32_t node_id;
    uint32_t j;

    if (n < 8u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "SCHEDULE op header truncated");
    }
    op_type = p[0];
    n_in = p[1];
    n_out = p[2];
    node_id = rd_u32(p + 4);
    p += 8;
    n -= 8;

    if (n < (((size_t)n_in + (size_t)n_out) * 4u)) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "SCHEDULE io list truncated");
    }
    if (op_type != 1u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "unsupported op_type=%u", (unsigned)op_type);
    }
    if (is_canonical_mode(mode)) {
      fprintf(out, "%" PRIu32 ": CALL node=%" PRIu32 " in=[", i, node_id);
    } else {
      fprintf(out, "  %" PRIu32 ": CALL node=%" PRIu32 " in=[", i, node_id);
    }
    for (j = 0; j < n_in; ++j) {
      if (j != 0u) {
        fprintf(out, " ");
      }
      fprintf(out, "%" PRIu32, rd_u32(p + ((size_t)j * 4u)));
    }
    fprintf(out, "] out=[");
    for (j = 0; j < n_out; ++j) {
      if (j != 0u) {
        fprintf(out, " ");
      }
      fprintf(out, "%" PRIu32, rd_u32(p + ((size_t)(n_in + j) * 4u)));
    }
    fprintf(out, "]\n");
    p += ((size_t)n_in + (size_t)n_out) * 4u;
    n -= ((size_t)n_in + (size_t)n_out) * 4u;
  }

  if (n != 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT, "SCHEDULE trailing bytes");
  }

  return GRPH_BLOB_OK;
}

static int disasm_param_defaults(FILE *out, const grph_blob_section *s,
                                 grph_blob_text_mode mode, char *err,
                                 size_t err_cap) {
  const uint8_t *p = s->payload;
  size_t n = s->payload_bytes;
  uint32_t count;
  uint32_t i;
  if (n < 4u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                "PARAM_DEFAULTS truncated");
  }
  count = rd_u32(p);
  p += 4;
  n -= 4;

  fprintf(out, "[PARAM_DEFAULTS]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "count=%" PRIu32 "\n", count);
  }
  for (i = 0; i < count; ++i) {
    uint32_t node_id;
    uint32_t bytes;
    float gain_db;
    if (n < 8u) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "PARAM_DEFAULTS entry truncated");
    }
    node_id = rd_u32(p + 0);
    bytes = rd_u32(p + 4);
    p += 8;
    n -= 8;
    if (n < bytes) {
      return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                  "PARAM_DEFAULTS payload truncated");
    }
    if (bytes == 4u) {
      gain_db = rd_f32(p);
      if (is_canonical_mode(mode)) {
        fprintf(out, "node=%" PRIu32 " bytes=%" PRIu32 " data=f32(%.1f)\n",
                node_id, bytes, gain_db);
      } else {
        fprintf(out, "  node=%" PRIu32 " bytes=%" PRIu32 "  data=f32(%.1f)\n",
                node_id, bytes, gain_db);
      }
    } else {
      if (is_canonical_mode(mode)) {
        fprintf(out, "node=%" PRIu32 " bytes=%" PRIu32 " data=", node_id,
                bytes);
      } else {
        fprintf(out, "  node=%" PRIu32 " bytes=%" PRIu32 "  data=", node_id,
                bytes);
      }
      print_hex_bytes(out, p, bytes);
      fprintf(out, "\n");
    }
    p += bytes;
    n -= bytes;
  }

  if (n != 0u) {
    return fail(err, err_cap, GRPH_BLOB_ERR_FORMAT,
                "PARAM_DEFAULTS trailing bytes");
  }
  return GRPH_BLOB_OK;
}

int grph_blob_dump(FILE *out, const uint8_t *data, size_t data_bytes,
                   grph_blob_text_mode mode, char *err, size_t err_cap) {
  grph_blob_view blob;
  char abi[5];
  int rc;
  if (!out) {
    return fail(err, err_cap, GRPH_BLOB_ERR_ARG, "null output stream");
  }

  rc = grph_blob_parse(data, data_bytes, &blob, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }

  abi_to_str(blob.target_abi, abi);
  if (is_canonical_mode(mode)) {
    fprintf(out,
            "GRPH v%u.%u abi=%s uuid=%08" PRIx32 "-%04" PRIx32 "-%04" PRIx32
            "-%04" PRIx32 "-%012" PRIx64 "\n",
            (unsigned)blob.version_major, (unsigned)blob.version_minor, abi,
            (uint32_t)(blob.graph_uuid_hi >> 32),
            (uint32_t)((blob.graph_uuid_hi >> 16) & 0xFFFFu),
            (uint32_t)(blob.graph_uuid_hi & 0xFFFFu),
            (uint32_t)((blob.graph_uuid_lo >> 48) & 0xFFFFu),
            (uint64_t)(blob.graph_uuid_lo & 0xFFFFFFFFFFFFULL));
  } else {
    fprintf(out,
            "GRPH v%u.%u  abi=%s  uuid=%08" PRIx32 "-%04" PRIx32 "-%04" PRIx32
            "-%04" PRIx32 "-%012" PRIx64 "\n",
            (unsigned)blob.version_major, (unsigned)blob.version_minor, abi,
            (uint32_t)(blob.graph_uuid_hi >> 32),
            (uint32_t)((blob.graph_uuid_hi >> 16) & 0xFFFFu),
            (uint32_t)(blob.graph_uuid_hi & 0xFFFFu),
            (uint32_t)((blob.graph_uuid_lo >> 48) & 0xFFFFu),
            (uint64_t)(blob.graph_uuid_lo & 0xFFFFFFFFFFFFULL));
  }

  fprintf(out,
          "Sections: REQUIRES GRAPH_CONFIG HEAPS BUFFERS NODES SCHEDULE "
          "PARAM_DEFAULTS");
  if (blob.metadata_min && !is_canonical_mode(mode)) {
    fprintf(out, " METADATA_MIN");
  }
  fprintf(out, "\n\n");

  rc = disasm_requires(out, blob.requires, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  rc = disasm_graph_config(out, &blob.graph_config_values, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  rc = disasm_heaps(out, blob.heaps, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  rc = disasm_buffers(out, blob.buffers, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  rc = disasm_nodes(out, blob.nodes, blob.param_defaults, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  rc = disasm_schedule(out, blob.schedule, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  rc = disasm_param_defaults(out, blob.param_defaults, mode, err, err_cap);
  if (rc != GRPH_BLOB_OK) {
    return rc;
  }
  fprintf(out, "\n");

  fprintf(out, "[CRC32]\n");
  if (is_canonical_mode(mode)) {
    fprintf(out, "status=ok\n");
  } else {
    fprintf(out, "  ok\n");
  }

  return GRPH_BLOB_OK;
}

int grph_blob_disassemble(FILE *out, const uint8_t *data, size_t data_bytes,
                          char *err, size_t err_cap) {
  return grph_blob_dump(out, data, data_bytes, GRPH_BLOB_TEXT_HUMAN, err,
                        err_cap);
}

int grph_blob_disassemble_canonical(FILE *out, const uint8_t *data,
                                    size_t data_bytes, char *err,
                                    size_t err_cap) {
  return grph_blob_dump(out, data, data_bytes, GRPH_BLOB_TEXT_CANONICAL, err,
                        err_cap);
}
