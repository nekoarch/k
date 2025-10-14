#include "builtins.h"
#include "def.h"
#include "eval.h"
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <ctype.h>

static bool is_number(KObj *v) {
  return v->type == INT || v->type == FLOAT || v->type == CHAR ||
         v->type == PINF || v->type == NINF;
}

static bool are_numbers(KObj *l, KObj *r) {
  return is_number(l) && is_number(r);
}

static double as_double(KObj *o) {
  switch (o->type) {
  case FLOAT: return o->as.float_value;
  case INT: return (double)o->as.int_value;
  default: return (double)(unsigned char)o->as.char_value;
  }
}

static int64_t as_int(KObj *o) {
  switch (o->type) {
  case INT: return o->as.int_value;
  case CHAR: return (int64_t)(unsigned char)o->as.char_value;
  case FLOAT: return (int64_t)o->as.float_value;
  default: return 0;
  }
}

static bool obj_match(KObj *left, KObj *right) {
  if (left->type != right->type) return false;
  switch (left->type) {
  case NIL:
    return true;
  case CHAR:
    return left->as.char_value == right->as.char_value;
  case INT:
    return left->as.int_value == right->as.int_value;
  case FLOAT:
    return left->as.float_value == right->as.float_value;
  case PINF:
  case NINF:
    return true;
  case SYM:
    return strcmp(left->as.symbol_value, right->as.symbol_value) == 0;
  case VECTOR:
    if (left->as.vector->length != right->as.vector->length) return false;
    for (size_t i = 0; i < left->as.vector->length; i++) {
      KObj *l = &left->as.vector->items[i];
      KObj *r = &right->as.vector->items[i];
      if (l->type == CHAR && r->type == VECTOR && r->as.vector->length == 1 &&
          r->as.vector->items[0].type == CHAR) {
        if (l->as.char_value != r->as.vector->items[0].as.char_value)
          return false;
        continue;
      }
      if (r->type == CHAR && l->type == VECTOR && l->as.vector->length == 1 &&
          l->as.vector->items[0].type == CHAR) {
        if (r->as.char_value != l->as.vector->items[0].as.char_value)
          return false;
        continue;
      }
      if (!obj_match(l, r)) return false;
    }
    return true;
  case DICT:
    return obj_match(left->as.dict->keys, right->as.dict->keys) &&
           obj_match(left->as.dict->values, right->as.dict->values);
  case VERB:
  case ADVERB:
  case LAMBDA:
    return left == right;
  default:
    return false;
  }
}

KObj *k_match(KObj *left, KObj *right) {
  return create_int(obj_match(left, right));
}

static KObj *apply_dict_binary(KObj *left, KObj *right,
                               KObj *(*op)(KObj *, KObj *));
static KObj *apply_vector_binary(KObj *left, KObj *right,
                                 KObj *(*op)(KObj *, KObj *));

static KObj *apply_binary(KObj *left, KObj *right,
                          KObj *(*op)(KObj *, KObj *)) {
  if (left->type == DICT || right->type == DICT)
    return apply_dict_binary(left, right, op);
  if (left->type == VECTOR || right->type == VECTOR)
    return apply_vector_binary(left, right, op);
  return op(left, right);
}

static KObj *apply_dict_binary(KObj *left, KObj *right,
                               KObj *(*op)(KObj *, KObj *)) {
  if (left->type == DICT && right->type == DICT) {
    KObj *vals = apply_binary(left->as.dict->values,
                              right->as.dict->values, op);
    if (vals->type == NIL) return vals;
    KObj *dict = create_dict(left->as.dict->keys, vals);
    release_object(vals);
    return dict;
  }
  if (left->type == DICT) {
    KObj *vals = apply_binary(left->as.dict->values, right, op);
    if (vals->type == NIL) return vals;
    KObj *dict = create_dict(left->as.dict->keys, vals);
    release_object(vals);
    return dict;
  }
  KObj *vals = apply_binary(left, right->as.dict->values, op);
  if (vals->type == NIL) return vals;
  KObj *dict = create_dict(right->as.dict->keys, vals);
  release_object(vals);
  return dict;
}

static KObj *apply_vector_binary(KObj *left, KObj *right,
                                 KObj *(*op)(KObj *, KObj *)) {
  if (left->type == VECTOR && right->type == VECTOR) {
    if (left->as.vector->length != right->as.vector->length) {
      printf("^length\n");
      return create_nil();
    }
    KObj *vec = create_vec(left->as.vector->length);
    for (size_t i = 0; i < left->as.vector->length; i++) {
      KObj *res = apply_binary(&left->as.vector->items[i],
                               &right->as.vector->items[i], op);
      if (res->type == NIL) {
        release_object(vec);
        return res;
      }
      vector_append(vec, res);
      release_object(res);
    }
    return vec;
  }
  if (left->type == VECTOR) {
    KObj *vec = create_vec(left->as.vector->length);
    for (size_t i = 0; i < left->as.vector->length; i++) {
      KObj *res = apply_binary(&left->as.vector->items[i], right, op);
      if (res->type == NIL) {
        release_object(vec);
        return res;
      }
      vector_append(vec, res);
      release_object(res);
    }
    return vec;
  }
  KObj *vec = create_vec(right->as.vector->length);
  for (size_t i = 0; i < right->as.vector->length; i++) {
    KObj *res = apply_binary(left, &right->as.vector->items[i], op);
    if (res->type == NIL) {
      release_object(vec);
      return res;
    }
    vector_append(vec, res);
    release_object(res);
  }
  return vec;
}

static KObj *op_add(KObj *left, KObj *right) {
  if (!are_numbers(left, right)) return create_nil();
  if (left->type == PINF || right->type == PINF) return create_pinf();
  if (left->type == NINF || right->type == NINF) return create_ninf();
  if (left->type == FLOAT || right->type == FLOAT)
    return create_float(as_double(left) + as_double(right));
  return create_int(as_int(left) + as_int(right));
}

