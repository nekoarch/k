#include "eval.h"
#include "def.h"
#include "builtins.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "arena.h"
#include "repl.h"
#include "ops.h"

static char *k_strdup_local(const char *s) {
  size_t len = strlen(s);
  char *res = (char *)arena_alloc(&global_arena, len + 1);
  if (!res) return NULL;
  memcpy(res, s, len + 1);
  return res;
}

typedef struct {
  char *name;
  KObj *value;
} VarEntry;

typedef struct {
  VarEntry entries[256];
  size_t count;
} EnvFrame;

static EnvFrame env_stack[256];
static size_t env_top = 0;

static void env_push() { env_top++; env_stack[env_top].count = 0; }

static void env_pop() {
  EnvFrame *frame = &env_stack[env_top];
  for (size_t i = 0; i < frame->count; i++) {
    release_object(frame->entries[i].value);
  }
  env_top--;
}

static KObj *env_get(const char *name) {
  for (size_t f = env_top + 1; f-- > 0;) {
    EnvFrame *frame = &env_stack[f];
    for (size_t i = 0; i < frame->count; i++) {
      if (strcmp(frame->entries[i].name, name) == 0) {
        retain_object(frame->entries[i].value);
        return frame->entries[i].value;
      }
    }
  }
  printf("^var\n");
  return create_nil();
}

static void env_set(const char *name, KObj *value) {
  EnvFrame *frame = &env_stack[env_top];
  for (size_t i = 0; i < frame->count; i++) {
    if (strcmp(frame->entries[i].name, name) == 0) {
      release_object(frame->entries[i].value);
      frame->entries[i].value = value;
      retain_object(value);
      return;
    }
  }
  frame->entries[frame->count].name = k_strdup_local(name);
  frame->entries[frame->count].value = value;
  retain_object(value);
  frame->count++;
}

void env_dump() {
  for (size_t f = 0; f <= env_top; f++) {
    EnvFrame *frame = &env_stack[f];
    for (size_t i = 0; i < frame->count; i++) {
      printf("%s: ", frame->entries[i].name);
      print(frame->entries[i].value);
    }
  }
}

static KObj *eval_literal(KObj *obj) {
  if (!obj) return create_nil();
  switch (obj->type) {
  case SYM:
    return env_get(obj->as.symbol_value);
  case VECTOR: {
    KObj *res = create_vec(obj->as.vector->length);
    for (size_t i = 0; i < obj->as.vector->length; i++) {
      KObj *item = eval_literal(&obj->as.vector->items[i]);
      if (item->type == NIL) {
        release_object(item);
        release_object(res);
        return create_nil();
      }
      vector_append(res, item);
      release_object(item);
    }
    return res;
  }
  default:
    retain_object(obj);
    return obj;
  }
}

