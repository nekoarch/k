#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "parser.h"
#include "def.h"
#include "eval.h"
#include "arena.h"
#include "ops.h"

static ASTNode *parse_expression(Parser *parser);
static ASTNode *parse_unary(Parser *parser);
static ASTNode *parse_postfix(Parser *parser);
static ASTNode *parse_lambda(Parser *parser);
static ASTNode *parse_list(Parser *parser);
static void advance(Parser *parser);

static bool is_unary_op(TokenType type) {
  return type == MINUS || type == STAR || type == PLUS ||
         type == AMP || type == PERCENT || type == BAR ||
         type == TILDE || type == CARET || type == BANG ||
         type == HASH || type == UNDERSCORE || type == EQUAL ||
         type == LESS || type == MORE || type == COMMA;
}

static bool unary_op_allowed(Parser *parser) {
  if (!is_unary_op(parser->current.type)) return false;
  Lexer backup_lexer = *parser->lexer;
  Token backup_current = parser->current;
  Token backup_previous = parser->previous;
  advance(parser);
  bool allowed = !((parser->current.type == SLASH ||
                    parser->current.type == BACKSLASH ||
                    parser->current.type == TICK) &&
                   !parser->lexer->had_whitespace);
  *parser->lexer = backup_lexer;
  parser->current = backup_current;
  parser->previous = backup_previous;
  return allowed;
}

static KObj *token_to_number(Token token) {
  if (token.length >= 2 &&
      (token.start[token.length - 1] == 'w' || token.start[token.length - 1] == 'W')) {
    if (token.start[0] == '-') return create_ninf();
    return create_pinf();
  }
  bool is_float = false;
  for (int i = 0; i < token.length; i++) {
    if (token.start[i] == '.') {
      is_float = true;
      break;
    }
  }
  if (is_float) {
    double val = strtod(token.start, NULL);
    return create_float(val);
  }
  int64_t val = strtoll(token.start, NULL, 10);
  return create_int(val);
}

static KObj *token_to_string(Token token) {
  KObj *vec = create_vec(token.length);
  for (int i = 0; i < token.length; i++) {
    KObj *ch = create_char(token.start[i]);
    vector_append(vec, ch);
    release_object(ch);
  }
  return vec;
}

static KObj *token_to_symbol(Token token) {
  char *name = (char *)arena_alloc(&global_arena, token.length + 1);
  memcpy(name, token.start, token.length);
  name[token.length] = '\0';
  KObj *sym = create_symbol(name);
  return sym;
}

static KObj *token_to_atom(Token token) {
  switch (token.type) {
  case NUMBER: return token_to_number(token);
  case STRING: return token_to_string(token);
  case IDENT:  return token_to_symbol(token);
  default: return NULL;
  }
}

static KObj *token_to_verb(Token token) {
  const OpDesc *d = get_op_desc(token.type);
  if (!d || (!d->unary && !d->binary)) return NULL;
  return create_verb(d->unary, d->binary, token);
}

static void advance(Parser *parser) {
  parser->previous = parser->current;
  parser->current = scan_token(parser->lexer);
}

static bool is_value_token(TokenType type) {
  return type == NUMBER || type == IDENT || type == STRING ||
         type == RPAREN || type == RBRACKET || type == RBRACE ||
         type == SIN || type == COS || type == ABS;
}

static bool is_expr_start(TokenType type) {
  return type == NUMBER || type == IDENT || type == STRING || type == SIN ||
         type == COS || type == ABS || type == DOLLAR || type == LPAREN ||
         type == LBRACKET || type == LBRACE;
}

static bool is_prefix_context(Parser *parser) {
  TokenType prev = parser->previous.type;
  if (prev == KEOF) return true;
  switch (prev) {
  case PLUS: case MINUS: case STAR: case PERCENT: case AMP:
  case BAR: case TILDE: case CARET: case BANG: case HASH: case UNDERSCORE: case EQUAL: case LESS: case MORE: case COMMA: case LPAREN: case LBRACKET:
  case LBRACE:
  case COLON: case SEMICOLON:
  case SIN:
  case COS:
  case ABS:
  case SLASH:
  case BACKSLASH:
  case TICK:
    return true;
  default:
    break;
  }
  if (parser->lexer->had_whitespace && is_value_token(prev)) {
    Lexer backup = *parser->lexer;
    Token next = scan_token(parser->lexer);
    bool result = (next.type == NUMBER && !parser->lexer->had_whitespace);
    *parser->lexer = backup;
    return result;
  }
  return false;
}

