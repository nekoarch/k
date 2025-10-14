#ifndef DEF_H_
#define DEF_H_
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "token.h"
typedef enum {
  NIL,
  CHAR,   // i8
  INT,    // i64
  FLOAT,  // f64
  PINF,   // +0w
  NINF,   // -0w
  SYM,    // var
  VECTOR, // list
  DICT,   // dict
  VERB,   // +-*%
  ADVERB, // '\/
  LAMBDA  // user-defined
} KType;

typedef struct KObj KObj;
typedef struct KVec KVec;
typedef struct KDict KDict;
typedef struct ASTNode ASTNode;
typedef struct KLambda KLambda;
typedef struct KVerb KVerb;
typedef struct KAdverb KAdverb;

typedef struct KObj* (*UnaryFunc)(struct KObj*);
typedef struct KObj* (*BinaryFunc)(struct KObj*, struct KObj*);

struct KVerb {
  UnaryFunc unary;
  BinaryFunc binary;
  Token op; // for repl
};

struct KAdverb {
  Token op;
  KObj *child;
};

struct KVec {
  size_t length;
  size_t capacity;
  KObj* items;
};

struct KDict {
  KObj *keys;
  KObj *values;
};

struct KLambda {
  int param_count;
  char **params;
  ASTNode **body;
  size_t body_count;
  bool has_return;
};

struct KObj {
  KType type;
  uint32_t ref_count;
  union {
    int64_t int_value;
    double float_value;
    char char_value;
    const char *symbol_value;
    KVec *vector;
    KDict *dict;
    KLambda *lambda;
    KVerb verb;
    KAdverb *adverb;
  } as;
};

KObj *create_object(KType type);
void retain_object(KObj *obj);
void release_object(KObj *obj);

KObj *create_nil();
KObj *create_int(int64_t value);
KObj *create_char(char value);
KObj *create_float(double value);
KObj *create_pinf();
KObj *create_ninf();
KObj *create_vec(size_t capacity);
KObj *create_symbol(const char *name);
KObj *create_dict(KObj *keys, KObj *values);
KObj *create_lambda(int param_count, char **params, ASTNode **body,
                    size_t body_count, bool has_return);
KObj *create_verb(UnaryFunc unary, BinaryFunc binary, Token op);
void vector_append(KObj *vec, KObj *item);
void vector_set(KObj *vec, size_t index, KObj *src);
#endif
