#ifndef EVAL_H_
#define EVAL_H_

#include "ast.h"
#include "def.h"

KObj *evaluate(ASTNode *node);
void env_dump();
KObj* call_unary(KObj* fn, KObj* arg);
KObj* call_binary(KObj* fn, KObj* left, KObj* right);

#endif
