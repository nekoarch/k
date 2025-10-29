#ifndef AST_H_
#define AST_H_
#include "def.h"
#include "token.h"

typedef enum {
  AST_LITERAL,
  AST_UNARY,
  AST_BINARY,
  AST_CALL,
  AST_SEQ,
  AST_LIST,
  AST_CONDITIONAL,
  AST_ADVERB,
  AST_VAR,
} ASTNodeType;

typedef struct ASTNode {
  ASTNodeType type;
  union {
    struct {
      KObj *value;
    } literal;
    struct {
      Token op;
      struct ASTNode *child;
    } unary;
    struct {
      Token op;
      struct ASTNode *left;
      struct ASTNode *right;
    } binary;
    struct {
      struct ASTNode *callee;
      struct ASTNode **args;
      size_t arg_count;
    } call;
    struct {
      struct ASTNode **items;
      size_t count;
    } seq;
    struct {
      struct ASTNode *condition;
      struct ASTNode *then_branch;
      struct ASTNode *else_branch;
    } conditional;
    struct {
      Token op;
      struct ASTNode *child;
    } adverb;
    struct {
      const char *name;
    } var;
  } as;
} ASTNode;

ASTNode *create_literal_node(KObj *value);
ASTNode *create_unary_node(Token op, ASTNode *child);
ASTNode *create_binary_node(Token op, ASTNode *left, ASTNode *right);
ASTNode *create_call_node(ASTNode *callee, ASTNode **args, size_t arg_count);
ASTNode *create_seq_node(ASTNode **items, size_t count);
ASTNode *create_list_node(ASTNode **items, size_t count);
ASTNode *create_conditional_node(ASTNode *condition, ASTNode *then_branch,
                                 ASTNode *else_branch);
ASTNode *create_adverb_node(Token op, ASTNode *child);
ASTNode *create_var_node(const char *name);
void free_ast(ASTNode *node);

#endif
