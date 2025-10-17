#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lex.h"
#include "parser.h"
#include "ast.h"
#include "eval.h"
#include "def.h"
#include "ops.h"

static char *k_strdup(const char *s) {
  size_t len = strlen(s);
  char *res = (char *)malloc(len + 1);
  if (!res) return NULL;
  memcpy(res, s, len + 1);
  return res;
}

static int is_char_vector(KObj *obj) {
  if (!obj || obj->type != VECTOR) return 0;
  for (size_t i = 0; i < obj->as.vector->length; i++) {
    if (obj->as.vector->items[i].type != CHAR) return 0;
  }
  return 1;
}

static char *kobj_to_string(KObj *obj);
static void print_inline(KObj *obj);

static const char *op_to_text(TokenType t) {
  const char *s = op_text(t);
  return s ? s : "<verb>";
}

static char *ast_to_string(ASTNode *node);

static char *concat3(const char *a, const char *b, const char *c) {
  size_t la = strlen(a), lb = strlen(b), lc = strlen(c);
  char *s = (char *)malloc(la + lb + lc + 1);
  memcpy(s, a, la);
  memcpy(s + la, b, lb);
  memcpy(s + la + lb, c, lc);
  s[la + lb + lc] = '\0';
  return s;
}

static char *ast_to_string(ASTNode *node) {
  if (!node) return k_strdup("<nil>");
  switch (node->type) {
  case AST_LITERAL: {
    KObj *v = node->as.literal.value;
    if (v->type == SYM) {
      size_t n = strlen(v->as.symbol_value);
      char *s = (char *)malloc(n + 2);
      s[0] = '`';
      memcpy(s + 1, v->as.symbol_value, n);
      s[n + 1] = '\0';
      return s;
    }
    if (v->type == INT) {
      char buf[64]; snprintf(buf, sizeof(buf), "%lld", (long long)v->as.int_value);
      return k_strdup(buf);
    }
    if (v->type == FLOAT) {
      char buf[64]; snprintf(buf, sizeof(buf), "%g", v->as.float_value);
      return k_strdup(buf);
    }
    if (v->type == VECTOR) {
      // string literal
      int is_str = 1;
      for (size_t i = 0; i < v->as.vector->length; i++) {
        if (v->as.vector->items[i].type != CHAR) { is_str = 0; break; }
      }
      if (is_str) {
        size_t n = v->as.vector->length;
        char *s = (char *)malloc(n + 3);
        s[0] = '"';
        for (size_t i = 0; i < n; i++) s[1 + i] = v->as.vector->items[i].as.char_value;
        s[1 + n] = '"'; s[2 + n] = '\0';
        return s;
      }
    }
    return k_strdup("<obj>");
  }
  case AST_VAR: {
    return k_strdup(node->as.var.name);
  }
  case AST_UNARY: {
    char *child = ast_to_string(node->as.unary.child);
    const char *op = op_to_text(node->as.unary.op.type);
    char *s = concat3(op, "", child);
    free(child);
    return s;
  }
  case AST_BINARY: {
    char *l = ast_to_string(node->as.binary.left);
    char *r = ast_to_string(node->as.binary.right);
    const char *op = op_to_text(node->as.binary.op.type);
    size_t ll = strlen(l), rl = strlen(r), ol = strlen(op);
    char *s = (char *)malloc(ll + ol + rl + 1);
    memcpy(s, l, ll);
    memcpy(s + ll, op, ol);
    memcpy(s + ll + ol, r, rl);
    s[ll + ol + rl] = '\0';
    free(l); free(r);
    return s;
  }
  case AST_SEQ: {
    // join with ';'
    size_t n = node->as.seq.count;
    if (n == 0) return k_strdup("");
    char *s = ast_to_string(node->as.seq.items[0]);
    for (size_t i = 1; i < n; i++) {
      char *next = ast_to_string(node->as.seq.items[i]);
      char *tmp = concat3(s, ";", next);
      free(s); free(next);
      s = tmp;
    }
    return s;
  }
  default:
    return k_strdup("<expr>");
  }
}

