/* Verifies low-level BlobCursor reads, slicing, skipping, and bounds handling.
   It protects the section-reader primitives used by blob parsing and runtime binding. */
#include "runtime/loader/blob_cursor.h"

#include <stdint.h>
#include <stdio.h>

static void wr_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFFu);
  p[1] = (uint8_t)((v >> 8) & 0xFFu);
  p[2] = (uint8_t)((v >> 16) & 0xFFu);
  p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static int parse_heap_record(const uint8_t *payload, uint32_t payload_bytes,
                             uint32_t index, uint32_t *out_heap_id,
                             uint32_t *out_heap_bytes, uint32_t *out_heap_align) {
  BlobCursor cur;
  BlobCursor record;
  uint32_t count;

  if (!payload || !out_heap_id || !out_heap_bytes || !out_heap_align) {
    return 0;
  }
  if (!cursor_init(&cur, payload, payload_bytes) ||
      !cursor_read_u32(&cur, &count) || index >= count ||
      !cursor_slice(&cur, index * 16u, 16u, &record)) {
    return 0;
  }

  /* HEAPS record: u32 heap_id, u32 reserved, u32 heap_bytes, u32 heap_align */
  return cursor_get_u32_at(&record, 0u, out_heap_id) &&
         cursor_get_u32_at(&record, 8u, out_heap_bytes) &&
         cursor_get_u32_at(&record, 12u, out_heap_align);
}

int main(void) {
  uint8_t heaps_payload[36];
  uint32_t heap_id;
  uint32_t heap_bytes;
  uint32_t heap_align;

  wr_u32(heaps_payload + 0, 2u);
  wr_u32(heaps_payload + 4, 1u);
  wr_u32(heaps_payload + 8, 0u);
  wr_u32(heaps_payload + 12, 768u);
  wr_u32(heaps_payload + 16, 16u);
  wr_u32(heaps_payload + 20, 2u);
  wr_u32(heaps_payload + 24, 0u);
  wr_u32(heaps_payload + 28, 256u);
  wr_u32(heaps_payload + 32, 16u);

  if (!parse_heap_record(heaps_payload, sizeof(heaps_payload), 1u, &heap_id,
                         &heap_bytes, &heap_align)) {
    fprintf(stderr, "valid HEAPS parse failed\n");
    return 1;
  }
  if (heap_id != 2u || heap_bytes != 256u || heap_align != 16u) {
    fprintf(stderr, "unexpected HEAPS values\n");
    return 1;
  }
  if (parse_heap_record(heaps_payload, sizeof(heaps_payload), 2u, &heap_id,
                        &heap_bytes, &heap_align)) {
    fprintf(stderr, "out-of-range HEAPS index should fail\n");
    return 1;
  }
  if (parse_heap_record(heaps_payload, 35u, 1u, &heap_id, &heap_bytes,
                        &heap_align)) {
    fprintf(stderr, "truncated HEAPS payload should fail\n");
    return 1;
  }

  return 0;
}
