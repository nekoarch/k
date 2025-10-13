#ifndef PARSER_H_
#define PARSER_H_
#include "lex.h"
#include "ast.h"

typedef struct {
  Lexer *lexer;
  Token current;
  Token previous;
} Parser;

void init_parser(Parser *parser, Lexer *lexer);
ASTNode *parse(Parser *parser);

#endif