static KObj *op_sub(KObj *left, KObj *right) {
  if (!are_numbers(left, right)) return create_nil();
  if (left->type == PINF || right->type == NINF) return create_pinf();
  if (left->type == NINF || right->type == PINF) return create_ninf();
  if (left->type == FLOAT || right->type == FLOAT)
    return create_float(as_double(left) - as_double(right));
  return create_int(as_int(left) - as_int(right));
}

static KObj *op_mul(KObj *left, KObj *right) {
  if (!are_numbers(left, right)) return create_nil();
  if (left->type == PINF || right->type == PINF) return create_pinf();
  if (left->type == NINF || right->type == NINF) return create_ninf();
  if (left->type == FLOAT || right->type == FLOAT)
    return create_float(as_double(left) * as_double(right));
  return create_int(as_int(left) * as_int(right));
}

static KObj *op_div(KObj *left, KObj *right) {
  if (!are_numbers(left, right)) return create_nil();
  if (left->type == PINF || right->type == NINF) return create_pinf();
  if (left->type == NINF || right->type == PINF) return create_ninf();
  double r = as_double(right);
  if (r == 0) {
    double l = as_double(left);
    return l >= 0 ? create_pinf() : create_ninf();
  }
  if (left->type == FLOAT || right->type == FLOAT)
    return create_float(as_double(left) / r);
  return create_float((1.0 * as_int(left)) / as_int(right));
}

static KObj *op_max(KObj *left, KObj *right) {
  if (!are_numbers(left, right)) return create_nil();
  if (left->type == PINF || right->type == PINF) return create_pinf();
  if (left->type == NINF && right->type == NINF) return create_ninf();
  if (left->type == NINF)
    return right->type == FLOAT ? create_float(as_double(right))
                                : create_int(as_int(right));
  if (right->type == NINF)
    return left->type == FLOAT ? create_float(as_double(left))
                               : create_int(as_int(left));
  if (left->type == FLOAT || right->type == FLOAT)
    return create_float(as_double(left) > as_double(right)
                            ? as_double(left)
                            : as_double(right));
  int64_t l = as_int(left), r_int = as_int(right);
  return create_int(l > r_int ? l : r_int);
}

static KObj *op_min(KObj *left, KObj *right) {
  if (!are_numbers(left, right)) return create_nil();
  if (left->type == NINF || right->type == NINF) return create_ninf();
  if (left->type == PINF && right->type == PINF) return create_pinf();
  if (left->type == PINF)
    return right->type == FLOAT ? create_float(as_double(right))
                                : create_int(as_int(right));
  if (right->type == PINF)
    return left->type == FLOAT ? create_float(as_double(left))
                               : create_int(as_int(left));
  if (left->type == FLOAT || right->type == FLOAT)
    return create_float(as_double(left) < as_double(right)
                            ? as_double(left)
                            : as_double(right));
  int64_t l = as_int(left), r_int = as_int(right);
  return create_int(l < r_int ? l : r_int);
}

static KObj *op_lt(KObj *left, KObj *right) {
  if (are_numbers(left, right)) {
    if (left->type == PINF || right->type == NINF) return create_int(0);
    if (left->type == NINF || right->type == PINF) return create_int(1);
    if (left->type == FLOAT || right->type == FLOAT)
      return create_int(as_double(left) < as_double(right));
    return create_int(as_int(left) < as_int(right));
  }
  if (left->type == SYM && right->type == SYM)
    return create_int(strcmp(left->as.symbol_value, right->as.symbol_value) < 0);
  return create_int(0);
}

static KObj *op_gt(KObj *left, KObj *right) {
  return op_lt(right, left);
}

static KObj *op_eq(KObj *left, KObj *right) {
  if (are_numbers(left, right)) {
    if (left->type == PINF || left->type == NINF || right->type == PINF ||
        right->type == NINF)
      return create_int(left->type == right->type);
    return create_int(as_double(left) == as_double(right));
  }
  if (left->type == SYM && right->type == SYM)
    return create_int(strcmp(left->as.symbol_value, right->as.symbol_value) == 0);
  if (left->type == right->type) {
    if (left->type == NIL) return create_int(1);
    return create_int(left == right);
  }
  return create_int(0);
}

static KObj *op_sin(KObj *left, KObj *value) {
  (void)left;
  if (!is_number(value)) return create_nil();
  if (value->type == PINF || value->type == NINF) return create_nil();
  double v = as_double(value);
  return create_float(sin(v));
}

static KObj *op_cos(KObj *left, KObj *value) {
  (void)left;
  if (!is_number(value)) return create_nil();
  if (value->type == PINF || value->type == NINF) return create_nil();
  double v = as_double(value);
  return create_float(cos(v));
}

static KObj *op_abs(KObj *left, KObj *value) {
  (void)left;
  if (!is_number(value)) return create_nil();
  if (value->type == PINF || value->type == NINF) return create_pinf();
  if (value->type == FLOAT) {
    return create_float(fabs(as_double(value)));
  }
  int64_t v = as_int(value);
  if (v < 0) v = -v;
  return create_int(v);
}

static KObj *op_sqrt(KObj *left, KObj *value) {
  (void)left;
  if (!is_number(value)) return create_nil();
  if (value->type == PINF) return create_pinf();
  if (value->type == NINF) return create_ninf();
  double v = as_double(value);
  if (v < 0) return create_nil();
  return create_float(sqrt(v));
}

static KObj *op_not(KObj *left, KObj *value) {
  (void)left;
  if (!is_number(value)) return create_nil();
  if (value->type == PINF || value->type == NINF) return create_int(0);
  if (value->type == FLOAT) return create_int(as_double(value) == 0.0);
  return create_int(as_int(value) == 0);
}

static KObj *op_floor(KObj *left, KObj *value) {
  (void)left;
  if (value->type == PINF) return create_pinf();
  if (value->type == NINF) return create_ninf();
  if (value->type == FLOAT) {
    double v = as_double(value);
    return create_int((int64_t)floor(v));
  }
  if (value->type == INT) {
    return create_int(as_int(value));
  }
  if (value->type == CHAR) {
    char c = (char)tolower((unsigned char)value->as.char_value);
    return create_char(c);
  }
  return create_nil();
}

