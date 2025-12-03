#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include "constfold.h"
#include "optlog.h"

namespace {
std::vector<std::string> *g_current_log = nullptr;
int g_current_delta = 0;

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
    g_current_delta++;

    std::ostringstream line;
    line << t->a->name << " = " << detail;
    if(g_current_log)
    {
        g_current_log->push_back(line.str());
    }
}//将常量计算后写回

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
            if(rhs == 0) return; //避免除0
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
    }//进行常量折叠

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

void detach_tac(TAC *node)
{
    if(node == nullptr) return;
    TAC *prev = node->prev;
    TAC *next = node->next;
    if(prev) prev->next = next; else tac_first = next;
    if(next) next->prev = prev; else tac_last = prev;
    node->prev = nullptr;
    node->next = nullptr;
}

void try_fold_ifz(TAC *t)
{
    int value = 0;
    if(!sym_is_int(t->b, &value)) return;

    if(value == 0)
    {
        // ifz 0 goto L -> goto L
        t->op = TAC_GOTO;
        t->b = NULL;
        g_current_delta++;
        if(g_current_log)
        {
            std::ostringstream oss;
            oss << "constant ifz -> " << (t->a ? t->a->name : "?") << " (condition 0)";
            g_current_log->push_back(oss.str());
        }
    }
    else
    {
        // ifz 1 goto L -> remove
        if(g_current_log)
        {
            std::ostringstream oss;
            oss << "removed constant ifz -> " << (t->a ? t->a->name : "?") << " (condition " << value << ")";
            g_current_log->push_back(oss.str());
        }
        detach_tac(t);
        g_current_delta++;
    }
}
}

extern "C" void constfold_reset(void)
{
    g_current_log = nullptr;
    g_current_delta = 0;
}

extern "C" int constfold_run(void)
{
    std::vector<std::string> run_log;
    g_current_log = &run_log;
    g_current_delta = 0;

    for(TAC *cur = tac_first; cur != NULL; )
    {
        TAC *next = cur->next;
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
            case TAC_IFZ:
                try_fold_ifz(cur);
                break;
            default:
                break;
        }
        cur = next;
    }

    g_current_log = nullptr;

    std::vector<const char*> raw;
    raw.reserve(run_log.size());
    for(const std::string &line : run_log)
    {
        raw.push_back(line.c_str());
    }
    optlog_record(OPT_PASS_CONSTFOLD,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  g_current_delta);

    return g_current_delta;
}
