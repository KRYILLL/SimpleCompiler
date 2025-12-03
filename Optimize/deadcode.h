#ifndef DEADCODE_H
#define DEADCODE_H

#include <stdio.h>
#include "tac.h"

#ifdef __cplusplus
extern "C" {
#endif

int deadcode_run(void);
void deadcode_emit_report(FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* DEADCODE_H */