static char *vector_to_string(KObj *vec) {
  if (is_char_vector(vec)) {
    size_t len = vec->as.vector->length;
    char *res = (char *)malloc(len + 3);
    res[0] = '"';
    for (size_t i = 0; i < len; i++) {
      res[i + 1] = vec->as.vector->items[i].as.char_value;
    }
    res[len + 1] = '"';
    res[len + 2] = '\0';
    return res;
  }
  size_t n = vec->as.vector->length;
  char **parts = (char **)malloc(n * sizeof(char *));
  size_t total = 2;
  for (size_t i = 0; i < n; i++) {
    parts[i] = kobj_to_string(&vec->as.vector->items[i]);
    total += strlen(parts[i]);
    if (i + 1 < n) total++;
  }
  char *res = (char *)malloc(total + 1);
  size_t pos = 0;
  res[pos++] = '(';
  for (size_t i = 0; i < n; i++) {
    size_t l = strlen(parts[i]);
    memcpy(res + pos, parts[i], l);
    pos += l;
    if (i + 1 < n) res[pos++] = ' ';
    free(parts[i]);
  }
  res[pos++] = ')';
  res[pos] = '\0';
  free(parts);
  return res;
}

static char *kobj_to_string(KObj *obj) {
  if (!obj) return k_strdup("nil");
  char buf[64];
  switch (obj->type) {
    case NIL:
      return k_strdup("");
    case INT:
      snprintf(buf, sizeof(buf), "%lld", (long long)obj->as.int_value);
      return k_strdup(buf);
    case FLOAT:
      snprintf(buf, sizeof(buf), "%g", obj->as.float_value);
      return k_strdup(buf);
    case PINF:
      return k_strdup("+0w");
    case NINF:
      return k_strdup("-0w");
    case CHAR:
      snprintf(buf, sizeof(buf), "%c", obj->as.char_value);
      return k_strdup(buf);
    case SYM: {
      const char *name = obj->as.symbol_value;
      size_t n = strlen(name);
      char *s = (char *)malloc(n + 2);
      s[0] = '`';
      memcpy(s + 1, name, n);
      s[n + 1] = '\0';
      return s;
    }
    case VECTOR:
      return vector_to_string(obj);
    case VERB: {
      const char *op = op_to_text(obj->as.verb.op.type);
      return k_strdup(op);
    }
    case PROJ: {
      KProj *p = obj->as.proj;
      // Render underlying function
      char *fn = kobj_to_string(p->fn);
      size_t len = strlen(fn) + 2; // []
      char **parts = NULL;
      if (p->argn > 0) {
        parts = (char **)malloc(sizeof(char*) * p->argn);
        for (size_t i = 0; i < p->argn; i++) {
          parts[i] = kobj_to_string(p->args[i]);
          len += strlen(parts[i]);
          if (i + 1 < p->argn) len += 1; // ';'
        }
      }
      char *s = (char *)malloc(len + 1);
      size_t pos = 0;
      size_t lfn = strlen(fn);
      memcpy(s + pos, fn, lfn); pos += lfn;
      s[pos++] = '[';
      for (size_t i = 0; i < p->argn; i++) {
        size_t la = strlen(parts[i]);
        memcpy(s + pos, parts[i], la);
        pos += la;
        if (i + 1 < p->argn) s[pos++] = ';';
        free(parts[i]);
      }
      if (parts) free(parts);
      s[pos++] = ']';
      s[pos] = '\0';
      free(fn);
      return s;
    }
    case LAMBDA: {
      KLambda *lam = obj->as.lambda;
      // Compute total length
      size_t len = 2; // for {}
      if (lam->param_count > 0) {
        len += 2; // []
        for (int i = 0; i < lam->param_count; i++) len += strlen(lam->params[i]);
        len += (size_t)(lam->param_count > 1 ? lam->param_count - 1 : 0); // ; between params
      }
      char **bodies = NULL;
      if (lam->body_count > 0) {
        bodies = (char **)malloc(sizeof(char*) * lam->body_count);
        for (size_t i = 0; i < lam->body_count; i++) {
          bodies[i] = ast_to_string(lam->body[i]);
          len += strlen(bodies[i]);
          if (i + 1 < lam->body_count) len += 1; // ; between exprs
        }
      }
      char *s = (char *)malloc(len + 1);
      size_t pos = 0;
      s[pos++] = '{';
      if (lam->param_count > 0) {
        s[pos++] = '[';
        for (int i = 0; i < lam->param_count; i++) {
          size_t l = strlen(lam->params[i]);
          memcpy(s + pos, lam->params[i], l);
          pos += l;
          if (i + 1 < lam->param_count) s[pos++] = ';';
        }
        s[pos++] = ']';
      }
      for (size_t i = 0; i < lam->body_count; i++) {
        size_t l = strlen(bodies[i]);
        memcpy(s + pos, bodies[i], l);
        pos += l;
        if (i + 1 < lam->body_count) s[pos++] = ';';
        free(bodies[i]);
      }
      if (bodies) free(bodies);
      s[pos++] = '}';
      s[pos] = '\0';
      return s;
    }
    case DICT: {
      KObj *keys = obj->as.dict->keys;
      KObj *vals = obj->as.dict->values;
      size_t n = keys->as.vector->length;
      if (n == 0) return k_strdup("()");
      char **parts = (char **)malloc(n * sizeof(char *));
      size_t total = 2; // for parentheses
      for (size_t i = 0; i < n; i++) {
      char *k = kobj_to_string(&keys->as.vector->items[i]);
      char *v = kobj_to_string(&vals->as.vector->items[i]);
        size_t pair_len = strlen(k) + 1 + strlen(v); // k|v
        parts[i] = (char *)malloc(pair_len + 1);
        snprintf(parts[i], pair_len + 1, "%s|%s", k, v);
        total += pair_len;
        if (i + 1 < n) total++; // ;
        free(k);
        free(v);
      }
      char *res = (char *)malloc(total + 1);
      size_t pos = 0;
      res[pos++] = '(';
      for (size_t i = 0; i < n; i++) {
        size_t l = strlen(parts[i]);
        memcpy(res + pos, parts[i], l);
        pos += l;
        free(parts[i]);
        if (i + 1 < n) res[pos++] = ';';
      }
      res[pos++] = ')';
      res[pos] = '\0';
      free(parts);
      return res;
    }
    default:
      return k_strdup("<obj>");
    }
}

