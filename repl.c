#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lex.h"
#include "parser.h"
#include "ast.h"
#include "eval.h"
#include "def.h"

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
    case VECTOR:
      return vector_to_string(obj);
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
        else putchar(' ');
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
      char *k = kobj_to_string(&keys->as.vector->items[i]);
      printf("%s|", k);
      free(k);
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
  for (size_t i = 0; i < obj->as.vector->length; i++) {
    KObj *item = &obj->as.vector->items[i];
    if (item->type == VECTOR && !is_char_vector(item)) {
      simple = 0;
    }
    if (item->type != VECTOR || !is_char_vector(item)) {
      all_strings = 0;
    }
    if (!simple && !all_strings) break;
  }
  if (simple) {
    for (size_t i = 0; i < obj->as.vector->length; i++) {
      char *s = kobj_to_string(&obj->as.vector->items[i]);
      if (all_strings) {
        printf("%s\n", s);
      } else {
        printf("%s", s);
        if (i + 1 < obj->as.vector->length) putchar(' ');
      }
      free(s);
    }
    if (!all_strings) putchar('\n');
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
    if (print_result) {
      print(result);
    }
    release_object(result);
    free_ast(ast_root);
  } else {
    printf("^parse\n");
  }
}

int run_file(const char *filename) {
  FILE *f = fopen(filename, "r");
  if (!f) {
    printf("^io\n");
    return 1;
  }
  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    char *p = trim_line(line);
    if (*p == '\0') continue;
    if (strcmp(p, "\\v") == 0) {
      env_dump();
      continue;
    }
    if (strncmp(p, "\\t", 2) == 0 && (p[2] == '\0' || p[2] == ' ')) {
      char *expr = p + 2;
      while (*expr == ' ') expr++;
      clock_t start = clock();
      execute(expr, 0);
      clock_t end = clock();
      long long ms = (long long)(end - start) * 1000 / CLOCKS_PER_SEC;
      printf("%lld\n", ms);
      continue;
    }
    execute(p, 1);
  }
  fclose(f);
  return 0;
}

void run_repl(void) {
  char line[1024];
  printf("neko/k "__DATE__"\n  ");
  while (fgets(line, sizeof(line), stdin)) {
    char *p = trim_line(line);
    if (*p == '\0') {
      printf("  ");
      continue;
    }
    if (strcmp(p, "\\\\") == 0) {
      break;
    }
    if (strcmp(p, "\\") == 0) {
      printf("man nyi\n  ");
      continue;
    }
    if (strcmp(p, "\\v") == 0) {
      env_dump();
      printf("  ");
      continue;
    }
    if (strncmp(p, "\\t", 2) == 0 && (p[2] == '\0' || p[2] == ' ')) {
      char *expr = p + 2;
      while (*expr == ' ') expr++;
      clock_t start = clock();
      execute(expr, 0);
      clock_t end = clock();
      long long ms = (long long)(end - start) * 1000 / CLOCKS_PER_SEC;
      printf("%lld\n  ", ms);
      continue;
    }
    execute(p, 1);
    printf("  ");
  }
}
