#include "runtime/loader/blob_cursor.h"

static uint32_t rd_u32_le(const uint8_t *p) {
  return (uint32_t)(p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
                    ((uint32_t)p[3] << 24));
}

int cursor_init(BlobCursor *cur, const uint8_t *data, uint32_t size_bytes) {
  if (!cur || !data) {
    return 0;
  }
  cur->data = data;
  cur->size_bytes = size_bytes;
  cur->offset_bytes = 0u;
  return 1;
}

int cursor_set_offset(BlobCursor *cur, uint32_t offset_bytes) {
  if (!cur || offset_bytes > cur->size_bytes) {
    return 0;
  }
  cur->offset_bytes = offset_bytes;
  return 1;
}

int cursor_skip(BlobCursor *cur, uint32_t nbytes) {
  if (!cur || nbytes > (cur->size_bytes - cur->offset_bytes)) {
    return 0;
  }
  cur->offset_bytes += nbytes;
  return 1;
}

int cursor_read_u32(BlobCursor *cur, uint32_t *out_value) {
  if (!cur || !out_value || (cur->size_bytes - cur->offset_bytes) < 4u) {
    return 0;
  }
  *out_value = rd_u32_le(cur->data + cur->offset_bytes);
  cur->offset_bytes += 4u;
  return 1;
}

int cursor_get_u32_at(const BlobCursor *cur, uint32_t offset_bytes,
                      uint32_t *out_value) {
  if (!cur || !out_value || offset_bytes > cur->size_bytes ||
      (cur->size_bytes - offset_bytes) < 4u) {
    return 0;
  }
  *out_value = rd_u32_le(cur->data + offset_bytes);
  return 1;
}

int cursor_slice(const BlobCursor *cur, uint32_t offset_bytes,
                 uint32_t size_bytes, BlobCursor *out_slice) {
  uint32_t base_offset;
  if (!cur || !out_slice) {
    return 0;
  }
  if (offset_bytes > (cur->size_bytes - cur->offset_bytes)) {
    return 0;
  }
  base_offset = cur->offset_bytes + offset_bytes;
  if (base_offset > cur->size_bytes ||
      size_bytes > (cur->size_bytes - base_offset)) {
    return 0;
  }
  out_slice->data = cur->data + base_offset;
  out_slice->size_bytes = size_bytes;
  out_slice->offset_bytes = 0u;
  return 1;
}