static void print_inline(KObj *obj) {
  if (!obj || obj->type == NIL) return;
  if (obj->type == CHAR) {
    printf("\"%c\"", obj->as.char_value);
    return;
  }
  if (obj->type == VECTOR && !is_char_vector(obj)) {
    size_t n = obj->as.vector->length;
    int need_paren = n == 0;
    int uniform = 1;
    int first_type = -1;
    for (size_t i = 0; i < n; i++) {
      KObj *item = &obj->as.vector->items[i];
      if (item->type == VECTOR && !is_char_vector(item)) {
        need_paren = 1;
      }
      if (first_type == -1) {
        first_type = item->type;
      } else if (item->type != first_type) {
        uniform = 0;
      }
    }
    if (!uniform) need_paren = 1;
    if (need_paren) putchar('(');
    for (size_t i = 0; i < n; i++) {
      print_inline(&obj->as.vector->items[i]);
      if (i + 1 < n) {
        if (need_paren) putchar(';');
        else if (uniform && first_type == SYM) {
          // No space between symbols
        } else putchar(' ');
      }
    }
    if (need_paren) putchar(')');
    return;
  }
  char *s = kobj_to_string(obj);
  printf("%s", s);
  free(s);
}

void print(KObj *obj) {
  if (!obj || obj->type == NIL) {
    return;
  }
  if (obj->type == DICT) {
    KObj *keys = obj->as.dict->keys;
    KObj *vals = obj->as.dict->values;
    size_t len = keys->as.vector->length;
    for (size_t i = 0; i < len; i++) {
      KObj *key_obj = &keys->as.vector->items[i];
      if (key_obj->type == SYM) {
        printf("%s|", key_obj->as.symbol_value);
      } else {
        char *k = kobj_to_string(key_obj);
        printf("%s|", k);
        free(k);
      }
      KObj *v = &vals->as.vector->items[i];
      if (v->type == VECTOR && !is_char_vector(v)) {
        size_t l = v->as.vector->length;
        if (l == 1) putchar(',');
        for (size_t j = 0; j < l; j++) {
          char *s = kobj_to_string(&v->as.vector->items[j]);
          printf("%s", s);
          free(s);
          if (j + 1 < l) putchar(' ');
        }
      } else {
        char *s = kobj_to_string(v);
        printf("%s", s);
        free(s);
      }
      putchar('\n');
    }
    return;
  }
  if (obj->type == VECTOR && obj->as.vector->length == 1 && !is_char_vector(obj)) {
    putchar(',');
    print_inline(&obj->as.vector->items[0]);
    putchar('\n');
    return;
  }
  if (obj->type != VECTOR || is_char_vector(obj)) {
    char *s = kobj_to_string(obj);
    printf("%s\n", s);
    free(s);
    return;
  }
  int simple = 1;
  int all_strings = 1;
  int all_syms = 1;
  for (size_t i = 0; i < obj->as.vector->length; i++) {
    KObj *item = &obj->as.vector->items[i];
    if (item->type == VECTOR && !is_char_vector(item)) {
      simple = 0;
    }
    if (item->type != VECTOR || !is_char_vector(item)) {
      all_strings = 0;
    }
    if (item->type != SYM) {
      all_syms = 0;
    }
    if (!simple && !all_strings) break;
  }
  if (simple) {
    if (all_strings) {
      int all_single_chars = 1;
      for (size_t i = 0; i < obj->as.vector->length; i++) {
        KObj *item = &obj->as.vector->items[i];
        if (item->as.vector->length != 1) { all_single_chars = 0; break; }
      }
      if (all_single_chars) {
        putchar('"');
        for (size_t i = 0; i < obj->as.vector->length; i++) {
          KObj *item = &obj->as.vector->items[i];
          putchar(item->as.vector->items[0].as.char_value);
        }
        puts("\"");
      } else {
        for (size_t i = 0; i < obj->as.vector->length; i++) {
          char *s = kobj_to_string(&obj->as.vector->items[i]);
          printf("%s\n", s);
          free(s);
        }
      }
    } else {
      for (size_t i = 0; i < obj->as.vector->length; i++) {
        char *s = kobj_to_string(&obj->as.vector->items[i]);
        printf("%s", s);
        if (!all_syms && i + 1 < obj->as.vector->length) putchar(' ');
        free(s);
      }
      putchar('\n');
    }
    return;
  }
  size_t rows = obj->as.vector->length;
  size_t *row_len = (size_t *)malloc(rows * sizeof(size_t));
  char ***cells = (char ***)malloc(rows * sizeof(char **));
  size_t max_cols = 0;
  for (size_t r = 0; r < rows; r++) {
    KObj *row_obj = &obj->as.vector->items[r];
    if (row_obj->type == VECTOR && !is_char_vector(row_obj)) {
      size_t cols = row_obj->as.vector->length;
      row_len[r] = cols;
      if (cols > max_cols) max_cols = cols;
      cells[r] = (char **)malloc(cols * sizeof(char *));
      for (size_t c = 0; c < cols; c++) {
        cells[r][c] = kobj_to_string(&row_obj->as.vector->items[c]);
      }
    } else {
      row_len[r] = 1;
      if (max_cols < 1) max_cols = 1;
      cells[r] = (char **)malloc(sizeof(char *));
      cells[r][0] = kobj_to_string(row_obj);
    }
  }
  size_t *col_w = (size_t *)calloc(max_cols, sizeof(size_t));
  for (size_t r = 0; r < rows; r++) {
    for (size_t c = 0; c < row_len[r]; c++) {
      size_t l = strlen(cells[r][c]);
      if (l > col_w[c]) col_w[c] = l;
    }
  }
  for (size_t r = 0; r < rows; r++) {
    for (size_t c = 0; c < row_len[r]; c++) {
      printf("%s", cells[r][c]);
      size_t l = strlen(cells[r][c]);
      if (c + 1 < row_len[r]) {
        size_t pad = col_w[c] > l ? col_w[c] - l + 1 : 1;
        for (size_t p = 0; p < pad; p++) putchar(' ');
      }
      free(cells[r][c]);
    }
    free(cells[r]);
    putchar('\n');
  }
  free(cells);
  free(row_len);
  free(col_w);
}

