#ifndef OPS_H_
#define OPS_H_

#include "def.h"
#include "token.h"

typedef struct {
  UnaryFunc unary;
  BinaryFunc binary;
} OpDesc;

const OpDesc *get_op_desc(TokenType t);

#endif

