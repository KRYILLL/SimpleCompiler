#include <unordered_map>
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

std::vector<std::string> *g_log = nullptr;
int g_eliminated = 0;

struct ExpressionDef {
    int expr_id = -1;
    SYM *result = nullptr;
};

struct InstructionInfo {
    TAC *tac = nullptr;
    SYM *def = nullptr;
    int expr_id = -1;
    int expr_def_id = -1;
    bool kill_all = false;
    std::vector<int> succ;
    std::vector<int> pred;
    std::vector<int> kill_expr_ids;
    std::vector<int> kill_def_ids;
    std::vector<int> in_values;
    std::vector<int> out_values;
};

constexpr int VALUE_UNAVAILABLE = -1;
constexpr int VALUE_CONFLICT = -2; // expression seen on all preds but with conflicting definitions

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

const char *sym_name(SYM *sym)
{
    if(sym == nullptr) return "<null>";
    if(sym->name != nullptr) return sym->name;
    return "<temp>";
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

int combine_values(int lhs, int rhs)
{
    if(lhs == VALUE_UNAVAILABLE || rhs == VALUE_UNAVAILABLE)
    {
        return VALUE_UNAVAILABLE;
    }
    if(lhs == VALUE_CONFLICT || rhs == VALUE_CONFLICT)
    {
        return VALUE_CONFLICT;
    }
    if(lhs == rhs)
    {
        return lhs;
    }
    return VALUE_CONFLICT;
}

} // namespace

extern "C" void cse_reset(void)
{
    g_log = nullptr;
    g_eliminated = 0;
}