static bool consume_negative(Parser *parser, Token *out) {
  if (parser->current.type != MINUS) return false;
  if (!is_prefix_context(parser)) return false;

  Lexer backup_lexer = *parser->lexer;
  Token backup_current = parser->current;
  Token backup_previous = parser->previous;

  advance(parser); // consume '-'
  if (parser->current.type == NUMBER && !parser->lexer->had_whitespace) {
    Token number = parser->current;
    Token combined;
    combined.type = NUMBER;
    combined.start = backup_current.start;
    combined.length = (int)(number.start + number.length - backup_current.start);
    advance(parser); // consume number
    *out = combined;
    return true;
  }

  *parser->lexer = backup_lexer;
  parser->current = backup_current;
  parser->previous = backup_previous;
  return false;
}

static bool read_atom(Parser *parser, Token *out) {
  if (parser->current.type == NUMBER || parser->current.type == STRING ||
      parser->current.type == IDENT) {
    *out = parser->current;
    advance(parser);
    return true;
  }
  return consume_negative(parser, out);
}

static bool peek_negative(Parser *parser) {
  if (parser->current.type != MINUS) return false;
  if (!is_prefix_context(parser)) return false;
  Lexer backup_lexer = *parser->lexer;
  Token backup_current = parser->current;
  Token backup_previous = parser->previous;
  advance(parser);
  bool result = (parser->current.type == NUMBER && !parser->lexer->had_whitespace);
  *parser->lexer = backup_lexer;
  parser->current = backup_current;
  parser->previous = backup_previous;
  return result;
}

static bool peek_enumerate(Parser *parser) {
  if (parser->current.type != BANG) return false;
  if (!is_prefix_context(parser)) return false;
  Lexer backup_lexer = *parser->lexer;
  Token backup_current = parser->current;
  Token backup_previous = parser->previous;
  advance(parser);
  bool result = is_expr_start(parser->current.type);
  *parser->lexer = backup_lexer;
  parser->current = backup_current;
  parser->previous = backup_previous;
  return result;
}

static ASTNode *parse_list(Parser *parser) {
  advance(parser); // (
  ASTNode **items = NULL;
  size_t count = 0, capacity = 4;
  items = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * capacity);
  while (parser->current.type != RPAREN && parser->current.type != KEOF) {
    ASTNode *elem = parse_expression(parser);
    if (!elem) {
      if (items) {
        for (size_t i = 0; i < count; i++) free_ast(items[i]);
      }
      return NULL;
    }
    if (count >= capacity) {
      capacity *= 2;
      ASTNode **new_items = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * capacity);
      memcpy(new_items, items, count * sizeof(ASTNode *));
      items = new_items;
    }
    items[count++] = elem;
    if (parser->current.type == SEMICOLON) {
      advance(parser);
    }
  }
  if (parser->current.type != RPAREN) {
    printf("^error: ) \n");
    if (items) {
      for (size_t i = 0; i < count; i++) free_ast(items[i]);
    }
    return NULL;
  }
  advance(parser);
  return create_list_node(items, count);
}