KObj* evaluate(ASTNode *node) {
  if (node == NULL) {
    return create_nil();
  }

  switch (node->type) {
  case AST_LITERAL: {
    return eval_literal(node->as.literal.value);
  }
  case AST_UNARY: {
    KObj *val = evaluate(node->as.unary.child);
    if (val->type == NIL) {
      return val;
    }
    KObj *result_obj = NULL;
    const OpDesc *d = get_op_desc(node->as.unary.op.type);
    if (d && d->unary) {
      result_obj = d->unary(val);
    } else {
      printf("^nyi\n");
      result_obj = create_nil();
    }
    release_object(val);
    return result_obj;
  }

  case AST_BINARY: {
    if (node->as.binary.op.type == COLON) {
      if (node->as.binary.left->type == AST_LITERAL &&
          node->as.binary.left->as.literal.value->type == SYM) {
        const char *name = node->as.binary.left->as.literal.value->as.symbol_value;
        KObj *right_val = evaluate(node->as.binary.right);
        if (right_val->type == NIL) {
          return right_val;
        }
        env_set(name, right_val);
        return right_val;
      }
      if (node->as.binary.left->type == AST_CALL) {
        ASTNode *call = node->as.binary.left;
        if (call->as.call.callee->type == AST_LITERAL &&
            call->as.call.callee->as.literal.value->type == SYM &&
            call->as.call.arg_count == 1) {
          const char *name = call->as.call.callee->as.literal.value->as.symbol_value;
          KObj *vec = env_get(name);
          if (vec->type != VECTOR) {
            if (vec->type != NIL) printf("^type\n");
            release_object(vec);
            return create_nil();
          }
          KObj *idx_obj = evaluate(call->as.call.args[0]);
          if (idx_obj->type == NIL) {
            release_object(vec);
            return idx_obj;
          }
          KObj *right_val = evaluate(node->as.binary.right);
          if (right_val->type == NIL) {
            release_object(vec);
            release_object(idx_obj);
            return right_val;
          }
          int64_t *idxs = NULL;
          size_t idx_count = 0;
          if (idx_obj->type == INT) {
            idx_count = 1;
            idxs = (int64_t *)malloc(sizeof(int64_t));
            idxs[0] = idx_obj->as.int_value;
          } else if (idx_obj->type == VECTOR) {
            idx_count = idx_obj->as.vector->length;
            idxs = (int64_t *)malloc(sizeof(int64_t) * idx_count);
            for (size_t i = 0; i < idx_count; i++) {
              KObj *it = &idx_obj->as.vector->items[i];
              if (it->type != INT) {
                printf("^type\n");
                goto assign_cleanup;
              }
              idxs[i] = it->as.int_value;
            }
          } else {
            printf("^type\n");
            goto assign_cleanup;
          }
          bool val_is_vec = right_val->type == VECTOR;
          size_t val_count = val_is_vec ? right_val->as.vector->length : 1;
          if (val_count != idx_count) {
            printf("^length\n");
            goto assign_cleanup;
          }
          size_t vec_len = vec->as.vector->length;
          for (size_t i = 0; i < idx_count; i++) {
            if (idxs[i] < 0 || (size_t)idxs[i] >= vec_len) {
              printf("^length\n");
              goto assign_cleanup;
            }
          }
          for (size_t i = 0; i < idx_count; i++) {
            size_t pos = (size_t)idxs[i];
            KObj *new_val = val_is_vec ? &right_val->as.vector->items[i] : right_val;
            vector_set(vec, pos, new_val);
          }
          free(idxs);
          release_object(vec);
          release_object(idx_obj);
          return right_val;
assign_cleanup:
          if (idxs) free(idxs);
          release_object(vec);
          release_object(idx_obj);
          release_object(right_val);
          return create_nil();
        }
      }
      printf("^assign\n");
      return create_nil();
    }
    KObj *left_val = evaluate(node->as.binary.left);
    if (left_val->type == NIL) {
      return left_val;
    }
    KObj *right_val = evaluate(node->as.binary.right);
    if (right_val->type == NIL) {
      release_object(left_val);
      return right_val;
    }
    KObj *result_obj = NULL;
    const OpDesc *d2 = get_op_desc(node->as.binary.op.type);
    if (d2 && d2->binary) {
      result_obj = d2->binary(left_val, right_val);
    } else {
      printf("^nyi\n");
      result_obj = create_nil();
    }
    release_object(left_val);
    release_object(right_val);
    return result_obj;
  }
  case AST_CONDITIONAL: {
    KObj *condition_result = evaluate(node->as.conditional.condition);
    if (condition_result->type == NIL) {
      return condition_result;
    }
    bool is_true = false;
    if (condition_result->type == INT) {
      is_true = (condition_result->as.int_value != 0);
    } else if (condition_result->type == FLOAT) {
      is_true = (condition_result->as.float_value != 0.0);
    } else {
      is_true = true;
    }
    release_object(condition_result);
    if (is_true) {
      return evaluate(node->as.conditional.then_branch);
    } else {
      return evaluate(node->as.conditional.else_branch);
    }
  }
  case AST_ADVERB: {
    KObj* child_obj = evaluate(node->as.adverb.child);
    if (child_obj->type == NIL) {
      return child_obj;
    }
    KObj* adv = create_object(ADVERB);
    adv->as.adverb = (KAdverb*)arena_alloc(&global_arena, sizeof(KAdverb));
    adv->as.adverb->op = node->as.adverb.op;
    adv->as.adverb->child = child_obj;
    return adv;
  }
  case AST_CALL: {
    KObj *fn = evaluate(node->as.call.callee);
    if (fn->type == NIL) {
      return fn;
    }
    size_t argn = node->as.call.arg_count;
    KObj **args = (KObj **)malloc(sizeof(KObj *) * argn);
    for (size_t i = 0; i < argn; i++) {
      args[i] = evaluate(node->as.call.args[i]);
      if (args[i]->type == NIL) {
        for (size_t j = 0; j <= i; j++) release_object(args[j]);
        release_object(fn);
        free(args);
        return create_nil();
      }
    }
    if (fn->type == ADVERB) {
      KObj* child = fn->as.adverb->child;
      KObj* result = NULL;
      if (fn->as.adverb->op.type == SLASH) {
        if (child->type == VERB || child->type == LAMBDA) {
          if (argn == 1) {
            result = k_over(child, args[0], NULL);
          } else if (argn == 2) {
            result = k_over(child, args[1], args[0]);
          } else {
            printf("^rank (over)\n");
            result = create_nil();
          }
        } else if (child->type == INT) {
          if (argn != 1) {
            printf("^rank (decode)\n");
            result = create_nil();
          } else {
            result = k_decode(child, args[0]);
          }
        } else {
          if (argn != 1) {
            printf("^rank (join)\n");
            result = create_nil();
          } else {
            result = k_join(child, args[0]);
          }
        }
      } else if (fn->as.adverb->op.type == BACKSLASH) {
        if (child->type == VERB || child->type == LAMBDA) {
          if (argn == 1) {
            result = k_scan(child, args[0], NULL);
          } else if (argn == 2) {
            result = k_scan(child, args[1], args[0]);
          } else {
            printf("^rank (scan)\n");
            result = create_nil();
          }
        } else if (child->type == INT) {
          if (argn != 1) {
            printf("^rank (encode)\n");
            result = create_nil();
          } else {
            result = k_encode(child, args[0]);
          }
        } else {
          if (argn != 1) {
            printf("^rank (split)\n");
            result = create_nil();
          } else {
            result = k_split(child, args[0]);
          }
        }
      } else if (fn->as.adverb->op.type == TICK) {
        if (child->type == VERB || child->type == LAMBDA) {
          result = k_each_n(child, args, argn);
        } else {
          printf("^type (each)\n");
          result = create_nil();
        }
      } else {
        printf("^nyi\n");
        result = create_nil();
      }
      for (size_t i = 0; i < argn; i++) release_object(args[i]);
      free(args);
      release_object(fn);
      return result;
    }
    if (fn->type == LAMBDA) {
      env_push();
      KLambda *lam = fn->as.lambda;
      if (lam->param_count > 0) {
        size_t n = lam->param_count < (int)argn ? (size_t)lam->param_count : argn;
        for (size_t i = 0; i < n; i++) {
          env_set(lam->params[i], args[i]);
        }
      } else {
        const char *defaults[] = {"x", "y", "z"};
        size_t n = argn < 3 ? argn : 3;
        for (size_t i = 0; i < n; i++) {
          env_set(defaults[i], args[i]);
        }
      }
      KObj *result_obj = create_nil();
      for (size_t i = 0; i < lam->body_count; i++) {
        release_object(result_obj);
        result_obj = evaluate(lam->body[i]);
      }
      if (!lam->has_return) {
        release_object(result_obj);
        result_obj = create_nil();
      }
      env_pop();
      for (size_t i = 0; i < argn; i++) release_object(args[i]);
      free(args);
      release_object(fn);
      return result_obj;
    }
    if (fn->type == VERB) {
      KObj *result_obj = create_nil();
      if (argn == 1 && fn->as.verb.unary) {
        release_object(result_obj);
        result_obj = fn->as.verb.unary(args[0]);
      } else if (argn == 2 && fn->as.verb.binary) {
        release_object(result_obj);
        result_obj = fn->as.verb.binary(args[0], args[1]);
      } else {
        printf("^rank\n");
      }
      for (size_t i = 0; i < argn; i++) release_object(args[i]);
      free(args);
      release_object(fn);
      return result_obj;
    }
    if (argn != 1) {
      printf("^arity\n");
      for (size_t i = 0; i < argn; i++) release_object(args[i]);
      free(args);
      release_object(fn);
      return create_nil();
    }
    KObj *vec = fn;
    KObj *idx = args[0];
    KObj *result_obj = create_int(0);
    if (vec->type == VECTOR) {
      if (idx->type == INT || idx->type == FLOAT) {
        size_t i = 0;
        if (idx->type == INT) {
          if (idx->as.int_value >= 0)
            i = (size_t)idx->as.int_value;
          else
            i = (size_t)-1;
        } else {
          if (idx->as.float_value >= 0)
            i = (size_t)idx->as.float_value;
          else
            i = (size_t)-1;
        }
        if (i < vec->as.vector->length) {
          result_obj = &vec->as.vector->items[i];
          retain_object(result_obj);
        }
      } else if (idx->type == VECTOR) {
        size_t idx_len = idx->as.vector->length;
        KObj *res = create_vec(idx_len);
        size_t vec_len = vec->as.vector->length;
        for (size_t j = 0; j < idx_len; j++) {
          KObj *it = &idx->as.vector->items[j];
          int64_t id;
          if (it->type == INT) {
            id = it->as.int_value;
          } else if (it->type == FLOAT) {
            id = (int64_t)it->as.float_value;
          } else {
            printf("^type\n");
            release_object(res);
            result_obj = create_nil();
            goto cleanup;
          }
          if (id < 0 || (size_t)id >= vec_len) {
            printf("^length\n");
            release_object(res);
            result_obj = create_nil();
            goto cleanup;
          }
          vector_append(res, &vec->as.vector->items[id]);
        }
        result_obj = res;
      } else {
        printf("^type\n");
        result_obj = create_nil();
      }
    } else {
      printf("^type\n");
      result_obj = create_nil();
    }
cleanup:
    release_object(vec);
    release_object(idx);
    free(args);
    return result_obj;
  }
  case AST_SEQ: {
    KObj *result = create_nil();
    for (size_t i = 0; i < node->as.seq.count; i++) {
      KObj *val = evaluate(node->as.seq.items[i]);
      if (i > 0) release_object(result);
      result = val;
    }
    return result;
  }
  case AST_LIST: {
    KObj *vec = create_vec(node->as.seq.count);
    for (size_t i = 0; i < node->as.seq.count; i++) {
      KObj *val = evaluate(node->as.seq.items[i]);
      vector_append(vec, val);
      release_object(val);
    }
    return vec;
  }
  }
  return create_nil();
}