KObj *k_add(KObj *left, KObj *right) {
  return apply_binary(left, right, op_add);
}

KObj *k_sub(KObj *left, KObj *right) {
  return apply_binary(left, right, op_sub);
}

KObj *k_mul(KObj *left, KObj *right) {
  return apply_binary(left, right, op_mul);
}

KObj *k_div(KObj *left, KObj *right) {
  return apply_binary(left, right, op_div);
}

KObj *k_max(KObj *left, KObj *right) {
  return apply_binary(left, right, op_max);
}

KObj *k_min(KObj *left, KObj *right) {
  return apply_binary(left, right, op_min);
}

KObj *k_less(KObj *left, KObj *right) {
  return apply_binary(left, right, op_lt);
}

KObj *k_more(KObj *left, KObj *right) {
  return apply_binary(left, right, op_gt);
}

KObj *k_eq(KObj *left, KObj *right) {
  return apply_binary(left, right, op_eq);
}

KObj *k_sin(KObj *value) {
  return apply_binary(value, value, op_sin);
}

KObj *k_cos(KObj *value) {
  return apply_binary(value, value, op_cos);
}

KObj *k_abs(KObj *value) {
  return apply_binary(value, value, op_abs);
}

KObj *k_sqrt(KObj *value) {
  return apply_binary(value, value, op_sqrt);
}

KObj *k_floor(KObj *value) {
  return apply_binary(value, value, op_floor);
}

KObj *k_negate(KObj *value) {
  KObj *zero = create_float(0);
  KObj *res = k_sub(zero, value);
  release_object(zero);
  return res;
}

KObj *k_not(KObj *value) {
  return apply_binary(value, value, op_not);
}

static KObj *k_where_vector(KObj *vec) {
  // First pass: validate and compute total output length
  size_t total = 0;
  for (size_t i = 0; i < vec->as.vector->length; i++) {
    KObj *item = &vec->as.vector->items[i];
    if (!is_number(item)) {
      return create_nil();
    }
    int64_t count = as_int(item);
    if (count > 0) total += (size_t)count;
  }
  // Allocate once and fill inline with ints
  KObj *result = create_vec(total);
  KObj *items = result->as.vector->items;
  size_t pos = 0;
  for (size_t i = 0; i < vec->as.vector->length; i++) {
    int64_t count = as_int(&vec->as.vector->items[i]);
    if (count < 0) count = 0;
    for (int64_t j = 0; j < count; j++) {
      items[pos].type = INT;
      items[pos].ref_count = 1;
      items[pos].as.int_value = (int64_t)i;
      pos++;
    }
  }
  result->as.vector->length = pos;
  return result;
}

static KObj *k_where_scalar(KObj *value) {
  if (!is_number(value)) return create_nil();
  int64_t count = as_int(value);
  if (count < 0) count = 0;
  KObj *result = create_vec((size_t)count);
  KObj *items = result->as.vector->items;
  for (int64_t i = 0; i < count; i++) {
    items[i].type = INT;
    items[i].ref_count = 1;
    items[i].as.int_value = 0;
  }
  result->as.vector->length = (size_t)count;
  return result;
}

KObj *k_where(KObj *value) {
  return value->type == VECTOR ? k_where_vector(value) : k_where_scalar(value);
}

KObj* k_first(KObj* value) {
  if (value->type == VECTOR) {
    if (value->as.vector->length == 0) {
      printf("^length\n");
      return create_nil();
    }
    KObj* first = &value->as.vector->items[0];
    retain_object(first);
    return first;
  }
  if (value->type == DICT) {
    KObj *vals = value->as.dict->values;
    if (vals->as.vector->length == 0) {
      printf("^length\n");
      return create_nil();
    }
    KObj *first = &vals->as.vector->items[0];
    retain_object(first);
    return first;
  }
  retain_object(value);
  return value;
}

KObj* k_enlist(KObj* value) {
  KObj* vec = create_vec(1);
  vector_append(vec, value);
  return vec;
}

static bool is_char_vector(KObj* obj) {
  if (obj->type != VECTOR) return false;
  for (size_t i = 0; i < obj->as.vector->length; i++) {
    if (obj->as.vector->items[i].type != CHAR) return false;
  }
  return true;
}

static size_t compute_max_cols(KObj *value) {
  size_t rows = value->as.vector->length;
  size_t max_cols = 0;
  for (size_t r = 0; r < rows; r++) {
    KObj *row = &value->as.vector->items[r];
    size_t len = (row->type == VECTOR) ? row->as.vector->length : 1;
    if (len > max_cols) max_cols = len;
  }
  return max_cols;
}

static KObj *build_flip_row(KObj *value, size_t column, size_t rows) {
  KObj *new_row = create_vec(rows);
  for (size_t r = 0; r < rows; r++) {
    KObj *row = &value->as.vector->items[r];
    if (row->type == VECTOR) {
      if (column < row->as.vector->length) {
        KObj *item = &row->as.vector->items[column];
        vector_append(new_row, item);
      } else {
        KObj *filler = is_char_vector(row) ? create_char(' ') : create_int(0);
        vector_append(new_row, filler);
        release_object(filler);
      }
    } else {
      vector_append(new_row, row);
    }
  }
  return new_row;
}

KObj *k_flip(KObj *value) {
  if (value->type != VECTOR) {
    printf("^rank\n");
    return create_nil();
  }
  size_t rows = value->as.vector->length;
  if (rows == 0) return create_vec(0);
  size_t max_cols = compute_max_cols(value);
  KObj *result = create_vec(max_cols);
  for (size_t c = 0; c < max_cols; c++) {
    KObj *new_row = build_flip_row(value, c, rows);
    vector_append(result, new_row);
    release_object(new_row);
  }
  return result;
}