static ASTNode *parse_lambda(Parser *parser) {
  advance(parser); // {
  char **params = NULL;
  int param_capacity = 4;
  int param_count = 0;
  ASTNode **body = NULL;
  size_t body_capacity = 4;
  size_t body_count = 0;
  bool last_semicolon = false;
  if (parser->current.type == LBRACKET) {
    advance(parser); // [
    params = (char **)arena_alloc(&global_arena, sizeof(char *) * param_capacity);
    while (parser->current.type != RBRACKET && parser->current.type != KEOF) {
      if (parser->current.type != IDENT) {
        printf("^param\n");
        goto error;
      }
      Token t = parser->current;
      char *name = (char *)arena_alloc(&global_arena, t.length + 1);
      memcpy(name, t.start, t.length);
      name[t.length] = '\0';
      if (param_count >= param_capacity) {
        param_capacity *= 2;
        char **new_params = (char **)arena_alloc(&global_arena, sizeof(char *) * param_capacity);
        memcpy(new_params, params, param_count * sizeof(char *));
        params = new_params;
      }
      params[param_count++] = name;
      advance(parser);
      if (parser->current.type == SEMICOLON) {
        advance(parser);
      } else if (parser->current.type != RBRACKET) {
        printf("^error: ] \n");
        goto error;
      }
    }
    if (parser->current.type != RBRACKET) {
      printf("^error: ] \n");
      goto error;
    }
    advance(parser); // ]
    parser->previous.type = LBRACE;
  }

  body = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * body_capacity);
  while (parser->current.type != RBRACE && parser->current.type != KEOF) {
    ASTNode *expr = parse_expression(parser);
    if (!expr) goto error;
    if (body_count >= body_capacity) {
      body_capacity *= 2;
      ASTNode **new_body = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * body_capacity);
      memcpy(new_body, body, body_count * sizeof(ASTNode *));
      body = new_body;
    }
    body[body_count++] = expr;
    last_semicolon = false;
    if (parser->current.type == SEMICOLON) {
      advance(parser);
      last_semicolon = true;
    }
  }
  if (parser->current.type != RBRACE) {
    printf("^error: } \n");
    goto error;
  }
  advance(parser); // }
  KObj *lambda_obj = create_lambda(param_count, params, body, body_count, !last_semicolon);
  return create_literal_node(lambda_obj);

error:
  if (body) {
    for (size_t i = 0; i < body_count; i++) free_ast(body[i]);
  }
  return NULL;
}

static ASTNode *parse_primary(Parser *parser) {
  Token tok;
  if (read_atom(parser, &tok)) {
    KObj *first_val = token_to_atom(tok);
    if (tok.type != IDENT) {
      Token next_tok;
      if (read_atom(parser, &next_tok)) {
        KObj *vec = create_vec(8);
        vector_append(vec, first_val);
        release_object(first_val);
        KObj *val = token_to_atom(next_tok);
        vector_append(vec, val);
        release_object(val);
        while (read_atom(parser, &next_tok)) {
          val = token_to_atom(next_tok);
          vector_append(vec, val);
          release_object(val);
        }
        return create_literal_node(vec);
      }
    }
    return create_literal_node(first_val);
  }
  if (parser->current.type == SIN || parser->current.type == COS || parser->current.type == ABS) {
    Token tok = parser->current;
    advance(parser);
    KObj *verb = token_to_verb(tok);
    ASTNode *node = create_literal_node(verb);
    release_object(verb);
    return node;
  }
  if (parser->current.type == DOLLAR) {
    ASTNode *node = create_unary_node(parser->current, NULL);
    advance(parser);
    return node;
  }
  if (parser->current.type == LPAREN) {
    Lexer backup_lexer = *parser->lexer;
    Token backup_current = parser->current;
    Token backup_previous = parser->previous;

    advance(parser); // (
    ASTNode *expr = parse_expression(parser);
    if (expr && parser->current.type == RPAREN) {
      advance(parser); // )
      return expr;
    }
    if (expr) {
      free_ast(expr);
    }
    *parser->lexer = backup_lexer;
    parser->current = backup_current;
    parser->previous = backup_previous;

    ASTNode *list = parse_list(parser);
    if (!list) return NULL;
    return list;
  }
  if (parser->current.type == LBRACE) {
    return parse_lambda(parser);
  }
  return NULL;
}

