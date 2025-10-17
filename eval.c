#include "eval.h"
#include "def.h"
#include "builtins.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include "arena.h"
#include "ast.h"
static int scan_node(ASTNode *n);
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
  case AST_VAR: {
    return env_get(node->as.var.name);
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
      if (node->as.binary.left->type == AST_VAR) {
        const char *name = node->as.binary.left->as.var.name;
        KObj *right_val = evaluate(node->as.binary.right);
        if (right_val->type == NIL) {
          return right_val;
        }
        env_set(name, right_val);
        return right_val;
      }
      if (node->as.binary.left->type == AST_CALL) {
        ASTNode *call = node->as.binary.left;
        if (call->as.call.callee->type == AST_VAR &&
            call->as.call.arg_count == 1) {
          const char *name = call->as.call.callee->as.var.name;
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
          if (idx_obj->type == INT) {
            size_t vec_len = vec->as.vector->length;
            int64_t id = idx_obj->as.int_value;
            if (id < 0 || (size_t)id >= vec_len) {
              printf("^length\n");
              release_object(vec);
              release_object(idx_obj);
              release_object(right_val);
              return create_nil();
            }
            vector_set(vec, (size_t)id, right_val);
            release_object(vec);
            release_object(idx_obj);
            return right_val;
          } else if (idx_obj->type == VECTOR) {
            size_t idx_count = idx_obj->as.vector->length;
            int64_t *idxs = (int64_t *)malloc(sizeof(int64_t) * idx_count);
            for (size_t i = 0; i < idx_count; i++) {
              KObj *it = &idx_obj->as.vector->items[i];
              if (it->type != INT) {
                printf("^type\n");
                free(idxs);
                release_object(vec);
                release_object(idx_obj);
                release_object(right_val);
                return create_nil();
              }
              idxs[i] = it->as.int_value;
            }
            bool val_is_vec = right_val->type == VECTOR;
            size_t val_count = val_is_vec ? right_val->as.vector->length : 1;
            if (val_count != idx_count) {
              printf("^length\n");
              free(idxs);
              release_object(vec);
              release_object(idx_obj);
              release_object(right_val);
              return create_nil();
            }
            size_t vec_len = vec->as.vector->length;
            for (size_t i = 0; i < idx_count; i++) {
              if (idxs[i] < 0 || (size_t)idxs[i] >= vec_len) {
                printf("^length\n");
                free(idxs);
                release_object(vec);
                release_object(idx_obj);
                release_object(right_val);
                return create_nil();
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
          } else {
            printf("^type\n");
            release_object(vec);
            release_object(idx_obj);
            release_object(right_val);
            return create_nil();
          }
        }
        if (call->as.call.callee->type == AST_VAR &&
            call->as.call.arg_count >= 2) {
          const char *name = call->as.call.callee->as.var.name;
          KObj *vec = env_get(name);
          if (vec->type != VECTOR) {
            if (vec->type != NIL) printf("^type\n");
            release_object(vec);
            return create_nil();
          }
          size_t argc = call->as.call.arg_count;
          KObj **idxobjs = (KObj **)malloc(sizeof(KObj*) * argc);
          for (size_t i = 0; i < argc; i++) {
            idxobjs[i] = evaluate(call->as.call.args[i]);
            if (idxobjs[i]->type == NIL) {
              for (size_t j = 0; j <= i; j++) release_object(idxobjs[j]);
              free(idxobjs);
              release_object(vec);
              return create_nil();
            }
            if (idxobjs[i]->type != INT) {
              printf("^type\n");
              for (size_t j = 0; j <= i; j++) release_object(idxobjs[j]);
              free(idxobjs);
              release_object(vec);
              return create_nil();
            }
          }
          KObj *container = vec;
          for (size_t i = 0; i + 1 < argc; i++) {
            if (container->type != VECTOR) {
              printf("^type\n");
              for (size_t j = 0; j < argc; j++) release_object(idxobjs[j]);
              free(idxobjs);
              release_object(vec);
              return create_nil();
            }
            int64_t id = idxobjs[i]->as.int_value;
            size_t len = container->as.vector->length;
            if (id < 0 || (size_t)id >= len) {
              printf("^length\n");
              for (size_t j = 0; j < argc; j++) release_object(idxobjs[j]);
              free(idxobjs);
              release_object(vec);
              return create_nil();
            }
            KObj *child = &container->as.vector->items[id];
            container = child;
          }
          if (container->type != VECTOR) {
            printf("^type\n");
            for (size_t j = 0; j < argc; j++) release_object(idxobjs[j]);
            free(idxobjs);
            release_object(vec);
            return create_nil();
          }
          int64_t last = idxobjs[argc - 1]->as.int_value;
          size_t clen = container->as.vector->length;
          if (last < 0 || (size_t)last >= clen) {
            printf("^length\n");
            for (size_t j = 0; j < argc; j++) release_object(idxobjs[j]);
            free(idxobjs);
            release_object(vec);
            return create_nil();
          }
          KObj *right_val = evaluate(node->as.binary.right);
          if (right_val->type == NIL) {
            for (size_t j = 0; j < argc; j++) release_object(idxobjs[j]);
            free(idxobjs);
            release_object(vec);
            return right_val;
          }
          vector_set(container, (size_t)last, right_val);
          for (size_t j = 0; j < argc; j++) release_object(idxobjs[j]);
          free(idxobjs);
          release_object(vec);
          return right_val;
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
    size_t assign_idx = (size_t)-1;
    if (fn->type == ADVERB && argn >= 2) {
      for (size_t i = 0; i < argn; i++) {
        ASTNode *a = node->as.call.args[i];
        if (a && a->type == AST_BINARY && a->as.binary.op.type == COLON &&
            a->as.binary.left && a->as.binary.left->type == AST_VAR) {
          assign_idx = i;
          break;
        }
      }
    }
    if (assign_idx != (size_t)-1) {
      args[assign_idx] = evaluate(node->as.call.args[assign_idx]);
      if (args[assign_idx]->type == NIL) {
        release_object(fn);
        free(args);
        return create_nil();
      }
      for (size_t i = 0; i < argn; i++) {
        if (i == assign_idx) continue;
        args[i] = evaluate(node->as.call.args[i]);
        if (args[i]->type == NIL) {
          for (size_t j = 0; j < argn; j++) {
            if (j == i) break;
            if (j == assign_idx) continue;
            release_object(args[j]);
          }
          release_object(args[assign_idx]);
          release_object(fn);
          free(args);
          return create_nil();
        }
      }
    } else {
      for (size_t i = 0; i < argn; i++) {
        args[i] = evaluate(node->as.call.args[i]);
        if (args[i]->type == NIL) {
          for (size_t j = 0; j <= i; j++) release_object(args[j]);
          release_object(fn);
          free(args);
          return create_nil();
        }
      }
    }
    if (fn->type == ADVERB) {
      KObj* child = fn->as.adverb->child;
      KObj* result = NULL;
      if (fn->as.adverb->op.type == SLASH) {
        if (child->type == VERB || child->type == LAMBDA || child->type == PROJ) {
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
        if (child->type == VERB || child->type == LAMBDA || child->type == PROJ) {
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
        if (child->type == VERB || child->type == LAMBDA || child->type == PROJ) {
          result = k_each_n(child, args, argn);
        } else {
          printf("^type (each)\n");
          result = create_nil();
        }
      } else if (fn->as.adverb->op.type == SLASH_COLON) {
        if (!(child->type == VERB || child->type == LAMBDA || child->type == PROJ)) {
          printf("^type (eachright)\n");
          result = create_nil();
        } else if (argn != 2) {
          printf("^rank (eachright)\n");
          result = create_nil();
        } else {
          KObj *left = args[0];
          KObj *right = args[1];
          if (right->type == VECTOR) {
            size_t len = right->as.vector->length;
            KObj *res = create_vec(len);
            for (size_t i = 0; i < len; i++) {
              KObj *elem = &right->as.vector->items[i];
              KObj *val = NULL;
            if (child->type == VERB && child->as.verb.binary) {
              val = child->as.verb.binary(left, elem);
            } else {
              KObj *call_args[2] = { left, elem };
              val = call_n(child, call_args, 2);
            }
              if (val->type == NIL) { release_object(res); result = val; goto adv_done; }
              vector_append(res, val);
              release_object(val);
            }
            result = res;
          } else {
            if (child->type == VERB && child->as.verb.binary) {
              result = child->as.verb.binary(left, right);
            } else {
              KObj *call_args[2] = { left, right };
              result = call_n(child, call_args, 2);
            }
          }
        }
      } else if (fn->as.adverb->op.type == BACKSLASH_COLON) {
        if (!(child->type == VERB || child->type == LAMBDA || child->type == PROJ)) {
          printf("^type (eachleft)\n");
          result = create_nil();
        } else if (argn != 2) {
          printf("^rank (eachleft)\n");
          result = create_nil();
        } else {
          KObj *left = args[0];
          KObj *right = args[1];
          if (left->type == VECTOR) {
            size_t len = left->as.vector->length;
            KObj *res = create_vec(len);
            for (size_t i = 0; i < len; i++) {
              KObj *elem = &left->as.vector->items[i];
              KObj *val = NULL;
            if (child->type == VERB && child->as.verb.binary) {
              val = child->as.verb.binary(elem, right);
            } else {
              KObj *call_args[2] = { elem, right };
              val = call_n(child, call_args, 2);
            }
              if (val->type == NIL) { release_object(res); result = val; goto adv_done; }
              vector_append(res, val);
              release_object(val);
            }
            result = res;
          } else {
            if (child->type == VERB && child->as.verb.binary) {
              result = child->as.verb.binary(left, right);
            } else {
              KObj *call_args[2] = { left, right };
              result = call_n(child, call_args, 2);
            }
          }
        }
      } else {
        printf("^nyi\n");
        result = create_nil();
      }
adv_done:
      for (size_t i = 0; i < argn; i++) release_object(args[i]);
      free(args);
      release_object(fn);
      return result;
    }
    if (fn->type == LAMBDA || fn->type == VERB || fn->type == PROJ) {
      KObj *result_obj = call_n(fn, args, argn);
      for (size_t i = 0; i < argn; i++) release_object(args[i]);
      free(args);
      release_object(fn);
      return result_obj;
    }
    KObj *current = fn;
    for (size_t ai = 0; ai < argn; ai++) {
      KObj *idx = args[ai];
      KObj *next;
      if (current->type != VECTOR) {
        printf("^type\n");
        next = create_nil();
      } else if (idx->type == INT || idx->type == FLOAT) {
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
        if (i < current->as.vector->length) {
          next = &current->as.vector->items[i];
          retain_object(next);
        } else {
          next = create_int(0);
        }
      } else if (idx->type == VECTOR) {
        size_t idx_len = idx->as.vector->length;
        KObj *res = create_vec(idx_len);
        size_t vec_len = current->as.vector->length;
        bool ok = true;
        for (size_t j = 0; j < idx_len; j++) {
          KObj *it = &idx->as.vector->items[j];
          int64_t id;
          if (it->type == INT) {
            id = it->as.int_value;
          } else if (it->type == FLOAT) {
            id = (int64_t)it->as.float_value;
          } else {
            printf("^type\n");
            ok = false;
            break;
          }
          if (id < 0 || (size_t)id >= vec_len) {
            printf("^length\n");
            ok = false;
            break;
          }
          vector_append(res, &current->as.vector->items[id]);
        }
        if (!ok) {
          release_object(res);
          next = create_nil();
        } else {
          next = res;
        }
      } else {
        printf("^type\n");
        next = create_nil();
      }
      release_object(idx);
      release_object(current);
      current = next;
      if (current->type == NIL) {
        for (size_t j = ai + 1; j < argn; j++) release_object(args[j]);
        free(args);
        return current;
      }
    }
    free(args);
    return current;
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
  if (fn->type == PROJ) {
    KObj *args[1] = { arg };
    return call_n(fn, args, 1);
  }
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
  if (fn->type == PROJ) {
    KObj *args[2] = { left, right };
    return call_n(fn, args, 2);
  }
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
  if (fn->type == PROJ) {
    KProj *p = fn->as.proj;
    size_t total = p->argn + argn;
    if (total < p->arity) {
      KObj **combined = NULL;
      if (total > 0) {
        combined = (KObj **)malloc(sizeof(KObj *) * total);
        for (size_t i = 0; i < p->argn; i++) combined[i] = p->args[i];
        for (size_t i = 0; i < argn; i++) combined[p->argn + i] = args[i];
      }
      KObj *np = create_projection(p->fn, combined, total, p->arity);
      if (combined) free(combined);
      return np;
    }
    size_t n = total;
    KObj **combined = (KObj **)malloc(sizeof(KObj *) * n);
    for (size_t i = 0; i < p->argn; i++) combined[i] = p->args[i];
    for (size_t i = 0; i < argn; i++) combined[p->argn + i] = args[i];
    KObj *res = call_n(p->fn, combined, n);
    free(combined);
    return res;
  }
  if (fn->type == LAMBDA) {
    int arity = fn->as.lambda->param_count;
    if (arity <= 0) {
      KLambda *lam_scan = fn->as.lambda;
      int max_idx = 0; // - x/y/z
      for (size_t i = 0; i < lam_scan->body_count; i++) {
        int t = scan_node(lam_scan->body[i]);
        if (t > max_idx) max_idx = t;
      }
      arity = max_idx; // 0..3
      if (arity == 0) arity = 0;
    }
    if ((int)argn < arity) {
      return create_projection(fn, args, argn, (size_t)arity);
    }
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

static int scan_node(ASTNode *n) {
  if (!n) return 0;
  switch (n->type) {
    case AST_VAR: {
      const char *nm = n->as.var.name;
      if (!nm) return 0;
      if (strcmp(nm, "x") == 0) return 1;
      if (strcmp(nm, "y") == 0) return 2;
      if (strcmp(nm, "z") == 0) return 3;
      return 0;
    }
    case AST_LITERAL:
      return 0;
    case AST_UNARY:
      return scan_node(n->as.unary.child);
    case AST_BINARY: {
      int a = scan_node(n->as.binary.left);
      int b = scan_node(n->as.binary.right);
      return a > b ? a : b;
    }
    case AST_CALL: {
      int m = scan_node(n->as.call.callee);
      for (size_t i = 0; i < n->as.call.arg_count; i++) {
        int t = scan_node(n->as.call.args[i]);
        if (t > m) m = t;
      }
      return m;
    }
    case AST_SEQ:
    case AST_LIST: {
      int m = 0;
      for (size_t i = 0; i < n->as.seq.count; i++) {
        int t = scan_node(n->as.seq.items[i]);
        if (t > m) m = t;
      }
      return m;
    }
    case AST_CONDITIONAL: {
      int a = scan_node(n->as.conditional.condition);
      int b = scan_node(n->as.conditional.then_branch);
      int c = scan_node(n->as.conditional.else_branch);
      int m = a > b ? a : b; if (c > m) m = c; return m;
    }
    case AST_ADVERB:
      return scan_node(n->as.adverb.child);
  }
  return 0;
}