KObj* k_rev(KObj* value) {
  if (value->type != VECTOR) {
    retain_object(value);
    return value;
  }
  size_t len = value->as.vector->length;
  KObj* result = create_vec(len);
  for (size_t i = 0; i < len; i++) {
    KObj* item = &value->as.vector->items[len - i - 1];
    vector_append(result, item);
  }
  return result;
}
static int asc_cmp(KObj* a, KObj* b, bool* domain) {
  if (is_number(a) && is_number(b)) {
    if (a->type == PINF || b->type == NINF) return 1;
    if (a->type == NINF || b->type == PINF) return -1;
    if (a->type == FLOAT || b->type == FLOAT) {
      double da = as_double(a);
      double db = as_double(b);
      if (da < db) return -1;
      if (da > db) return 1;
      return 0;
    }
    int64_t ia = as_int(a);
    int64_t ib = as_int(b);
    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
  }
  if (a->type == SYM && b->type == SYM) {
    int c = strcmp(a->as.symbol_value, b->as.symbol_value);
    return c < 0 ? -1 : (c > 0);
  }
  if (a->type == VECTOR && b->type == VECTOR) {
    size_t alen = a->as.vector->length;
    size_t blen = b->as.vector->length;
    size_t n = alen < blen ? alen : blen;
    for (size_t i = 0; i < n; i++) {
      int cmp = asc_cmp(&a->as.vector->items[i], &b->as.vector->items[i], domain);
      if (*domain) return 0;
      if (cmp != 0) return cmp;
    }
    if (alen < blen) return -1;
    if (alen > blen) return 1;
    return 0;
  }
  *domain = true;
  return 0;
}

KObj* k_asc(KObj* value) {
  if (value->type != VECTOR) {
    KObj* result = create_vec(1);
    KObj* idx = create_int(0);
    vector_append(result, idx);
    release_object(idx);
    return result;
  }
  size_t len = value->as.vector->length;
  if (len == 0) {
    return create_vec(0);
  }
  KObj* items = value->as.vector->items;
  KType first_type = items[0].type;
  int first_num = is_number(&items[0]);
  for (size_t i = 1; i < len; i++) {
    if (first_num) {
      if (!is_number(&items[i])) {
        printf("^domain\n");
        return create_nil();
      }
    } else {
      if (items[i].type != first_type) {
        printf("^domain\n");
        return create_nil();
      }
    }
  }
  size_t* idxs = (size_t*)malloc(sizeof(size_t) * len);
  for (size_t i = 0; i < len; i++) idxs[i] = i;
  bool domain = false;
  size_t *tmp = (size_t*)malloc(sizeof(size_t) * len);
  if (!tmp) {
    free(idxs);
    printf("^oom\n");
    return create_nil();
  }
  for (size_t width = 1; width < len; width *= 2) {
    for (size_t i = 0; i < len; i += 2 * width) {
      size_t left = i;
      size_t mid = (i + width < len) ? (i + width) : len;
      size_t right = (i + 2 * width < len) ? (i + 2 * width) : len;
      size_t p = left, q = mid, t = left;
      while (p < mid && q < right) {
        int c = asc_cmp(&items[idxs[p]], &items[idxs[q]], &domain);
        if (domain) break;
        if (c < 0 || (c == 0 && idxs[p] <= idxs[q])) {
          tmp[t++] = idxs[p++];
        } else {
          tmp[t++] = idxs[q++];
        }
      }
      while (p < mid) tmp[t++] = idxs[p++];
      while (q < right) tmp[t++] = idxs[q++];
      for (size_t k = left; k < right; k++) idxs[k] = tmp[k];
      if (domain) break;
    }
    if (domain) break;
  }
  free(tmp);
  if (domain) {
    free(idxs);
    printf("^domain\n");
    return create_nil();
  }
  KObj* result = create_vec(len);
  for (size_t i = 0; i < len; i++) {
    KObj* idx = create_int((int64_t)idxs[i]);
    vector_append(result, idx);
    release_object(idx);
  }
  free(idxs);
  return result;
}

KObj *k_desc(KObj *value) {
  return k_rev(k_asc(value));
}

KObj* k_sort(KObj* value) {
  if (value->type == VECTOR) {
    size_t len = value->as.vector->length;
    KObj* idxs = k_asc(value);
    if (idxs->type == NIL) return idxs;
    KObj* res = create_vec(len);
    for (size_t i = 0; i < len; i++) {
      KObj* idx = &idxs->as.vector->items[i];
      size_t id = (size_t)idx->as.int_value;
      vector_append(res, &value->as.vector->items[id]);
    }
    release_object(idxs);
    return res;
  }
  if (value->type == DICT) {
    KObj* keys = value->as.dict->keys;
    KObj* vals = value->as.dict->values;
    size_t len = keys->as.vector->length;
    KObj* idxs = k_asc(keys);
    if (idxs->type == NIL) return idxs;
    KObj* sk = create_vec(len);
    KObj* sv = create_vec(len);
    for (size_t i = 0; i < len; i++) {
      KObj* idx = &idxs->as.vector->items[i];
      size_t id = (size_t)idx->as.int_value;
      vector_append(sk, &keys->as.vector->items[id]);
      vector_append(sv, &vals->as.vector->items[id]);
    }
    release_object(idxs);
    KObj* dict = create_dict(sk, sv);
    release_object(sk);
    release_object(sv);
    return dict;
  }
  retain_object(value);
  return value;
}

static bool eq_bool(KObj *left, KObj *right) {
  if (are_numbers(left, right)) {
    if (left->type == PINF || left->type == NINF ||
        right->type == PINF || right->type == NINF) {
      return left->type == right->type;
    }
    return as_double(left) == as_double(right);
  }
  if (left->type == SYM && right->type == SYM) {
    return strcmp(left->as.symbol_value, right->as.symbol_value) == 0;
  }
  if (left->type == right->type) {
    if (left->type == NIL) return true;
    return left == right;
  }
  return false;
}

static uint64_t mix64(uint64_t x) {
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  return x;
}

static uint64_t hash_str64(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  while (*s) {
    h ^= (unsigned char)(*s++);
    h *= 1099511628211ULL;
  }
  return h;
}