static ASTNode *parse_postfix(Parser *parser) {
  ASTNode *node = NULL;
  if (is_unary_op(parser->current.type)) {
    Lexer backup_lexer = *parser->lexer;
    Token backup_current = parser->current;
    advance(parser);
    bool adverb_follow = ((parser->current.type == SLASH ||
                           parser->current.type == BACKSLASH ||
                           parser->current.type == TICK) &&
                          !parser->lexer->had_whitespace);
    *parser->lexer = backup_lexer;
    parser->current = backup_current;
    if (adverb_follow) {
      Token tok = parser->current;
      advance(parser);
      KObj *verb = token_to_verb(tok);
      node = create_literal_node(verb);
      release_object(verb);
    }
  }
  if (!node) {
    node = parse_primary(parser);
    if (!node) return NULL;
  }
  while (true) {
    if ((parser->current.type == SLASH || parser->current.type == BACKSLASH ||
         parser->current.type == TICK) &&
        !parser->lexer->had_whitespace) {
      Token op = parser->current;
      advance(parser);
      node = create_adverb_node(op, node);
      continue;
    }
    if (node->type == AST_UNARY && node->as.unary.op.type == DOLLAR && parser->current.type == LBRACKET) {
      advance(parser); // consume '['

      ASTNode *condition = parse_expression(parser);
      if (!condition || parser->current.type != SEMICOLON) {
        printf("^error: $[b;x;y] condition error\n");
        if (condition) free_ast(condition);
        free_ast(node);
        return NULL;
      }
      advance(parser); // consume ';'

      ASTNode *then_branch = parse_expression(parser);
      if (!then_branch || parser->current.type != SEMICOLON) {
        printf("^error: $[b;x;y] then branch error\n");
        if (then_branch) free_ast(then_branch);
        free_ast(condition);
        free_ast(node);
        return NULL;
      }
      advance(parser); // consume ';'

      ASTNode *else_branch = parse_expression(parser);
      if (!else_branch || parser->current.type != RBRACKET) {
        printf("^error: $[b;x;y] else branch error\n");
        if (else_branch) free_ast(else_branch);
        free_ast(then_branch);
        free_ast(condition);
        free_ast(node);
        return NULL;
      }
      advance(parser); // consume ']'

      free_ast(node);
      node = create_conditional_node(condition, then_branch, else_branch);
      continue;
    }
    if (parser->current.type == LBRACKET ||
        (parser->current.type == LPAREN && !parser->lexer->had_whitespace)) {
      bool paren_call = parser->current.type == LPAREN;
      TokenType closing = parser->current.type == LBRACKET ? RBRACKET : RPAREN;
      advance(parser); // [ or (
      ASTNode **args = NULL;
      size_t arg_capacity = 4;
      size_t arg_count = 0;
      args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * arg_capacity);
      if (parser->current.type != closing) {
        while (true) {
          ASTNode *arg = parse_expression(parser);
          if (!arg) {
            free_ast(node);
            goto arg_error;
          }
          if (arg_count >= arg_capacity) {
            arg_capacity *= 2;
            ASTNode **new_args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * arg_capacity);
            memcpy(new_args, args, arg_count * sizeof(ASTNode *));
            args = new_args;
          }
          args[arg_count++] = arg;
          if (parser->current.type == SEMICOLON) {
            advance(parser);
            continue;
          }
          break;
        }
      }
      if (parser->current.type != closing) {
        printf("^error: %c \n", closing == RBRACKET ? ']' : ')');
        free_ast(node);
        goto arg_error;
      }
      advance(parser); // ] or )
      if (paren_call && arg_count > 1) {
        KObj *vec = create_vec(arg_count);
        for (size_t i = 0; i < arg_count; i++) {
          KObj *val = evaluate(args[i]);
          vector_append(vec, val);
          release_object(val);
          free_ast(args[i]);
        }
        ASTNode *lit = create_literal_node(vec);
        release_object(vec);
        ASTNode **new_args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *));
        new_args[0] = lit;
        node = create_call_node(node, new_args, 1);
      } else {
        node = create_call_node(node, args, arg_count);
      }
      continue;
arg_error:
      if (args) {
        for (size_t i = 0; i < arg_count; i++) free_ast(args[i]);
      }
      return NULL;
    }
    if (node->type == AST_ADVERB &&
        (is_expr_start(parser->current.type) || unary_op_allowed(parser) ||
         peek_negative(parser) || peek_enumerate(parser))) {
      ASTNode *arg = parse_expression(parser);
      if (!arg) {
        free_ast(node);
        return NULL;
      }
      ASTNode **args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *));
      args[0] = arg;
      node = create_call_node(node, args, 1);
      continue;
    }
    if (parser->lexer->had_whitespace &&
        (is_expr_start(parser->current.type) || peek_negative(parser) ||
         peek_enumerate(parser) ||
         (node->type == AST_LITERAL &&
          node->as.literal.value->type == VERB &&
          is_unary_op(parser->current.type)))) {
      ASTNode *arg = parse_unary(parser);
      if (!arg) {
        free_ast(node);
        return NULL;
      }
      ASTNode **args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *));
      args[0] = arg;
      node = create_call_node(node, args, 1);
      continue;
    }
    break;
  }
  return node;
}

