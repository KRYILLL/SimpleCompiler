#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include "cse.h"
#include "optlog.h"

namespace {

bool is_tracked_symbol(SYM *sym)
{
    if(sym == nullptr) return false;
    switch(sym->type)
    {
        case SYM_INT:
        case SYM_TEXT:
        case SYM_FUNC:
        case SYM_LABEL:
            return false;
        default:
            return true;
    }
}

bool is_commutative(int op)
{
    switch(op)
    {
        case TAC_ADD:
        case TAC_MUL:
        case TAC_EQ:
        case TAC_NE:
            return true;
        default:
            return false;
    }
}

struct ExprKey {
    int op = TAC_UNDEF;
    SYM *lhs = nullptr;
    SYM *rhs = nullptr;

    bool operator==(const ExprKey &other) const noexcept
    {
        return op == other.op && lhs == other.lhs && rhs == other.rhs;
    }
};

struct ExprKeyHash {
    std::size_t operator()(const ExprKey &key) const noexcept
    {
        std::size_t h1 = reinterpret_cast<std::size_t>(key.lhs);
        std::size_t h2 = reinterpret_cast<std::size_t>(key.rhs);
        return static_cast<std::size_t>(key.op) ^ (h1 << 1) ^ (h2 << 3);
    }
};

struct ExprEntry {
    SYM *result = nullptr;
};

std::vector<std::string> *g_log = nullptr;
int g_eliminated = 0;

std::unordered_map<ExprKey, ExprEntry, ExprKeyHash> g_expr_map;
std::unordered_map<SYM*, std::unordered_set<ExprKey, ExprKeyHash>> g_sym_to_keys;
std::unordered_map<SYM*, std::unordered_set<ExprKey, ExprKeyHash>> g_result_to_keys;

void log_append(const std::string &line)
{
    if(g_log)
    {
        g_log->push_back(line);
    }
}

ExprKey make_key(TAC *t)
{
    ExprKey key;
    key.op = t->op;
    key.lhs = t->b;
    key.rhs = t->c;
    if(is_commutative(key.op) && key.lhs && key.rhs && key.lhs > key.rhs)
    {
        std::swap(key.lhs, key.rhs);
    }
    return key;
}

bool is_expression_candidate(TAC *t)
{
    if(t == nullptr) return false;
    switch(t->op)
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
        case TAC_NEG:
            return t->a != nullptr && is_tracked_symbol(t->a);
        default:
            return false;
    }
}

void remove_expr_from_symbol(SYM *sym, const ExprKey &key)
{
    if(sym == nullptr) return;
    auto it = g_sym_to_keys.find(sym);
    if(it == g_sym_to_keys.end()) return;
    it->second.erase(key);
    if(it->second.empty())
    {
        g_sym_to_keys.erase(it);
    }
}

void erase_expression(const ExprKey &key)
{
    auto it = g_expr_map.find(key);
    if(it == g_expr_map.end()) return;
    const ExprKey stored = key;
    SYM *result = it->second.result;
    remove_expr_from_symbol(stored.lhs, key);
    remove_expr_from_symbol(stored.rhs, key);
    if(result)
    {
        auto itRes = g_result_to_keys.find(result);
        if(itRes != g_result_to_keys.end())
        {
            itRes->second.erase(key);
            if(itRes->second.empty())
            {
                g_result_to_keys.erase(itRes);
            }
        }
    }
    g_expr_map.erase(it);
}

void invalidate_symbol(SYM *sym)
{
    if(sym == nullptr) return;
    std::unordered_set<ExprKey, ExprKeyHash> keys;

    auto opIt = g_sym_to_keys.find(sym);
    if(opIt != g_sym_to_keys.end())
    {
        keys.insert(opIt->second.begin(), opIt->second.end());
    }

    auto resIt = g_result_to_keys.find(sym);
    if(resIt != g_result_to_keys.end())
    {
        keys.insert(resIt->second.begin(), resIt->second.end());
    }

    for(const ExprKey &key : keys)
    {
        erase_expression(key);
    }

    g_sym_to_keys.erase(sym);
    g_result_to_keys.erase(sym);
}

