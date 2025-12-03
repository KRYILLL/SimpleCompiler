#ifndef CONSTFOLD_H
#define CONSTFOLD_H

#include <stdio.h>
#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

void constfold_reset(void);
int constfold_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CONSTFOLD_H */
