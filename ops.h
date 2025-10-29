#ifndef OPS_H_
#define OPS_H_

#include "def.h"
#include "token.h"

typedef struct {
  UnaryFunc unary;
  BinaryFunc binary;
} OpDesc;

typedef enum { ASSOC_LEFT, ASSOC_RIGHT, ASSOC_NONE } Assoc;

typedef struct {
  TokenType type;
  const char *text;
  const char *print_text;
  int precedence; // reserved for Pratt parser
  Assoc assoc;
  int is_operator;
} OpInfo;

const OpDesc *get_op_desc(TokenType t);
const OpInfo *get_op_info(TokenType t);
const char *op_text(TokenType t);
const OpInfo *find_op_by_char(char c);
const OpInfo *find_op_by_ident(const char *s, int len);

#endif
