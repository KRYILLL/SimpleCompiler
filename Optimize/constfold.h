#ifndef CONSTFOLD_H
#define CONSTFOLD_H

#include <stdio.h>
#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

void constfold_run(void);
void constfold_emit_report(FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* CONSTFOLD_H */