static uint64_t hash_obj(KObj *o) {
  switch (o->type) {
  case INT: {
    uint64_t u = (uint64_t)o->as.int_value;
    double d = (double)o->as.int_value;
    union { double d; uint64_t u; } pun = { .d = d };
    if (pun.d == 0.0) pun.u = 0;
    return mix64(pun.u ^ 0x9e3779b97f4a7c15ULL);
  }
  case CHAR: {
    double d = (double)(unsigned char)o->as.char_value;
    union { double d; uint64_t u; } pun = { .d = d };
    return mix64(pun.u ^ 0x9e3779b97f4a7c15ULL);
  }
  case FLOAT: {
    double d = o->as.float_value;
    union { double d; uint64_t u; } pun = { .d = d };
    if (pun.d == 0.0) pun.u = 0;
    return mix64(pun.u ^ 0x517cc1b727220a95ULL);
  }
  case PINF:
    return 0x7ff0000000000001ULL;
  case NINF:
    return 0xfff0000000000001ULL;
  case SYM:
    return mix64(hash_str64(o->as.symbol_value));
  default:
    return mix64((uintptr_t)o);
  }
}

KObj* k_group(KObj* value) {
  KObj* vec = value;
  int created = 0;
  if (value->type != VECTOR) {
    vec = create_vec(1);
    vector_append(vec, value);
    created = 1;
  }
  for (size_t i = 0; i < vec->as.vector->length; i++) {
    if (vec->as.vector->items[i].type == VECTOR) {
      if (created) release_object(vec);
      printf("^rank\n");
      return create_nil();
    }
  }
  KObj* keys = create_vec(8);
  KObj* vals = create_vec(8);
  size_t n = vec->as.vector->length;
  size_t cap = 1;
  while (cap < (n ? (n << 1) : 1)) cap <<= 1;
  size_t *map = (size_t*)malloc(sizeof(size_t) * cap);
  if (!map) {
    if (created) release_object(vec);
    release_object(keys);
    release_object(vals);
    printf("^oom\n");
    return create_nil();
  }
  for (size_t i = 0; i < cap; i++) map[i] = SIZE_MAX;

  for (size_t i = 0; i < n; i++) {
    KObj* item = &vec->as.vector->items[i];
    uint64_t h = hash_obj(item);
    size_t p = (size_t)(h & (cap - 1));
    size_t slot;
    for (;;) {
      slot = map[p];
      if (slot == SIZE_MAX) break;
      KObj *key = &keys->as.vector->items[slot];
      if (eq_bool(item, key)) break;
      p = (p + 1) & (cap - 1);
    }
    if (slot != SIZE_MAX) {
      KObj* idxs = &vals->as.vector->items[slot];
      KObj* idx = create_int((int64_t)i);
      vector_append(idxs, idx);
      release_object(idx);
    } else {
      size_t new_index = keys->as.vector->length;
      vector_append(keys, item);
      KObj* idxs = create_vec(1);
      KObj* idx = create_int((int64_t)i);
      vector_append(idxs, idx);
      release_object(idx);
      vector_append(vals, idxs);
      release_object(idxs);
      map[p] = new_index;
    }
  }
  free(map);
  KObj* dict = create_dict(keys, vals);
  release_object(keys);
  release_object(vals);
  if (created) {
    release_object(vec);
  }
  return dict;
}

static KObj *build_enum_row(int64_t *dims, size_t dims_len, size_t idx, int64_t total) {
  KObj *row = create_vec((size_t)total);
  if (total == 0 || dims[idx] == 0) return row;
  int64_t repeat_after = 1;
  for (size_t j = idx + 1; j < dims_len; j++) repeat_after *= dims[j];
  if (repeat_after == 0) return row;
  int64_t repeat_before = total / (repeat_after * dims[idx]);
  KObj *items = row->as.vector->items;
  KObj **vals = (KObj **)malloc(sizeof(KObj *) * (size_t)dims[idx]);
  if (!vals) return row;
  for (int64_t val = 0; val < dims[idx]; val++) {
    vals[val] = create_int(val);
  }
  size_t pos = 0;
  for (int64_t b = 0; b < repeat_before; b++) {
    for (int64_t val = 0; val < dims[idx]; val++) {
      for (int64_t a = 0; a < repeat_after; a++) {
        items[pos++] = *vals[val];
      }
    }
  }
  row->as.vector->length = (size_t)total;
  for (int64_t val = 0; val < dims[idx]; val++) release_object(vals[val]);
  free(vals);
  return row;
}

static KObj *enum_vector(KObj *value) {
  size_t dims_len = value->as.vector->length;
  if (dims_len == 0) return create_vec(0);
  int64_t *dims = (int64_t *)malloc(sizeof(int64_t) * dims_len);
  int64_t total = 1;
  for (size_t i = 0; i < dims_len; i++) {
    KObj *item = &value->as.vector->items[i];
    if (!is_number(item)) {
      free(dims);
      return create_nil();
    }
    int64_t d = as_int(item);
    if (d < 0) d = 0;
    dims[i] = d;
    total *= d;
  }
  KObj *result = create_vec(dims_len);
  for (size_t i = 0; i < dims_len; i++) {
    KObj *row = build_enum_row(dims, dims_len, i, total);
    vector_append(result, row);
    release_object(row);
  }
  free(dims);
  return result;
}

static KObj *enum_positive(int64_t n) {
  KObj *result = create_vec((size_t)n);
  for (int64_t i = 0; i < n; i++) {
    KObj *idx = create_int(i);
    vector_append(result, idx);
    release_object(idx);
  }
  return result;
}

static KObj *enum_negative(int64_t m) {
  KObj *result = create_vec((size_t)m);
  KObj *zero = create_int(0);
  KObj *one = create_int(1);
  for (int64_t r = 0; r < m; r++) {
    KObj *row = create_vec((size_t)m);
    KObj *items = row->as.vector->items;
    for (int64_t c = 0; c < m; c++) {
      KObj *elem = (r == c) ? one : zero;
      items[c] = *elem;
    }
    row->as.vector->length = (size_t)m;
    vector_append(result, row);
    release_object(row);
  }
  release_object(zero);
  release_object(one);
  return result;
}

KObj *k_enum(KObj *value) {
  if (value->type == VECTOR) return enum_vector(value);
  if (!is_number(value)) return create_nil();
  int64_t n = as_int(value);
  return n >= 0 ? enum_positive(n) : enum_negative(-n);
}