KObj* call_unary(KObj* fn, KObj* arg) {
  if (fn->type == LAMBDA) {
    env_push();
    KLambda* lam = fn->as.lambda;
    if (lam->param_count > 0) {
      env_set(lam->params[0], arg);
    } else {
      env_set("x", arg);
    }
    KObj* result = create_nil();
    for (size_t i = 0; i < lam->body_count; i++) {
      release_object(result);
      result = evaluate(lam->body[i]);
    }
    if (!lam->has_return) {
      release_object(result);
      result = create_nil();
    }
    env_pop();
    return result;
  }
  if (fn->type == VERB) {
    if (fn->as.verb.unary) {
      return fn->as.verb.unary(arg);
    }
    printf("^rank\n");
    return create_nil();
  }
  printf("^type\n");
  return create_nil();
}

KObj* call_binary(KObj* fn, KObj* left, KObj* right) {
  if (fn->type == LAMBDA) {
    env_push();
    KLambda* lam = fn->as.lambda;
    if (lam->param_count > 0) {
      env_set(lam->params[0], left);
      if (lam->param_count > 1) {
        env_set(lam->params[1], right);
      } else {
        env_set("y", right);
      }
    } else {
      env_set("x", left);
      env_set("y", right);
    }
    KObj* result = create_nil();
    for (size_t i = 0; i < lam->body_count; i++) {
      release_object(result);
      result = evaluate(lam->body[i]);
    }
    if (!lam->has_return) {
      release_object(result);
      result = create_nil();
    }
    env_pop();
    return result;
  }
  if (fn->type == VERB && fn->as.verb.binary) {
    return fn->as.verb.binary(left, right);
  }
  printf("^rank\n");
  return create_nil();
}

KObj* call_n(KObj* fn, KObj** args, size_t argn) {
  if (fn->type == LAMBDA) {
    env_push();
    KLambda* lam = fn->as.lambda;
    if (lam->param_count > 0) {
      size_t n = lam->param_count < (int)argn ? (size_t)lam->param_count : argn;
      for (size_t i = 0; i < n; i++) {
        env_set(lam->params[i], args[i]);
      }
    } else {
      const char *defaults[] = {"x", "y", "z"};
      size_t n = argn < 3 ? argn : 3;
      for (size_t i = 0; i < n; i++) {
        env_set(defaults[i], args[i]);
      }
    }
    KObj* result = create_nil();
    for (size_t i = 0; i < lam->body_count; i++) {
      release_object(result);
      result = evaluate(lam->body[i]);
    }
    if (!lam->has_return) {
      release_object(result);
      result = create_nil();
    }
    env_pop();
    return result;
  }
  if (fn->type == VERB) {
    if (argn == 1 && fn->as.verb.unary) return fn->as.verb.unary(args[0]);
    if (argn == 2 && fn->as.verb.binary) return fn->as.verb.binary(args[0], args[1]);
    printf("^rank\n");
    return create_nil();
  }
  printf("^type\n");
  return create_nil();
}
