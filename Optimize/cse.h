#ifndef CSE_H
#define CSE_H

#include <stdio.h>
#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

void cse_reset(void);
int cse_run(void);

#ifdef __cplusplus
}
#endif

#endif /* CSE_H */
