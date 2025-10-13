#include "def.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "ast.h"
#include "arena.h"

static char *k_strdup_local(const char *s) {
  size_t len = strlen(s);
  char *res = (char *)arena_alloc(&global_arena, len + 1);
  if (!res) return NULL;
  memcpy(res, s, len + 1);
  return res;
}

KObj *create_object(KType type) {
  KObj *obj = (KObj *)arena_alloc(&global_arena, sizeof(KObj));
  if (!obj) {
    fprintf(stderr, "^oom\n");
    exit(1);
  }
  obj->type = type;
  obj->ref_count = 1;
  return obj;
}

void retain_object(KObj *obj) {
  if (obj) {
    obj->ref_count++;
  }
}

void release_object(KObj *obj) {
  if (!obj) {
    return;
  }
  obj->ref_count--;
  if (obj->ref_count == 0) {
    switch (obj->type) {
    case VECTOR:
      for (size_t i = 0; i < obj->as.vector->length; i++) {
        release_object(&obj->as.vector->items[i]);
      }
    break;
    case DICT:
      release_object(obj->as.dict->keys);
      release_object(obj->as.dict->values);
    break;
    case LAMBDA:
    break;
    case ADVERB:
      release_object(obj->as.adverb->child);
    break;
    default:
    break;
    }
  }
}

KObj *create_nil() {
  KObj *obj = create_object(NIL);
  return obj;
}

KObj *create_int(int64_t value) {
  KObj *obj = create_object(INT);
  obj->as.int_value = value;
  return obj;
}

KObj *create_char(char value) {
  KObj *obj = create_object(CHAR);
  obj->as.char_value = value;
  return obj;
}

KObj *create_float(double value) {
  KObj *obj = create_object(FLOAT);
  obj->as.float_value = value;
  return obj;
}

KObj *create_pinf() {
  return create_object(PINF);
}

KObj *create_ninf() {
  return create_object(NINF);
}

KObj *create_vec(size_t capacity) {
  KObj *obj = create_object(VECTOR);
  obj->as.vector = (KVec *)arena_alloc(&global_arena, sizeof(KVec));
  if (!obj->as.vector) {
    fprintf(stderr, "^oom\n");
    return NULL;
  }
  obj->as.vector->length = 0;
  obj->as.vector->capacity = capacity;
  obj->as.vector->items = (capacity > 0)
                              ? (KObj *)arena_alloc(&global_arena, capacity * sizeof(KObj))
                              : NULL;
  if (!obj->as.vector->items && capacity > 0) {
    fprintf(stderr, "^oom\n");
    return NULL;
  }
  return obj;
}

KObj *create_symbol(const char *name) {
  KObj *obj = create_object(SYM);
  obj->as.symbol_value = k_strdup_local(name);
  return obj;
}

KObj *create_dict(KObj *keys, KObj *values) {
  KObj *obj = create_object(DICT);
  obj->as.dict = (KDict *)arena_alloc(&global_arena, sizeof(KDict));
  if (!obj->as.dict) {
    fprintf(stderr, "^oom\n");
    return NULL;
  }
  obj->as.dict->keys = keys;
  obj->as.dict->values = values;
  retain_object(keys);
  retain_object(values);
  return obj;
}

// Retain subobjects for a copied KObj placed inline in a vector.
static void retain_subobjects(KObj *obj) {
  if (!obj) return;
  switch (obj->type) {
  case VECTOR: {
    KVec *v = obj->as.vector;
    for (size_t j = 0; j < v->length; j++) {
      retain_object(&v->items[j]);
    }
  } break;
  case DICT:
    retain_object(obj->as.dict->keys);
    retain_object(obj->as.dict->values);
  break;
  case ADVERB:
    retain_object(obj->as.adverb->child);
  break;
  default:
  break;
  }
}

void vector_append(KObj *vec_obj, KObj *item) {
  if (vec_obj->type != VECTOR) {
    return;
  }
  KVec *vec = vec_obj->as.vector;
  if (vec->length >= vec->capacity) {
    size_t new_capacity = vec->capacity * 2;
    if (new_capacity == 0) new_capacity = 8;
    KObj *new_items = (KObj *)arena_alloc(&global_arena, new_capacity * sizeof(KObj));
    if (vec->items && vec->length > 0) {
      memcpy(new_items, vec->items, vec->length * sizeof(KObj));
    }
    vec->items = new_items;
    vec->capacity = new_capacity;
  }
  vec->items[vec->length] = *item;
  vec->items[vec->length].ref_count = 1;
  retain_subobjects(&vec->items[vec->length]);
  vec->length++;
}

void vector_set(KObj *vec_obj, size_t index, KObj *src) {
  if (!vec_obj || vec_obj->type != VECTOR) return;
  KVec *vec = vec_obj->as.vector;
  if (index >= vec->length) return;
  release_object(&vec->items[index]);
  vec->items[index] = *src;
  vec->items[index].ref_count = 1;
  retain_subobjects(&vec->items[index]);
}

KObj *create_lambda(int param_count, char **params, ASTNode **body,
                    size_t body_count, bool has_return) {
  KObj *obj = create_object(LAMBDA);
  obj->as.lambda = (KLambda *)arena_alloc(&global_arena, sizeof(KLambda));
  obj->as.lambda->param_count = param_count;
  obj->as.lambda->params = params;
  obj->as.lambda->body = body;
  obj->as.lambda->body_count = body_count;
  obj->as.lambda->has_return = has_return;
  return obj;
}

KObj *create_verb(UnaryFunc unary, BinaryFunc binary) {
  KObj *obj = create_object(VERB);
  obj->as.verb.unary = unary;
  obj->as.verb.binary = binary;
  return obj;
}