KObj* k_count(KObj* value) {
  if (value->type == VECTOR) {
    return create_int((int64_t)value->as.vector->length);
  }
  if (value->type == DICT) {
    return create_int((int64_t)value->as.dict->keys->as.vector->length);
  }
  if (value->type == NIL) {
    return create_int(0);
  }
  return create_int(1);
}

KObj *k_key(KObj *left, KObj *right) {
  if (left->type != VECTOR || right->type != VECTOR) {
    printf("^domain\n");
    return create_nil();
  }
  if (left->as.vector->length != right->as.vector->length) {
    printf("^length\n");
    return create_nil();
  }
  size_t len = left->as.vector->length;
  for (size_t i = 0; i < len; i++) {
    KObj *lk = &left->as.vector->items[i];
    KObj *rv = &right->as.vector->items[i];
    if (lk->type == VECTOR || lk->type == DICT || lk->type == VERB ||
        lk->type == ADVERB || lk->type == LAMBDA ||
        rv->type == VECTOR || rv->type == DICT || rv->type == VERB ||
        rv->type == ADVERB || rv->type == LAMBDA) {
      printf("^domain\n");
      return create_nil();
    }
  }
  return create_dict(left, right);
}

static KObj* take_n(int64_t n, KObj* src) {
  if (n <= 0) {
    return create_vec(0);
  }
  if (src->type == VECTOR) {
    size_t len = src->as.vector->length;
    if (len == 0) return create_vec(0);
    KObj* res = create_vec((size_t)n);
    for (int64_t i = 0; i < n; i++) {
      KObj* item = &src->as.vector->items[i % len];
      vector_append(res, item);
    }
    return res;
  }
  if (src->type == DICT) {
    KObj* keys = src->as.dict->keys;
    KObj* vals = src->as.dict->values;
    size_t len = keys->as.vector->length;
    if (len == 0) {
      KObj* k = create_vec(0);
      KObj* v = create_vec(0);
      KObj* d = create_dict(k, v);
      release_object(k);
      release_object(v);
      return d;
    }
    KObj* k = create_vec((size_t)n);
    KObj* v = create_vec((size_t)n);
    for (int64_t i = 0; i < n; i++) {
      KObj* key = &keys->as.vector->items[i % len];
      KObj* val = &vals->as.vector->items[i % len];
      vector_append(k, key);
      vector_append(v, val);
    }
    KObj* d = create_dict(k, v);
    release_object(k);
    release_object(v);
    return d;
  }
  KObj* res = create_vec((size_t)n);
  for (int64_t i = 0; i < n; i++) {
    vector_append(res, src);
  }
  return res;
}

typedef struct {
  KObj* flat;
  int64_t index;
} FlatIter;

static KObj* build_shape(FlatIter* it, int64_t* dims, size_t dim_idx, size_t dims_len) {
  int64_t n = dims[dim_idx];
  KObj* res = create_vec((size_t)n);
  if (dim_idx == dims_len - 1) {
    for (int64_t i = 0; i < n; i++) {
      KObj* item = &it->flat->as.vector->items[it->index++];
      vector_append(res, item);
    }
  } else {
    for (int64_t i = 0; i < n; i++) {
      KObj* child = build_shape(it, dims, dim_idx + 1, dims_len);
      vector_append(res, child);
      release_object(child);
    }
  }
  return res;
}

KObj *k_take(KObj *left, KObj *right) {
  if (right->type == DICT && left->type != INT) {
    printf("^type\n");
    return create_nil();
  }
  if (left->type == INT) {
    return take_n(left->as.int_value, right);
  }
  if (left->type != VECTOR) {
    printf("^type\n");
    return create_nil();
  }
  size_t dims_len = left->as.vector->length;
  int64_t* dims = (int64_t*)malloc(sizeof(int64_t) * dims_len);
  int64_t total = 1;
  for (size_t i = 0; i < dims_len; i++) {
    KObj* d = &left->as.vector->items[i];
    if (d->type != INT) {
      free(dims);
      printf("^type\n");
      return create_nil();
    }
    int64_t v = d->as.int_value;
    if (v < 0) v = 0;
    dims[i] = v;
    total *= v;
  }
  KObj* src_vec = NULL;
  int created = 0;
  if (right->type == VECTOR) {
    src_vec = right;
  } else {
    src_vec = create_vec(1);
    vector_append(src_vec, right);
    created = 1;
  }
  size_t src_len = src_vec->as.vector->length;
  if (src_len == 0) {
    if (created) release_object(src_vec);
    free(dims);
    return create_vec(0);
  }
  KObj* flat = create_vec((size_t)total);
  for (int64_t i = 0; i < total; i++) {
    KObj* item = &src_vec->as.vector->items[i % src_len];
    vector_append(flat, item);
  }
  if (created) release_object(src_vec);
  FlatIter it = {flat, 0};
  KObj* result = build_shape(&it, dims, 0, dims_len);
  release_object(flat);
  free(dims);
  return result;
}

static KObj* drop_int(int64_t n, KObj* src) {
  if (src->type == VECTOR) {
    size_t len = src->as.vector->length;
    int64_t start = 0, end = (int64_t)len;
    if (n > 0) {
      start = n > (int64_t)len ? (int64_t)len : n;
    } else if (n < 0) {
      end = (int64_t)len + n;
      if (end < 0) end = 0;
    }
    if (end < start) end = start;
    KObj* res = create_vec((size_t)(end - start));
    for (int64_t i = start; i < end; i++) {
      vector_append(res, &src->as.vector->items[i]);
    }
    return res;
  }
  if (src->type == DICT) {
    KObj* keys = drop_int(n, src->as.dict->keys);
    KObj* vals = drop_int(n, src->as.dict->values);
    KObj* d = create_dict(keys, vals);
    release_object(keys);
    release_object(vals);
    return d;
  }
  KObj* res = create_vec(0);
  if (n == 0) {
    vector_append(res, src);
  }
  return res;
}

