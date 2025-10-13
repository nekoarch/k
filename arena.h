#ifndef ARENA_H_
#define ARENA_H_

#include <stddef.h>

typedef struct ArenaBlock {
  struct ArenaBlock *next;
  size_t size;
  unsigned char data[];
} ArenaBlock;

typedef struct Arena {
  ArenaBlock *head;
} Arena;

void arena_init(Arena *arena);
void *arena_alloc(Arena *arena, size_t size);
void arena_dispose(Arena *arena);

extern Arena global_arena;

#endif
