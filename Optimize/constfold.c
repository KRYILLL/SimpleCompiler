#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "constfold.h"

typedef struct cf_log_entry {
    char *text;
    struct cf_log_entry *next;
} CF_LOG_ENTRY;

static CF_LOG_ENTRY *log_head = NULL;
static CF_LOG_ENTRY *log_tail = NULL;
static int folds_applied = 0;//记录折叠次数

static void log_clear(void)
{
    CF_LOG_ENTRY *node = log_head;
    while(node)
    {
        CF_LOG_ENTRY *next = node->next;
        free(node->text);
        free(node);
        node = next;
    }
    log_head = NULL;
    log_tail = NULL;
    folds_applied = 0;
}

static void log_append(const char *msg)
{
    CF_LOG_ENTRY *entry = (CF_LOG_ENTRY *)malloc(sizeof(CF_LOG_ENTRY));
    if(entry == NULL) return;
    entry->text = strdup(msg);
    entry->next = NULL;
    if(entry->text == NULL)
    {
        free(entry);
        return;
    }

    if(log_tail == NULL)
    {
        log_head = entry;
        log_tail = entry;
    }
    else
    {
        log_tail->next = entry;
        log_tail = entry;
    }
}

static int sym_is_int(const SYM *s, int *value)
{
    if(s == NULL || s->type != SYM_INT) return 0;
    if(value) *value = s->value;
    return 1;
}//判断符号是否为整数常量

static const char *op_to_str(int op)
{
    switch(op)
    {
        case TAC_ADD: return "+";
        case TAC_SUB: return "-";
        case TAC_MUL: return "*";
        case TAC_DIV: return "/";
        case TAC_EQ:  return "==";
        case TAC_NE:  return "!=";
        case TAC_LT:  return "<";
        case TAC_LE:  return "<=";
        case TAC_GT:  return ">";
        case TAC_GE:  return ">=";
        default: return "?";
    }
}

static void fold_into_copy(TAC *t, int result, const char *detail)
{
    if(t == NULL || t->a == NULL) return;
    SYM *k = mk_const(result);
    t->op = TAC_COPY;
    t->b = k;
    t->c = NULL;
    folds_applied++;

    char line[128];
    snprintf(line, sizeof(line), "%s = %s", t->a->name, detail);
    log_append(line);
}

static void try_fold_binary(TAC *t)
{
    int lhs, rhs;
    if(!sym_is_int(t->b, &lhs) || !sym_is_int(t->c, &rhs)) return;

    int result;
    switch(t->op)
    {
        case TAC_ADD:
            result = lhs + rhs;
            break;
        case TAC_SUB:
            result = lhs - rhs;
            break;
        case TAC_MUL:
            result = lhs * rhs;
            break;
        case TAC_DIV:
            if(rhs == 0) return; /* avoid folding division by zero */
            result = lhs / rhs;
            break;
        case TAC_EQ:
            result = (lhs == rhs);
            break;
        case TAC_NE:
            result = (lhs != rhs);
            break;
        case TAC_LT:
            result = (lhs < rhs);
            break;
        case TAC_LE:
            result = (lhs <= rhs);
            break;
        case TAC_GT:
            result = (lhs > rhs);
            break;
        case TAC_GE:
            result = (lhs >= rhs);
            break;
        default:
            return;
    }

    char detail[96];
    snprintf(detail, sizeof(detail), "%d %s %d -> %d", lhs, op_to_str(t->op), rhs, result);
    fold_into_copy(t, result, detail);
}

static void try_fold_unary(TAC *t)
{
    int value;
    if(!sym_is_int(t->b, &value)) return;

    if(t->op == TAC_NEG)
    {
        int result = -value;
        char detail[64];
        snprintf(detail, sizeof(detail), "-(%d) -> %d", value, result);
        fold_into_copy(t, result, detail);
    }
}

void constfold_run(void)
{
    log_clear();

    for(TAC *cur = tac_first; cur != NULL; cur = cur->next)
    {
        switch(cur->op)
        {
            case TAC_ADD:
            case TAC_SUB:
            case TAC_MUL:
            case TAC_DIV:
            case TAC_EQ:
            case TAC_NE:
            case TAC_LT:
            case TAC_LE:
            case TAC_GT:
            case TAC_GE:
                try_fold_binary(cur);
                break;
            case TAC_NEG:
                try_fold_unary(cur);
                break;
            default:
                break;
        }
    }
}

void constfold_emit_report(FILE *out)
{
    if(out == NULL) return;

    out_str(out, "\n\t# constant folding pass\n");
    if(folds_applied == 0)
    {
        out_str(out, "\t#   no changes\n\n");
    }
    else
    {
        for(CF_LOG_ENTRY *node = log_head; node != NULL; node = node->next)
        {
            out_str(out, "\t#   %s\n", node->text);
        }
        out_str(out, "\t#   total folds: %d\n\n", folds_applied);
    }

    log_clear();
}
