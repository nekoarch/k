#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include "lex.h"
#include "ops.h"

void init_lexer(Lexer *lexer, const char *source) {
  lexer->source = source;
  lexer->start = source;
  lexer->current = source;
  lexer->had_whitespace = false;
}

static bool at_end(Lexer *lexer) { return *lexer->current == '\0'; }

static char advance(Lexer *lexer) { return *lexer->current++;
}

static Token make_token(Lexer *lexer, TokenType type) {
  Token t;
  t.type = type;
  t.start = lexer->start;
  t.length = (int)(lexer->current - lexer->start);
  t.ws_before = lexer->had_whitespace;
  return t;
}

static void skip_space(Lexer *lexer) {
  lexer->had_whitespace = false;
  while (true) {
    while (*lexer->current && strchr(" \r\t\n", *lexer->current)) {
      advance(lexer);
      lexer->had_whitespace = true;
    }
    if (*lexer->current == '/' &&
        (lexer->had_whitespace || lexer->current == lexer->source)) {
      while (*lexer->current && *lexer->current != '\n') advance(lexer);
      lexer->had_whitespace = true;
      continue;
    }
    break;
  }
}

static Token read_number(Lexer *lexer) {
  //  digits ('.' digits)? ([eE] ['+'|'-']? digits)? ('w'|'W')?
  //  '.' digits ([eE] ['+'|'-']? digits)? ('w'|'W')?
  bool started_with_dot = (lexer->start[0] == '.');
  if (started_with_dot) {
    while (isdigit(*lexer->current)) advance(lexer);
  } else {
    while (isdigit(*lexer->current)) advance(lexer);
    if (*lexer->current == '.') {
      advance(lexer);
      while (isdigit(*lexer->current)) advance(lexer);
    }
  }
  if (*lexer->current == 'e' || *lexer->current == 'E') {
    const char *exp_start = lexer->current;
    advance(lexer);
    if (*lexer->current == '+' || *lexer->current == '-') advance(lexer);
    if (isdigit(*lexer->current)) {
      while (isdigit(*lexer->current)) advance(lexer);
    } else {
      lexer->current = exp_start;
    }
  }
  
  if (*lexer->current == 'w' || *lexer->current == 'W') advance(lexer);
  return make_token(lexer, NUMBER);
}

static Token read_string(Lexer *lexer) {
  while (*lexer->current != '"' && !at_end(lexer)) advance(lexer);
  if (at_end(lexer)) return make_token(lexer, ERROR);
  advance(lexer);
  Token token = make_token(lexer, STRING);
  token.start++;
  token.length -= 2;
  return token;
}

static Token read_ident(Lexer *lexer) {
  while (isalnum(*lexer->current)) advance(lexer);
  Token token = make_token(lexer, IDENT);
  const OpInfo *info = find_op_by_ident(token.start, token.length);
  if (info) token.type = info->type;
  return token;
}

Token scan_token(Lexer* lexer) {
  skip_space(lexer);
  lexer->start = lexer->current;
  if (at_end(lexer)) return make_token(lexer, KEOF);
  char c = advance(lexer);
  if (isdigit(c)) return read_number(lexer);
  if (c == '.' && isdigit(*lexer->current)) return read_number(lexer);
  if (c == '"') return read_string(lexer);
  if (isalpha(c)) return read_ident(lexer);
  const OpInfo *op = find_op_by_char(c);
  if (op) return make_token(lexer, op->type);
  switch (c) {
  case '(': return make_token(lexer, LPAREN);
  case ')': return make_token(lexer, RPAREN);
  case '[': return make_token(lexer, LBRACKET);
  case ']': return make_token(lexer, RBRACKET);
  case '{': return make_token(lexer, LBRACE);
  case '}': return make_token(lexer, RBRACE);
  case ';':
    lexer->had_whitespace = true;
    return make_token(lexer, SEMICOLON);
  }
  return make_token(lexer, ERROR);
}