static KObj* drop_list(KObj* lst, KObj* src) {
  KObj* vec = NULL;
  int created = 0;
  if (src->type == VECTOR) {
    vec = src;
  } else {
    vec = create_vec(1);
    vector_append(vec, src);
    created = 1;
  }
  KObj* res = create_vec(vec->as.vector->length);
  for (size_t i = 0; i < vec->as.vector->length; i++) {
    KObj* item = &vec->as.vector->items[i];
    bool remove = false;
    for (size_t j = 0; j < lst->as.vector->length && !remove; j++) {
      if (obj_match(&lst->as.vector->items[j], item)) {
        remove = true;
      }
    }
    if (!remove) {
      vector_append(res, item);
    }
  }
  if (created) release_object(vec);
  return res;
}

static KObj* drop_fn(KObj* fn, KObj* src) {
  KObj* vec = NULL;
  int created = 0;
  if (src->type == VECTOR) {
    vec = src;
  } else {
    vec = create_vec(1);
    vector_append(vec, src);
    created = 1;
  }
  KObj* res = create_vec(vec->as.vector->length);
  for (size_t i = 0; i < vec->as.vector->length; i++) {
    KObj* item = &vec->as.vector->items[i];
    KObj* r = call_unary(fn, item);
    if (r->type == NIL) {
      if (created) release_object(vec);
      release_object(res);
      return r;
    }
    bool drop = true;
    switch (r->type) {
    case INT: drop = (r->as.int_value != 0); break;
    case FLOAT: drop = (r->as.float_value != 0.0); break;
    case CHAR: drop = (r->as.char_value != 0); break;
    case NIL: drop = false; break;
    default: drop = true; break;
    }
    release_object(r);
    if (!drop) {
      vector_append(res, item);
    }
  }
  if (created) release_object(vec);
  return res;
}

KObj* k_drop(KObj* left, KObj* right) {
  if (left->type == INT) {
    return drop_int(left->as.int_value, right);
  }
  if (left->type == VECTOR) {
    return drop_list(left, right);
  }
  if (left->type == VERB || left->type == LAMBDA) {
    return drop_fn(left, right);
  }
  printf("^type\n");
  return create_nil();
}

KObj *k_concat(KObj *left, KObj *right) {
  int left_is_vec = left->type == VECTOR;
  int right_is_vec = right->type == VECTOR;
  if (is_char_vector(left) && is_char_vector(right)) {
    size_t llen = left->as.vector->length;
    size_t rlen = right->as.vector->length;
    size_t len = llen + rlen;
    KObj *res = create_vec(len);
    KObj *out = res->as.vector->items;
    for (size_t i = 0; i < llen; i++) { out[i] = left->as.vector->items[i]; out[i].ref_count = 1; }
    for (size_t i = 0; i < rlen; i++) { out[llen + i] = right->as.vector->items[i]; out[llen + i].ref_count = 1; }
    res->as.vector->length = len;
    return res;
  }
  size_t left_len = left_is_vec ? left->as.vector->length : 1;
  size_t right_len = right_is_vec ? right->as.vector->length : 1;
  KObj *res = create_vec(left_len + right_len);
  if (left_is_vec) {
    for (size_t i = 0; i < left->as.vector->length; i++) {
      vector_append(res, &left->as.vector->items[i]);
    }
  } else {
    vector_append(res, left);
  }
  if (right_is_vec) {
    for (size_t i = 0; i < right->as.vector->length; i++) {
      vector_append(res, &right->as.vector->items[i]);
    }
  } else {
    vector_append(res, right);
  }
  return res;
}

KObj* k_each(KObj* func, KObj* left, KObj* right) {
  bool left_is_vec = left->type == VECTOR;
  bool right_is_vec = right ? right->type == VECTOR : false;
  if (!right) {
    if (!left_is_vec) {
      printf("^type\n");
      return create_nil();
    }
    KObj* res = create_vec(left->as.vector->length);
    for (size_t i = 0; i < left->as.vector->length; i++) {
      KObj* item = &left->as.vector->items[i];
      KObj* val = NULL;
      if (func->type == VERB) {
        if (!func->as.verb.unary) {
          release_object(res);
          printf("^rank\n");
          return create_nil();
        }
        val = func->as.verb.unary(item);
      } else if (func->type == LAMBDA) {
        val = call_unary(func, item);
      } else {
        release_object(res);
        printf("^type\n");
        return create_nil();
      }
      if (val->type == NIL) {
        release_object(res);
        return val;
      }
      vector_append(res, val);
      release_object(val);
    }
    return res;
  }
  if (!left_is_vec && !right_is_vec) {
    printf("^type\n");
    return create_nil();
  }
  size_t len = left_is_vec ? left->as.vector->length : right->as.vector->length;
  if (left_is_vec && right_is_vec &&
      left->as.vector->length != right->as.vector->length) {
    printf("^length\n");
    return create_nil();
  }
  KObj* res = create_vec(len);
  for (size_t i = 0; i < len; i++) {
    KObj* l = left_is_vec ? &left->as.vector->items[i] : left;
    KObj* r = right_is_vec ? &right->as.vector->items[i] : right;
    KObj* val = NULL;
    if (func->type == VERB && func->as.verb.binary) {
      val = func->as.verb.binary(l, r);
    } else if (func->type == LAMBDA) {
      val = call_binary(func, l, r);
    } else {
      release_object(res);
      printf("^type\n");
      return create_nil();
    }
    if (val->type == NIL) {
      release_object(res);
      return val;
    }
    vector_append(res, val);
    release_object(val);
  }
  return res;
}

KObj* k_over(KObj* func, KObj* list, KObj* init) {
  if (list->type != VECTOR) {
    printf("^type\n");
    return create_nil();
  }
  size_t start = 0;
  KObj* result = NULL;
  if (init) {
    retain_object(init);
    result = init;
  } else {
    if (list->as.vector->length == 0) {
      return create_nil();
    }
    result = &list->as.vector->items[0];
    retain_object(result);
    start = 1;
  }
  for (size_t i = start; i < list->as.vector->length; i++) {
    KObj* item = &list->as.vector->items[i];
    KObj* next = NULL;
    if (func->type == VERB && func->as.verb.binary) {
      next = func->as.verb.binary(result, item);
    } else if (func->type == LAMBDA) {
      next = call_binary(func, result, item);
    } else {
      printf("^type\n");
      release_object(result);
      return create_nil();
    }
    release_object(result);
    result = next;
    if (result->type == NIL) return result;
  }
  return result;
}

