#include "arena.h"
#include <stdlib.h>

Arena global_arena;

void arena_init(Arena *arena) {
  arena->head = NULL;
}

void *arena_alloc(Arena *arena, size_t size) {
  ArenaBlock *block = (ArenaBlock *)malloc(sizeof(ArenaBlock) + size);
  if (!block) return NULL;
  block->next = arena->head;
  block->size = size;
  arena->head = block;
  return block->data;
}

void arena_dispose(Arena *arena) {
  ArenaBlock *block = arena->head;
  while (block) {
    ArenaBlock *next = block->next;
    free(block);
    block = next;
  }
  arena->head = NULL;
}
