#include "runtime/common/mem_arena.h"

#include <stddef.h>
#include <stdint.h>

int mem_arena_init(MemArena *arena, void *base, uint32_t size_bytes) {
  if (!arena || !base) {
    return 0;
  }
  arena->base = (uint8_t *)base;
  arena->size_bytes = size_bytes;
  arena->offset_bytes = 0u;
  return 1;
}

void mem_arena_reset(MemArena *arena) {
  if (!arena) {
    return;
  }
  arena->offset_bytes = 0u;
}

void *mem_arena_alloc(MemArena *arena, uint32_t size_bytes, uint32_t align) {
  uintptr_t base_addr;
  uintptr_t current_addr;
  uintptr_t aligned_addr;
  uint32_t aligned_offset;

  if (!arena || !arena->base) {
    return NULL;
  }
  if (align <= 1u) {
    align = 1u;
  }

  base_addr = (uintptr_t)arena->base;
  current_addr = base_addr + arena->offset_bytes;
  aligned_addr = (current_addr + (uintptr_t)(align - 1u)) &
                 ~((uintptr_t)align - 1u);
  aligned_offset = (uint32_t)(aligned_addr - base_addr);

  if (aligned_offset > arena->size_bytes ||
      size_bytes > arena->size_bytes - aligned_offset) {
    return NULL;
  }

  arena->offset_bytes = aligned_offset + size_bytes;
  return arena->base + aligned_offset;
}
