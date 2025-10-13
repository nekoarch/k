#include "arena.h"
#include "repl.h"

int main(int argc, char **argv) {
  arena_init(&global_arena);
  int status = 0;
  if (argc > 1) {
    status = run_file(argv[1]);
  } else {
    run_repl();
  }
  arena_dispose(&global_arena);
  return status;
}
