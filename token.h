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
  SLASH_COLON,
  BACKSLASH_COLON,
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
  EXP,
  RAND,
  LOG,
  SIN,
  COS,
  ABS,
  NUMBER,
  IDENT,
  STRING,
  SYMBOL,
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
