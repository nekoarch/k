#include "ast.h"
#include "arena.h"
#include "def.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void print_kobj(KObj *obj) {
  if (!obj)
    return;
  switch (obj->type) {
  case NIL:
    printf("nil");
    break;
  case INT:
    printf("%lld", (long long)obj->as.int_value);
    break;
  case FLOAT:
    printf("%f", obj->as.float_value);
    break;
  case CHAR:
    printf("%c", obj->as.char_value);
    break;
  case PINF:
    printf("+0w");
    break;
  case NINF:
    printf("-0w");
    break;
  case VECTOR: {
    bool is_string = true;
    for (size_t i = 0; i < obj->as.vector->length; i++) {
      if (obj->as.vector->items[i].type != CHAR) {
        is_string = false;
        break;
      }
    }
    if (is_string) {
      for (size_t i = 0; i < obj->as.vector->length; i++) {
        printf("%c", obj->as.vector->items[i].as.char_value);
      }
    } else {
      printf("(");
      for (size_t i = 0; i < obj->as.vector->length; i++) {
        print_kobj(&obj->as.vector->items[i]);
        if (i + 1 < obj->as.vector->length)
          printf(" ");
      }
      printf(")");
    }
    break;
  }
  default:
    printf("^type\n");
    break;
  }
}

ASTNode *create_literal_node(KObj *value) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_LITERAL;
  retain_object(value);
  node->as.literal.value = value;
  return node;
}

ASTNode *create_unary_node(Token op, ASTNode *child) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_UNARY;
  node->as.unary.op = op;
  node->as.unary.child = child;
  return node;
}

ASTNode *create_binary_node(Token op, ASTNode *left, ASTNode *right) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_BINARY;
  node->as.binary.op = op;
  node->as.binary.left = left;
  node->as.binary.right = right;
  return node;
}

ASTNode *create_call_node(ASTNode *callee, ASTNode **args, size_t arg_count) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_CALL;
  node->as.call.callee = callee;
  node->as.call.args = args;
  node->as.call.arg_count = arg_count;
  return node;
}

ASTNode *create_seq_node(ASTNode **items, size_t count) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_SEQ;
  node->as.seq.items = items;
  node->as.seq.count = count;
  return node;
}

ASTNode *create_list_node(ASTNode **items, size_t count) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_LIST;
  node->as.seq.items = items;
  node->as.seq.count = count;
  return node;
}

ASTNode *create_conditional_node(ASTNode *condition, ASTNode *then_branch,
                                 ASTNode *else_branch) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_CONDITIONAL;
  node->as.conditional.condition = condition;
  node->as.conditional.then_branch = then_branch;
  node->as.conditional.else_branch = else_branch;
  return node;
}

ASTNode *create_adverb_node(Token op, ASTNode *child) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_ADVERB;
  node->as.adverb.op = op;
  node->as.adverb.child = child;
  return node;
}

ASTNode *create_var_node(const char *name) {
  ASTNode *node = (ASTNode *)arena_alloc(&global_arena, sizeof(ASTNode));
  node->type = AST_VAR;
  node->as.var.name = name;
  return node;
}

void free_ast(ASTNode *node) {
  if (node == NULL) {
    return;
  }
  switch (node->type) {
  case AST_LITERAL:
    release_object(node->as.literal.value);
    break;
  case AST_UNARY:
    free_ast(node->as.unary.child);
    break;
  case AST_BINARY:
    free_ast(node->as.binary.left);
    free_ast(node->as.binary.right);
    break;
  case AST_CALL:
    free_ast(node->as.call.callee);
    for (size_t i = 0; i < node->as.call.arg_count; i++) {
      free_ast(node->as.call.args[i]);
    }
    break;
  case AST_SEQ:
  case AST_LIST:
    for (size_t i = 0; i < node->as.seq.count; i++) {
      free_ast(node->as.seq.items[i]);
    }
    break;
  case AST_CONDITIONAL:
    free_ast(node->as.conditional.condition);
    free_ast(node->as.conditional.then_branch);
    free_ast(node->as.conditional.else_branch);
    break;
  case AST_ADVERB:
    free_ast(node->as.adverb.child);
    break;
  case AST_VAR:
    break;
  }
}
