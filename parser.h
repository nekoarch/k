#ifndef PARSER_H_
#define PARSER_H_
#include "ast.h"
#include "lex.h"

typedef struct {
  Lexer *lexer;
  Token current;
  Token previous;
} Parser;

void init_parser(Parser *parser, Lexer *lexer);
ASTNode *parse(Parser *parser);

#endif