extern "C" int cse_run(void)
{
    std::vector<std::string> run_log;
    g_log = &run_log;
    g_eliminated = 0;
    std::vector<TAC*> sequence;
    for(TAC *cur = tac_first; cur != nullptr; cur = cur->next)
    {
        sequence.push_back(cur);
    }

    if(sequence.empty())
    {
        g_log = nullptr;
        optlog_record(OPT_PASS_CSE, nullptr, 0, 0);
        return 0;
    }

    std::vector<InstructionInfo> infos(sequence.size());
    std::unordered_map<SYM*, int> label_map;
    std::unordered_map<ExprKey, int, ExprKeyHash> expr_index_map;
    std::unordered_map<SYM*, std::vector<int>> exprs_by_symbol;
    std::unordered_map<SYM*, std::vector<int>> defs_by_result;
    std::vector<ExpressionDef> expr_defs;

    for(size_t i = 0; i < sequence.size(); ++i)
    {
        TAC *t = sequence[i];
        InstructionInfo &info = infos[i];
        info.tac = t;
        info.def = tac_def(t);
        info.kill_all = is_global_side_effect(t) || (t && t->op == TAC_BEGINFUNC);

        if(t->op == TAC_LABEL && t->a)
        {
            label_map[t->a] = static_cast<int>(i);
        }

        if(is_expression_candidate(t))
        {
            ExprKey key = make_key(t);
            auto it = expr_index_map.find(key);
            int expr_id;
            if(it == expr_index_map.end())
            {
                expr_id = static_cast<int>(expr_index_map.size());
                expr_index_map.emplace(key, expr_id);
                if(key.lhs)
                {
                    exprs_by_symbol[key.lhs].push_back(expr_id);
                }
                if(key.rhs)
                {
                    exprs_by_symbol[key.rhs].push_back(expr_id);
                }
            }
            else
            {
                expr_id = it->second;
            }

            info.expr_id = expr_id;
            info.expr_def_id = static_cast<int>(expr_defs.size());
            expr_defs.push_back(ExpressionDef{expr_id, t->a});
            if(t->a)
            {
                defs_by_result[t->a].push_back(info.expr_def_id);
            }
        }
    }

    const int expr_count = static_cast<int>(expr_index_map.size());
    if(expr_count == 0)
    {
        g_log = nullptr;
        optlog_record(OPT_PASS_CSE, nullptr, 0, 0);
        return 0;
    }

    auto next_index = [&](size_t idx) -> int {
        return (idx + 1 < sequence.size()) ? static_cast<int>(idx + 1) : -1;
    };

    for(size_t i = 0; i < infos.size(); ++i)
    {
        TAC *t = infos[i].tac;
        switch(t->op)
        {
            case TAC_GOTO:
            {
                int target = -1;
                if(t->a)
                {
                    auto it = label_map.find(t->a);
                    if(it != label_map.end())
                    {
                        target = it->second;
                    }
                }
                if(target >= 0) infos[i].succ.push_back(target);
                break;
            }
            case TAC_IFZ:
            {
                int target = -1;
                if(t->a)
                {
                    auto it = label_map.find(t->a);
                    if(it != label_map.end())
                    {
                        target = it->second;
                    }
                }
                if(target >= 0) infos[i].succ.push_back(target);
                int fall = next_index(i);
                if(fall >= 0) infos[i].succ.push_back(fall);
                break;
            }
            case TAC_RETURN:
            case TAC_ENDFUNC:
                break;
            default:
            {
                int fall = next_index(i);
                if(fall >= 0) infos[i].succ.push_back(fall);
                break;
            }
        }
    }

    for(size_t i = 0; i < infos.size(); ++i)
    {
        for(int succ : infos[i].succ)
        {
            if(succ >= 0 && static_cast<size_t>(succ) < infos.size())
            {
                infos[succ].pred.push_back(static_cast<int>(i));
            }
        }
    }

    for(InstructionInfo &info : infos)
    {
        if(info.def)
        {
            auto it_expr = exprs_by_symbol.find(info.def);
            if(it_expr != exprs_by_symbol.end())
            {
                info.kill_expr_ids = it_expr->second;
            }
            auto it_def = defs_by_result.find(info.def);
            if(it_def != defs_by_result.end())
            {
                info.kill_def_ids = it_def->second;
            }
        }
        info.in_values.assign(expr_count, VALUE_UNAVAILABLE);
        info.out_values.assign(expr_count, VALUE_UNAVAILABLE);
    }

    bool changed;
    do
    {
        changed = false;
        for(size_t i = 0; i < infos.size(); ++i)
        {
            InstructionInfo &info = infos[i];

            std::vector<int> new_in(expr_count, VALUE_UNAVAILABLE);
            if(!info.pred.empty())
            {
                new_in = infos[info.pred[0]].out_values;
                for(size_t p = 1; p < info.pred.size(); ++p)
                {
                    const std::vector<int> &pred_out = infos[info.pred[p]].out_values;
                    for(int expr_id = 0; expr_id < expr_count; ++expr_id)
                    {
                        new_in[expr_id] = combine_values(new_in[expr_id], pred_out[expr_id]);
                    }
                }
            }

            if(new_in != info.in_values)
            {
                info.in_values.swap(new_in);
                changed = true;
            }

            std::vector<int> new_out = info.in_values;

            if(info.kill_all)
            {
                std::fill(new_out.begin(), new_out.end(), VALUE_UNAVAILABLE);
            }
            else
            {
                for(int expr_id : info.kill_expr_ids)
                {
                    if(expr_id >= 0 && expr_id < expr_count)
                    {
                        new_out[expr_id] = VALUE_UNAVAILABLE;
                    }
                }
                for(int def_id : info.kill_def_ids)
                {
                    if(def_id >= 0 && static_cast<size_t>(def_id) < expr_defs.size())
                    {
                        int expr_id = expr_defs[def_id].expr_id;
                        if(expr_id >= 0 && expr_id < expr_count && new_out[expr_id] == def_id)
                        {
                            new_out[expr_id] = VALUE_UNAVAILABLE;
                        }
                    }
                }
            }

            if(info.expr_id >= 0 && info.expr_id < expr_count)
            {
                new_out[info.expr_id] = info.expr_def_id;
            }

            if(new_out != info.out_values)
            {
                info.out_values.swap(new_out);
                changed = true;
            }
        }
    }
    while(changed);

    for(InstructionInfo &info : infos)
    {
        if(info.expr_id < 0) continue;
        if(info.expr_id >= expr_count) continue;

        int reaching_def = info.in_values[info.expr_id];
        if(reaching_def < 0) continue;
        if(static_cast<size_t>(reaching_def) >= expr_defs.size()) continue;

        SYM *replacement = expr_defs[reaching_def].result;
        if(replacement == nullptr) continue;
        if(replacement == info.tac->a) continue;

        info.tac->op = TAC_COPY;
        info.tac->b = replacement;
        info.tac->c = nullptr;

        g_eliminated++;

        std::ostringstream msg;
        msg << "eliminated redundant " << sym_name(info.tac->a)
            << " using " << sym_name(replacement);
        log_append(msg.str());
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
