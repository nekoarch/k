#ifndef LEX_H_
#define LEX_H_
#include "token.h"
#include <stdbool.h>

typedef struct {
  const char *source;
  const char *start;
  const char *current;
  bool had_whitespace;
} Lexer;

void init_lexer(Lexer *lexer, const char *source);
Token scan_token(Lexer *lexer);

#endif
