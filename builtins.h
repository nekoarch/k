#ifndef BUILTINS_H_
#define BUILTINS_H_

#include "def.h"

KObj *k_add(KObj *left, KObj *right);
KObj *k_sub(KObj *left, KObj *right);
KObj *k_mul(KObj *left, KObj *right);
KObj *k_div(KObj *left, KObj *right);
KObj *k_max(KObj *left, KObj *right);
KObj *k_min(KObj *left, KObj *right);
KObj *k_eq(KObj *left, KObj *right);
KObj *k_match(KObj *left, KObj *right);
KObj *k_less(KObj *left, KObj *right);
KObj *k_more(KObj *left, KObj *right);
KObj *k_concat(KObj *left, KObj *right);
KObj *k_key(KObj *left, KObj *right);
KObj *k_take(KObj *left, KObj *right);
KObj *k_drop(KObj *left, KObj *right);
KObj *k_negate(KObj *value);
KObj *k_not(KObj *value);
KObj *k_where(KObj *value);
KObj *k_first(KObj *value);
KObj *k_flip(KObj *value);
KObj *k_rev(KObj *value);
KObj *k_sort(KObj *value);
KObj *k_asc(KObj *value);
KObj *k_desc(KObj *value);
KObj *k_exp(KObj *value);
KObj *k_log(KObj *value);
KObj *k_rand(KObj *value);
KObj *k_sin(KObj *value);
KObj *k_cos(KObj *value);
KObj *k_abs(KObj *value);
KObj *k_sqrt(KObj *value);
KObj *k_group(KObj *value);
KObj *k_enum(KObj *value);
KObj *k_count(KObj *value);
KObj *k_enlist(KObj *value);
KObj *k_floor(KObj *value);
KObj *k_pow(KObj *left, KObj *right);
KObj *k_logb(KObj *left, KObj *right);
KObj *k_randb(KObj *left, KObj *right);
KObj *k_over(KObj *func, KObj *list, KObj *init);
KObj *k_each(KObj *func, KObj *left, KObj *right);
KObj *k_each_n(KObj *func, KObj **args, size_t argn);
KObj *k_join(KObj *sep, KObj *list);
KObj *k_decode(KObj *base, KObj *list);
KObj *k_scan(KObj *func, KObj *list, KObj *init);
KObj *k_split(KObj *sep, KObj *str);
KObj *k_encode(KObj *base, KObj *num);

#endif
