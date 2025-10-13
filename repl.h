#ifndef REPL_H_
#define REPL_H_

#include "def.h"

void print(KObj *obj);
int run_file(const char *filename);
void run_repl(void);

#endif