static ASTNode *parse_unary(Parser *parser) {
  if (unary_op_allowed(parser)) {
    Lexer backup_lexer = *parser->lexer;
    Token backup_current = parser->current;
    Token backup_previous = parser->previous;
    advance(parser);
    if (parser->current.type == LBRACKET) {
      advance(parser);
      ASTNode **args = NULL;
      size_t arg_capacity = 4;
      size_t arg_count = 0;
      args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * arg_capacity);
      if (parser->current.type != RBRACKET) {
        while (true) {
          ASTNode *arg = parse_expression(parser);
          if (!arg) {
            if (args) {
              for (size_t i = 0; i < arg_count; i++) free_ast(args[i]);
            }
            return NULL;
          }
          if (arg_count >= arg_capacity) {
            arg_capacity *= 2;
            ASTNode **new_args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * arg_capacity);
            memcpy(new_args, args, arg_count * sizeof(ASTNode *));
            args = new_args;
          }
          args[arg_count++] = arg;
          if (parser->current.type == SEMICOLON) {
            advance(parser);
            continue;
          }
          break;
        }
      }
      if (parser->current.type != RBRACKET) {
        printf("^error: ] \n");
        if (args) {
          for (size_t i = 0; i < arg_count; i++) free_ast(args[i]);
        }
        return NULL;
      }
      advance(parser);
      KObj *verb = token_to_verb(backup_current);
      ASTNode *callee = create_literal_node(verb);
      release_object(verb);
      return create_call_node(callee, args, arg_count);
    }
    *parser->lexer = backup_lexer;
    parser->current = backup_current;
    parser->previous = backup_previous;
  }
  if (parser->current.type == LPAREN) {
    Lexer backup_lexer = *parser->lexer;
    Token backup_current = parser->current;
    Token backup_previous = parser->previous;
    advance(parser); // (
    int capacity = 4;
    int count = 0;
    Token *ops = (Token *)malloc(sizeof(Token) * capacity);
    while (unary_op_allowed(parser) &&
           (parser->current.type != MINUS ||
           (is_prefix_context(parser) && !peek_negative(parser)))) {
      if (count >= capacity) {
        capacity *= 2;
        ops = (Token *)realloc(ops, sizeof(Token) * capacity);
      }
      ops[count++] = parser->current;
      advance(parser);
    }
    if (count > 0 && parser->current.type == RPAREN) {
      advance(parser); // )
      ASTNode *child = parse_expression(parser);
      if (child) {
        for (int i = count - 1; i >= 0; i--) {
          child = create_unary_node(ops[i], child);
        }
        free(ops);
        return child;
      }
    }
    free(ops);
    *parser->lexer = backup_lexer;
    parser->current = backup_current;
    parser->previous = backup_previous;
  }

  int capacity = 4;
  int count = 0;
  Token *ops = (Token *)malloc(sizeof(Token) * capacity);
  while (unary_op_allowed(parser) &&
         (parser->current.type != MINUS ||
         (is_prefix_context(parser) && !peek_negative(parser)))) {
    if (count >= capacity) {
      capacity *= 2;
      ops = (Token *)realloc(ops, sizeof(Token) * capacity);
    }
    ops[count++] = parser->current;
    advance(parser);
  }
  if (count > 0 && parser->current.type == LBRACKET) {
    Token op = ops[count - 1];
    count--;
    advance(parser); // [
    ASTNode **args = NULL;
    size_t arg_capacity = 4;
    size_t arg_count = 0;
    args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * arg_capacity);
    if (parser->current.type != RBRACKET) {
      while (true) {
        ASTNode *arg = parse_expression(parser);
        if (!arg) {
          if (args) {
            for (size_t i = 0; i < arg_count; i++) free_ast(args[i]);
          }
          free(ops);
          return NULL;
        }
        if (arg_count >= arg_capacity) {
          arg_capacity *= 2;
          ASTNode **new_args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * arg_capacity);
          memcpy(new_args, args, arg_count * sizeof(ASTNode *));
          args = new_args;
        }
        args[arg_count++] = arg;
        if (parser->current.type == SEMICOLON) {
          advance(parser);
          continue;
        }
        break;
      }
    }
    if (parser->current.type != RBRACKET) {
      printf("^error: ] \n");
      if (args) {
        for (size_t i = 0; i < arg_count; i++) free_ast(args[i]);
      }
      free(ops);
      return NULL;
    }
    advance(parser); // ]
    KObj *verb = token_to_verb(op);
    ASTNode *callee = create_literal_node(verb);
    release_object(verb);
    ASTNode *child = create_call_node(callee, args, arg_count);
    for (int i = count - 1; i >= 0; i--) {
      child = create_unary_node(ops[i], child);
    }
    free(ops);
    return child;
  }
  if (count > 0) {
    bool follows_expr = is_expr_start(parser->current.type) || peek_negative(parser) || peek_enumerate(parser);
    if (!follows_expr && parser->current.type != LBRACKET) {
      ASTNode *node = NULL;
      if (count == 1) {
        KObj *verb = token_to_verb(ops[0]);
        if (verb) {
          node = create_literal_node(verb);
          release_object(verb);
          free(ops);
          return node;
        }
      }
      KObj *symx = create_symbol("x");
      ASTNode *body = create_literal_node(symx);
      release_object(symx);
      for (int i = count - 1; i >= 0; i--) {
        body = create_unary_node(ops[i], body);
      }
      ASTNode **body_arr = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *));
      body_arr[0] = body;
      KObj *lam = create_lambda(0, NULL, body_arr, 1, true);
      ASTNode *lam_node = create_literal_node(lam);
      release_object(lam);
      free(ops);
      return lam_node;
    }
    ASTNode *child = parse_expression(parser);
    if (!child) {
      free(ops);
      return NULL;
    }
    for (int i = count - 1; i >= 0; i--) {
      child = create_unary_node(ops[i], child);
    }
    free(ops);
    return child;
  }
  free(ops);
  return parse_postfix(parser);
}