KObj* k_join(KObj* sep, KObj* list) {
  if (list->type != VECTOR) {
    printf("^type\n");
    return create_nil();
  }
  KObj* sep_char = NULL;
  if (sep->type == CHAR) {
    sep_char = sep;
  } else if (sep->type == VECTOR && sep->as.vector->length == 1 &&
             is_char_vector(sep)) {
    sep_char = &sep->as.vector->items[0];
  } else {
    printf("^type\n");
    return create_nil();
  }
  size_t total_len = 0;
  size_t list_len = list->as.vector->length;
  for (size_t i = 0; i < list_len; i++) {
    KObj* item = &list->as.vector->items[i];
    if (!is_char_vector(item)) {
      printf("^type (join requires list of strings)\n");
      return create_nil();
    }
    total_len += item->as.vector->length;
  }
  if (list_len > 1) total_len += (list_len - 1);

  KObj* res = create_vec(total_len);
  KObj* out = res->as.vector->items;
  size_t pos = 0;
  for (size_t i = 0; i < list_len; i++) {
    KObj* item = &list->as.vector->items[i];
    for (size_t j = 0; j < item->as.vector->length; j++) {
      // Copy char inline
      out[pos] = item->as.vector->items[j];
      out[pos].ref_count = 1;
      pos++;
    }
    if (i < list_len - 1) {
      out[pos].type = CHAR;
      out[pos].ref_count = 1;
      out[pos].as.char_value = sep_char->as.char_value;
      pos++;
    }
  }
  res->as.vector->length = pos;
  return res;
}

KObj* k_decode(KObj* base, KObj* list) {
  if (base->type != INT || list->type != VECTOR) {
    printf("^type\n");
    return create_nil();
  }
  int64_t b = base->as.int_value;
  int64_t result = 0;
  for (size_t i = 0; i < list->as.vector->length; i++) {
    KObj* item = &list->as.vector->items[i];
    if (!is_number(item)) {
      printf("^type\n");
      return create_nil();
    }
    result = result * b + as_int(item);
  }
  return create_int(result);
}

KObj* k_encode(KObj* base, KObj* num) {
  if (base->type != INT || num->type != INT) {
    printf("^type\n");
    return create_nil();
  }
  int64_t b = base->as.int_value;
  int64_t n = num->as.int_value;
  if (b < 2) return create_nil();
  if (n == 0) {
    KObj* res = create_vec(1);
    KObj* zero = create_int(0);
    vector_append(res, zero);
    release_object(zero);
    return res;
  }
  int sign = 1;
  if (n < 0) {
    sign = -1;
    n = -n;
  }
  size_t count = 0;
  int64_t tmp = n;
  while (tmp > 0) {
    tmp /= b;
    count++;
  }
  KObj* res = create_vec(count);
  int64_t* digits = (int64_t*)malloc(sizeof(int64_t) * count);
  tmp = n;
  for (size_t i = 0; i < count; i++) {
    digits[count - 1 - i] = tmp % b;
    tmp /= b;
  }
  if (sign < 0 && count > 0) {
    digits[0] *= -1;
  }
  for (size_t i = 0; i < count; i++) {
    KObj* d = create_int(digits[i]);
    vector_append(res, d);
    release_object(d);
  }
  free(digits);
  return res;
}

KObj* k_scan(KObj* func, KObj* list, KObj* init) {
  if (list->type != VECTOR) {
    printf("^type\n");
    return create_nil();
  }
  KObj* res = create_vec(list->as.vector->length);
  size_t start = 0;
  KObj* acc = NULL;
  if (init) {
    retain_object(init);
    acc = init;
  } else {
    if (list->as.vector->length == 0) return res;
    acc = &list->as.vector->items[0];
    retain_object(acc);
    vector_append(res, acc);
    start = 1;
  }
  for (size_t i = start; i < list->as.vector->length; i++) {
    KObj* item = &list->as.vector->items[i];
    KObj* next = NULL;
    if (func->type == VERB && func->as.verb.binary) {
      next = func->as.verb.binary(acc, item);
    } else if (func->type == LAMBDA) {
      next = call_binary(func, acc, item);
    } else {
      printf("^type\n");
      release_object(acc);
      release_object(res);
      return create_nil();
    }
    release_object(acc);
    acc = next;
    vector_append(res, acc);
    if (acc->type == NIL) {
      break;
    }
  }
  release_object(acc);
  return res;
}

KObj* k_split(KObj* sep, KObj* str) {
  size_t sep_len = 0;
  KObj* sep_items = NULL;
  if (sep->type == CHAR) {
    sep_len = 1;
    sep_items = sep;
  } else if (sep->type == VECTOR && is_char_vector(sep) &&
             sep->as.vector->length > 0) {
    sep_len = sep->as.vector->length;
    sep_items = sep->as.vector->items;
  } else {
    printf("^type\n");
    return create_nil();
  }
  if (str->type != VECTOR || !is_char_vector(str)) {
    printf("^type\n");
    return create_nil();
  }
  KObj* res = create_vec(4);
  KObj* part = create_vec(str->as.vector->length);
  size_t i = 0;
  while (i < str->as.vector->length) {
    bool match = false;
    if (i + sep_len <= str->as.vector->length) {
      match = true;
      for (size_t j = 0; j < sep_len; j++) {
        if (str->as.vector->items[i + j].as.char_value !=
            sep_items[j].as.char_value) {
          match = false;
          break;
        }
      }
    }
    if (match) {
      vector_append(res, part);
      release_object(part);
      part = create_vec(str->as.vector->length - i - sep_len);
      i += sep_len;
    } else {
      vector_append(part, &str->as.vector->items[i]);
      i++;
    }
  }
  vector_append(res, part);
  release_object(part);
  return res;
}
