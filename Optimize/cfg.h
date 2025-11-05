#ifndef CFG_H
#define CFG_H

#include "tac.h"

typedef struct basic_block BASIC_BLOCK;
typedef struct bb_list BB_LIST;

typedef struct basic_block{
    int id;                 /* block id within function */
    SYM *label;             /* label symbol if block starts with TAC_LABEL, else NULL */
    TAC *first;             /* first TAC in block (inclusive) */
    TAC *last;              /* last TAC in block (inclusive) */
    BB_LIST *succ;          /* successors */
    BB_LIST *pred;          /* predecessors */
    struct basic_block *next; /* next block in function */
} BASIC_BLOCK;

typedef struct bb_list {
    BASIC_BLOCK *bb;
    struct bb_list *next;
} BB_LIST;


typedef struct cfg_function {
    char *name;             /* function name */
    BASIC_BLOCK *blocks;    /* linked list of blocks */
    int block_count;        /* number of blocks */
    struct cfg_function *next; /* next function cfg */
} CFG_FUNCTION;//不做函数间的分析

typedef struct cfg_all {
    CFG_FUNCTION *funcs;    /* list of function CFGs */
    int func_count;         /* number of functions */
} CFG_ALL;

/* Build CFGs for all functions found in the global TAC list. */
CFG_ALL *cfg_build_all(void);

/* Print CFGs in a human-friendly text form to file_x (same as TAC list). */
void cfg_print_all(CFG_ALL *all);

/* Free memory of CFGs. */
void cfg_free_all(CFG_ALL *all);

#endif /* CFG_H */
