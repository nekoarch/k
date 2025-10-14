#ifndef TOKEN_H_
#define TOKEN_H_

#include <stdint.h>
#include <stdbool.h>

typedef enum {
  PLUS,
  MINUS,
  STAR,
  PERCENT,
  AMP,
  BAR,
  TILDE,
  CARET,
  BANG,
  HASH,
  UNDERSCORE,
  SLASH,
  BACKSLASH,
  TICK,
  LESS,
  MORE,
  COLON,
  EQUAL,
  COMMA,
  LPAREN,
  RPAREN,
  LBRACKET,
  RBRACKET,
  LBRACE,
  RBRACE,
  SEMICOLON,
  DOLLAR,
  SIN,
  COS,
  ABS,
  NUMBER,
  IDENT,
  STRING,
  ERROR,
  KEOF
} TokenType;

typedef struct {
  TokenType type;
  const char *start;
  int length;
  bool ws_before;
} Token;

#endif
