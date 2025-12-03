#ifndef OPTLOG_H
#define OPTLOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    OPT_PASS_CONSTFOLD = 0,
    OPT_PASS_COPYPROP = 1,
    OPT_PASS_CSE = 2,
    OPT_PASS_LICM = 3,
    OPT_PASS_LSR = 4,
    OPT_PASS_LOOPREDUCE = 5,
    OPT_PASS_COUNT
} OPT_PASS;

void optlog_reset(void);
void optlog_record(OPT_PASS pass, const char * const *lines, int line_count, int delta);
void optlog_emit(FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* OPTLOG_H */
