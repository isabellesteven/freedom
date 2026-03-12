#ifndef FREEDOM_RUNTIME_LOADER_BLOB_CURSOR_H
#define FREEDOM_RUNTIME_LOADER_BLOB_CURSOR_H

#include <stdint.h>

typedef struct BlobCursor {
  const uint8_t *data;
  uint32_t size_bytes;
  uint32_t offset_bytes;
} BlobCursor;

/* BlobCursor is intended for section/record traversal where centralizing
   offset arithmetic and bounds checks improves reviewability. Keep direct
   fixed-field reads outside this helper when they are already simple. */
int cursor_init(BlobCursor *cur, const uint8_t *data, uint32_t size_bytes);
int cursor_set_offset(BlobCursor *cur, uint32_t offset_bytes);
int cursor_skip(BlobCursor *cur, uint32_t nbytes);
int cursor_read_u32(BlobCursor *cur, uint32_t *out_value);
int cursor_get_u32_at(const BlobCursor *cur, uint32_t offset_bytes,
                      uint32_t *out_value);
int cursor_slice(const BlobCursor *cur, uint32_t offset_bytes,
                 uint32_t size_bytes, BlobCursor *out_slice);

#endif
