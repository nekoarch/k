#include "ops.h"
#include "builtins.h"
#include <string.h>

static const OpDesc op_table[] = {
  [PLUS]        = { k_flip,      k_add },
  [MINUS]       = { k_negate,    k_sub },
  [STAR]        = { k_first,     k_mul },
  [PERCENT]     = { k_sqrt,      k_div },
  [AMP]         = { k_where,     k_min },
  [BAR]         = { k_rev,       k_max },
  [TILDE]       = { k_not,       k_match },
  [CARET]       = { k_sort,      NULL },
  [EQUAL]       = { k_group,     k_eq },
  [LESS]        = { k_asc,       k_less },
  [MORE]        = { k_desc,      k_more },
  [BANG]        = { k_enum,      k_key },
  [HASH]        = { k_count,     k_take },
  [UNDERSCORE]  = { k_floor,     k_drop },
  [COMMA]       = { k_enlist,    k_concat },
  [SIN]         = { k_sin,       NULL },
  [COS]         = { k_cos,       NULL },
  [ABS]         = { k_abs,       NULL },
};

static const OpDesc empty_desc = { NULL, NULL };

const OpDesc *get_op_desc(TokenType t) {
  if ((unsigned)t < (unsigned)(sizeof(op_table)/sizeof(op_table[0]))) {
    return &op_table[t];
  }
  return &empty_desc;
}

static const OpInfo op_infos[] = {
  { PLUS,       "+", "+",  0, ASSOC_LEFT, 1 },
  { MINUS,      "-", "-",  0, ASSOC_LEFT, 1 },
  { STAR,       "*", "*",  0, ASSOC_LEFT, 1 },
  { PERCENT,    "%", "%", 0, ASSOC_LEFT, 1 },
  { AMP,        "&", "&",  0, ASSOC_LEFT, 1 },
  { BAR,        "|", "|",  0, ASSOC_LEFT, 1 },
  { TILDE,      "~", "~",  0, ASSOC_LEFT, 1 },
  { CARET,      "^", "^",  0, ASSOC_LEFT, 1 },
  { EQUAL,      "=", "=",  0, ASSOC_LEFT, 1 },
  { LESS,       "<", "<",  0, ASSOC_LEFT, 1 },
  { MORE,       ">", ">",  0, ASSOC_LEFT, 1 },
  { BANG,       "!", "!",  0, ASSOC_LEFT, 1 },
  { HASH,       "#", "#",  0, ASSOC_LEFT, 1 },
  { UNDERSCORE, "_", "_",  0, ASSOC_LEFT, 1 },
  { COMMA,      ",", ",",  0, ASSOC_LEFT, 1 },
  { COLON,      ":", ":",  0, ASSOC_LEFT, 1 },
  { EQUAL,      "=", "=",  0, ASSOC_LEFT, 1 },
  { SLASH,      "/", "/",  0, ASSOC_LEFT, 1 },
  { BACKSLASH,  "\\", "\\",0, ASSOC_LEFT, 1 },
  { TICK,       "'", "'",  0, ASSOC_LEFT, 1 },
  { DOLLAR,     "$", "$",  0, ASSOC_LEFT, 1 },
  { SIN,        "sin", "sin", 0, ASSOC_LEFT, 1 },
  { COS,        "cos", "cos", 0, ASSOC_LEFT, 1 },
  { ABS,        "abs", "abs", 0, ASSOC_LEFT, 1 },
};

const OpInfo *get_op_info(TokenType t) {
  for (size_t i = 0; i < sizeof(op_infos)/sizeof(op_infos[0]); i++) {
    if (op_infos[i].type == t) return &op_infos[i];
  }
  return NULL;
}

const char *op_text(TokenType t) {
  const OpInfo *info = get_op_info(t);
  return info ? (info->print_text ? info->print_text : info->text) : NULL;
}

const OpInfo *find_op_by_char(char c) {
  for (size_t i = 0; i < sizeof(op_infos)/sizeof(op_infos[0]); i++) {
    const char *txt = op_infos[i].text;
    if (txt && txt[0] == c && txt[1] == '\0') return &op_infos[i];
  }
  return NULL;
}

const OpInfo *find_op_by_ident(const char *s, int len) {
  for (size_t i = 0; i < sizeof(op_infos)/sizeof(op_infos[0]); i++) {
    const char *txt = op_infos[i].text;
    if (txt && txt[0] && txt[1] && (int)strlen(txt) == len && strncmp(txt, s, (size_t)len) == 0) {
      return &op_infos[i];
    }
  }
  return NULL;
}
