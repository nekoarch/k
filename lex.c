#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#include "lex.h"

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
  return (Token){type, lexer->start,
                 (int)(lexer->current - lexer->start)};
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
  while (isdigit(*lexer->current)) advance(lexer);
  if (*lexer->current == '.') {
    advance(lexer);
    while (isdigit(*lexer->current)) advance(lexer);
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
  if (token.length == 3) {
    if (strncmp(token.start, "sin", 3) == 0) {
      token.type = SIN;
    } else if (strncmp(token.start, "cos", 3) == 0) {
      token.type = COS;
    } else if (strncmp(token.start, "abs", 3) == 0) {
      token.type = ABS;
    }
  }
  return token;
}

Token scan_token(Lexer* lexer) {
  skip_space(lexer);
  lexer->start = lexer->current;
  if (at_end(lexer)) return make_token(lexer, KEOF);
  char c = advance(lexer);
  if (isdigit(c)) return read_number(lexer);
  if (c == '"') return read_string(lexer);
  if (isalpha(c)) return read_ident(lexer);
  if (c == '_') return make_token(lexer, UNDERSCORE);
  switch (c) {
  case '+': return make_token(lexer, PLUS);
  case '-': return make_token(lexer, MINUS);
  case '*': return make_token(lexer, STAR);
  case '/': return make_token(lexer, SLASH);
  case '\\': return make_token(lexer, BACKSLASH);
  case '%': return make_token(lexer, PERCENT);
  case '&': return make_token(lexer, AMP);
  case '|': return make_token(lexer, BAR);
  case '~': return make_token(lexer, TILDE);
  case '^': return make_token(lexer, CARET);
  case '!': return make_token(lexer, BANG);
  case '#': return make_token(lexer, HASH);
  case '$': return make_token(lexer, DOLLAR);
  case '\'': return make_token(lexer, TICK);
  case '<': return make_token(lexer, LESS);
  case '>': return make_token(lexer, MORE);
  case ':': return make_token(lexer, COLON);
  case '=': return make_token(lexer, EQUAL);
  case ',': return make_token(lexer, COMMA);
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