static char *trim_line(char *line) {
  char *p = line;
  while (*p == ' ' || *p == '\t') p++;
  size_t len = strlen(p);
  while (len > 0 && (p[len-1] == '\n' || p[len-1] == '\r' || p[len-1] == ' ' || p[len-1] == '\t')) {
    p[--len] = '\0';
  }
  return p;
}

static void execute(const char *p, int print_result) {
  Lexer lexer;
  init_lexer(&lexer, p);
  Parser parser;
  init_parser(&parser, &lexer);
  ASTNode *ast_root = parse(&parser);
  if (ast_root) {
    KObj *result = evaluate(ast_root);
    int suppress = 0;
    if (ast_root->type == AST_BINARY && ast_root->as.binary.op.type == COLON) {
      suppress = 1;
    }
    if (ast_root->type == AST_SEQ) {
      int all_assign = 1;
      for (size_t i = 0; i < ast_root->as.seq.count; i++) {
        ASTNode *n = ast_root->as.seq.items[i];
        if (!(n->type == AST_BINARY && n->as.binary.op.type == COLON)) {
          all_assign = 0; break;
        }
      }
      if (all_assign) suppress = 1;
    }
    if (print_result && !suppress) {
      print(result);
    }
    release_object(result);
    free_ast(ast_root);
  } else {
    printf("^parse\n");
  }
}

