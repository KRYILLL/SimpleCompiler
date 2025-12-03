#ifndef LICM_H
#define LICM_H

#include <stdio.h>
#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

void licm_reset(void);
int licm_run(void);

#ifdef __cplusplus
}
#endif

#endif /* LICM_H */