void clear_all()
{
    g_expr_map.clear();
    g_sym_to_keys.clear();
    g_result_to_keys.clear();
}

const char *sym_name(SYM *sym)
{
    if(sym == nullptr) return "<null>";
    if(sym->name != nullptr) return sym->name;
    return "<temp>";
}

bool is_block_boundary_before(const TAC *t)
{
    if(t == nullptr) return false;
    switch(t->op)
    {
        case TAC_BEGINFUNC:
        case TAC_LABEL:
            return true;
        default:
            return false;
    }
}

bool is_block_boundary_after(const TAC *t)
{
    if(t == nullptr) return false;
    switch(t->op)
    {
        case TAC_GOTO:
        case TAC_IFZ:
        case TAC_RETURN:
        case TAC_ENDFUNC:
            return true;
        default:
            return false;
    }
}

bool is_global_side_effect(const TAC *t)
{
    if(t == nullptr) return false;
    switch(t->op)
    {
        case TAC_CALL:
        case TAC_INPUT:
            return true;
        default:
            return false;
    }
}

SYM *tac_def(TAC *t)
{
    if(t == nullptr) return nullptr;
    switch(t->op)
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
        case TAC_NEG:
        case TAC_COPY:
        case TAC_INPUT:
        case TAC_CALL:
        case TAC_VAR:
        case TAC_FORMAL:
            return t->a;
        default:
            return nullptr;
    }
}

void register_expression(const ExprKey &key, SYM *result)
{
    g_expr_map[key] = ExprEntry{result};
    if(key.lhs)
    {
        g_sym_to_keys[key.lhs].insert(key);
    }
    if(key.rhs)
    {
        g_sym_to_keys[key.rhs].insert(key);
    }
    if(result)
    {
        g_result_to_keys[result].insert(key);
    }
}

} // namespace

extern "C" void cse_reset(void)
{
    g_log = nullptr;
    g_eliminated = 0;
    clear_all();
}

extern "C" int cse_run(void)
{
    std::vector<std::string> run_log;
    g_log = &run_log;
    g_eliminated = 0;
    clear_all();

    for(TAC *cur = tac_first; cur != nullptr; cur = cur->next)
    {
        if(is_block_boundary_before(cur))
        {
            clear_all();
        }

        if(is_global_side_effect(cur))
        {
            clear_all();
        }

        SYM *def = tac_def(cur);
        if(def)
        {
            invalidate_symbol(def);
        }

        if(is_expression_candidate(cur))
        {
            ExprKey key = make_key(cur);
            auto it = g_expr_map.find(key);
            if(it != g_expr_map.end())
            {
                SYM *replacement = it->second.result;
                if(replacement && replacement != cur->a)
                {
                    cur->op = TAC_COPY;
                    cur->b = replacement;
                    cur->c = nullptr;
                    g_eliminated++;

                    std::ostringstream msg;
                    msg << "eliminated redundant " << sym_name(cur->a)
                        << " using " << sym_name(replacement);
                    log_append(msg.str());

                    if(def)
                    {
                        invalidate_symbol(cur->a);
                    }
                    if(is_block_boundary_after(cur) || is_global_side_effect(cur))
                    {
                        clear_all();
                    }
                    continue;
                }
            }

            register_expression(key, cur->a);
        }

        if(is_block_boundary_after(cur))
        {
            clear_all();
        }
    }

    g_log = nullptr;

    std::vector<const char*> raw;
    raw.reserve(run_log.size());
    for(const std::string &line : run_log)
    {
        raw.push_back(line.c_str());
    }
    optlog_record(OPT_PASS_CSE,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  g_eliminated);

    return g_eliminated;
}