static long long monotonic_ns(void) {
  struct timespec ts;
#ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts);
#else
  ts.tv_sec = 0;
  ts.tv_nsec = (long)(clock() * (1000000000.0 / CLOCKS_PER_SEC));
#endif
  return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

static void time_and_print_average(const char *expr, long runs, int repl_prompt) {
  if (runs <= 0) runs = 1;
  long long total_ns = 0;
  for (long i = 0; i < runs; i++) {
    long long start = monotonic_ns();
    execute(expr, 0);
    long long end = monotonic_ns();
    total_ns += (end - start);
  }
  long long avg_ms = total_ns / runs / 1000000LL;
  if (repl_prompt)
    printf("%lld\n  ", avg_ms);
  else
    printf("%lld\n", avg_ms);
}

static void parse_timing_args(char *q, long *runs_out, char **expr_out) {
  long runs = 1;
  char *expr = q;
  if (*q >= '0' && *q <= '9') {
    char *digits_start = q;
    long tmp = 0;
    while (*q >= '0' && *q <= '9') { tmp = tmp * 10 + (*q - '0'); q++; }
    if (*q == ' ' || *q == '\t') {
      while (*q == ' ' || *q == '\t') q++;
      runs = tmp;
      expr = q;
    } else {
      expr = digits_start;
    }
  } else {
    while (*expr == ' ' || *expr == '\t') expr++;
  }
  *runs_out = runs;
  *expr_out = expr;
}

static int process_line(char *p, int interactive) {
  if (*p == '\0') { if (interactive) printf("  "); return 1; }
  if (strcmp(p, "\\\\") == 0) { return 0; }
  if (strcmp(p, "\\") == 0) {
    FILE *f = fopen("man", "r");
    if (!f) {
      printf("^io\n");
      if (interactive) printf("  ");
      return 1;
    }
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
      fputs(buf, stdout);
    }
    fclose(f);
    if (interactive) printf("  ");
    return 1;
  }
  if (strcmp(p, "\\v") == 0) { env_dump(); if (interactive) printf("  "); return 1; }
  if (strncmp(p, "\\t", 2) == 0) {
    char *q = p + 2; long runs = 1; char *expr = q; parse_timing_args(q, &runs, &expr);
    if (*expr == '\0') { if (interactive) printf("0\n  "); else printf("0\n"); return 1; }
    time_and_print_average(expr, runs, interactive);
    return 1;
  }
  execute(p, 1);
  if (interactive) printf("  ");
  return 1;
}

int run_file(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) { printf("^io\n"); return 1; }
  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    char *p = trim_line(line);
    (void)process_line(p, 0);
  }
  fclose(f);
  return 0;
}

void run_repl(void) {
  char line[1024];
  printf("neko/k "__DATE__"\n  ");
  while (fgets(line, sizeof(line), stdin)) {
    char *p = trim_line(line);
    if (!process_line(p, 1)) break;
  }
}
