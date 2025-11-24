#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include "constfold.h"

namespace {
std::vector<std::string> g_log;
int g_folds_applied = 0;

void log_clear()
{
    g_log.clear();
    g_folds_applied = 0;
}

void log_append(const std::string &msg)
{
    g_log.push_back(msg);
}

bool sym_is_int(const SYM *s, int *value)
{
    if(s == NULL || s->type != SYM_INT) return false;
    if(value) *value = s->value;
    return true;
}

const char *op_to_str(int op)
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

void fold_into_copy(TAC *t, int result, const std::string &detail)
{
    if(t == NULL || t->a == NULL) return;
    SYM *k = mk_const(result);
    t->op = TAC_COPY;
    t->b = k;
    t->c = NULL;
    g_folds_applied++;

    std::ostringstream line;
    line << t->a->name << " = " << detail;
    log_append(line.str());
}

void try_fold_binary(TAC *t)
{
    int lhs = 0;
    int rhs = 0;
    if(!sym_is_int(t->b, &lhs) || !sym_is_int(t->c, &rhs)) return;

    int result = 0;
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

    std::ostringstream detail;
    detail << lhs << ' ' << op_to_str(t->op) << ' ' << rhs << " -> " << result;
    fold_into_copy(t, result, detail.str());
}

void try_fold_unary(TAC *t)
{
    int value = 0;
    if(!sym_is_int(t->b, &value)) return;

    if(t->op == TAC_NEG)
    {
        int result = -value;
        std::ostringstream detail;
        detail << "-(" << value << ") -> " << result;
        fold_into_copy(t, result, detail.str());
    }
}
}

extern "C" void constfold_run(void)
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

extern "C" void constfold_emit_report(FILE *out)
{
    if(out == NULL) return;

    out_str(out, "\n\t# constant folding pass\n");
    if(g_folds_applied == 0)
    {
        out_str(out, "\t#   no changes\n\n");
    }
    else
    {
        for(const std::string &line : g_log)
        {
            out_str(out, "\t#   %s\n", line.c_str());
        }
        out_str(out, "\t#   total folds: %d\n\n", g_folds_applied);
    }

    log_clear();
}
