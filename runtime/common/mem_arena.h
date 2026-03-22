#ifndef FREEDOM_RUNTIME_COMMON_MEM_ARENA_H
#define FREEDOM_RUNTIME_COMMON_MEM_ARENA_H

#include <stdint.h>

typedef struct MemArena {
  uint8_t *base;
  uint32_t size_bytes;
  uint32_t offset_bytes;
} MemArena;

int mem_arena_init(MemArena *arena, void *base, uint32_t size_bytes);
void mem_arena_reset(MemArena *arena);
void *mem_arena_alloc(MemArena *arena, uint32_t size_bytes, uint32_t align);

#endif
