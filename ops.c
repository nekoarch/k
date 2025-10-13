#include "ops.h"
#include "builtins.h"

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