static ASTNode* parse_expression(Parser* parser) {
  ASTNode* left_node = parse_unary(parser);
  if (!left_node)
    return NULL;
  // todo: split this
  if (parser->current.type == PLUS || parser->current.type == STAR ||
      (parser->current.type == MINUS && !is_prefix_context(parser)) ||
      parser->current.type == PERCENT || parser->current.type == AMP ||
      parser->current.type == COLON || parser->current.type == BAR ||
      parser->current.type == TILDE || parser->current.type == CARET ||
      parser->current.type == EQUAL || parser->current.type == BANG ||
      parser->current.type == HASH || parser->current.type == UNDERSCORE || parser->current.type == LESS ||
      parser->current.type == MORE || parser->current.type == COMMA) {
    Token op = parser->current;
    advance(parser);
    if ((parser->current.type == SLASH || parser->current.type == BACKSLASH ||
         parser->current.type == TICK) &&
        !parser->lexer->had_whitespace) {
      Token adv = parser->current;
      advance(parser);
      ASTNode* right_node = parse_expression(parser);
      if (!right_node) return NULL;
      KObj *verb = token_to_verb(op);
      ASTNode *verb_node = create_literal_node(verb);
      release_object(verb);
      ASTNode *adverb = create_adverb_node(adv, verb_node);
      ASTNode **args = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * 2);
      args[0] = left_node;
      args[1] = right_node;
      return create_call_node(adverb, args, 2);
    }
    ASTNode* right_node = parse_expression(parser);
    if (!right_node) return NULL;
    return create_binary_node(op, left_node, right_node);
  }
  return left_node;
}

void init_parser(Parser *parser, Lexer *lexer) {
  parser->lexer = lexer;
  parser->current.type = KEOF;
  parser->previous.type = KEOF;
  advance(parser);
}

ASTNode* parse(Parser *parser) {
  if (parser->current.type == KEOF) {
    return create_literal_node(create_nil());
  }
  ASTNode **items = NULL;
  size_t count = 0, cap = 4;
  items = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * cap);
  while (true) {
    ASTNode *expr = parse_expression(parser);
    if (!expr) {
      if (items) {
        for (size_t i = 0; i < count; i++) free_ast(items[i]);
      }
      return NULL;
    }
    if (count >= cap) {
      cap *= 2;
      ASTNode **new_items = (ASTNode **)arena_alloc(&global_arena, sizeof(ASTNode *) * cap);
      memcpy(new_items, items, count * sizeof(ASTNode *));
      items = new_items;
    }
    items[count++] = expr;
    if (parser->current.type == SEMICOLON) {
      advance(parser);
      if (parser->current.type == KEOF) break;
      continue;
    }
    break;
  }
  if (parser->current.type != KEOF) {
    for (size_t i = 0; i < count; i++) free_ast(items[i]);
    return NULL;
  }
  if (count == 1) return items[0];
  return create_seq_node(items, count);
}
