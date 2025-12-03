#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include "deadcode.h"

namespace {

enum class RemovalReason {
    None,
    DeadDefinition,
    Unreachable
};

struct InstructionInfo {
    TAC *tac = nullptr;
    SYM *def = nullptr;
    std::vector<SYM*> uses;
    std::vector<int> succ;
    std::unordered_set<SYM*> live_in;
    std::unordered_set<SYM*> live_out;
    bool removable = false;
    RemovalReason reason = RemovalReason::None;
};

struct ConstDefCandidate {
    TAC *tac = nullptr;
    int index = -1;
    int value = 0;
};

std::vector<std::string> g_log;
int g_removed_total = 0;

using ConstEnv = std::unordered_map<SYM*, int>;

bool is_tracked(SYM *sym);

bool env_equal(const ConstEnv &a, const ConstEnv &b)
{
    if(a.size() != b.size()) return false;
    for(const auto &entry : a)
    {
        auto it = b.find(entry.first);
        if(it == b.end() || it->second != entry.second)
        {
            return false;
        }
    }
    return true;
}

bool assign_env(ConstEnv &dst, const ConstEnv &src)
{
    if(env_equal(dst, src)) return false;
    dst = src;
    return true;
}

ConstEnv merge_envs(ConstEnv lhs, const ConstEnv &rhs)
{
    for(auto it = lhs.begin(); it != lhs.end(); )
    {
        auto jt = rhs.find(it->first);
        if(jt == rhs.end() || jt->second != it->second)
        {
            it = lhs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    return lhs;
}

bool operand_constant(SYM *sym, const ConstEnv &env, int &value)
{
    if(sym == nullptr) return false;
    if(sym->type == SYM_INT)
    {
        value = sym->value;
        return true;
    }
    if(!is_tracked(sym)) return false;
    auto it = env.find(sym);
    if(it == env.end()) return false;
    value = it->second;
    return true;
}

bool evaluate_constant(TAC *t, const ConstEnv &env, int &value)
{
    if(t == nullptr) return false;
    switch(t->op)
    {
        case TAC_COPY:
            if(operand_constant(t->b, env, value)) return true;
            return false;
        case TAC_NEG:
        {
            int inner;
            if(!operand_constant(t->b, env, inner)) return false;
            value = -inner;
            return true;
        }
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
        {
            int lhs, rhs;
            if(!operand_constant(t->b, env, lhs)) return false;
            if(!operand_constant(t->c, env, rhs)) return false;
            switch(t->op)
            {
                case TAC_ADD: value = lhs + rhs; break;
                case TAC_SUB: value = lhs - rhs; break;
                case TAC_MUL: value = lhs * rhs; break;
                case TAC_DIV:
                    if(rhs == 0) return false;
                    value = lhs / rhs;
                    break;
                case TAC_EQ: value = (lhs == rhs) ? 1 : 0; break;
                case TAC_NE: value = (lhs != rhs) ? 1 : 0; break;
                case TAC_LT: value = (lhs < rhs) ? 1 : 0; break;
                case TAC_LE: value = (lhs <= rhs) ? 1 : 0; break;
                case TAC_GT: value = (lhs > rhs) ? 1 : 0; break;
                case TAC_GE: value = (lhs >= rhs) ? 1 : 0; break;
                default:
                    return false;
            }
            return true;
        }
        default:
            return false;
    }
}

void log_clear()
{
    g_log.clear();
    g_removed_total = 0;
}

void log_append(const std::string &msg)
{
    g_log.push_back(msg);
}

bool is_tracked(SYM *sym)
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

void add_use(std::vector<SYM*> &uses, SYM *sym)
{
    if(!is_tracked(sym)) return;
    uses.push_back(sym);
}

void collect_uses(TAC *t, std::vector<SYM*> &uses)
{
    if(t == nullptr) return;
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
            add_use(uses, t->b);
            add_use(uses, t->c);
            break;
        case TAC_NEG:
        case TAC_COPY:
            add_use(uses, t->b);
            break;
        case TAC_IFZ:
            add_use(uses, t->b);
            break;
        case TAC_ACTUAL:
        case TAC_RETURN:
        case TAC_OUTPUT:
            add_use(uses, t->a);
            break;
        default:
            break;
    }
}

bool is_side_effect_free(int op)
{
    switch(op)
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
            return true;
        default:
            return false;
    }
}

std::string tac_op_name(int op)
{
    switch(op)
    {
        case TAC_ADD: return "add";
        case TAC_SUB: return "sub";
        case TAC_MUL: return "mul";
        case TAC_DIV: return "div";
        case TAC_EQ:  return "eq";
        case TAC_NE:  return "ne";
        case TAC_LT:  return "lt";
        case TAC_LE:  return "le";
        case TAC_GT:  return "gt";
        case TAC_GE:  return "ge";
        case TAC_NEG: return "neg";
        case TAC_COPY: return "copy";
        case TAC_GOTO: return "goto";
        case TAC_IFZ: return "ifz";
        case TAC_BEGINFUNC: return "beginfunc";
        case TAC_ENDFUNC: return "endfunc";
        case TAC_LABEL: return "label";
        case TAC_VAR: return "var";
        case TAC_FORMAL: return "formal";
        case TAC_ACTUAL: return "actual";
        case TAC_CALL: return "call";
        case TAC_RETURN: return "return";
        case TAC_INPUT: return "input";
        case TAC_OUTPUT: return "output";
        default: return "op";
    }
}

std::string sym_repr(SYM *sym)
{
    if(sym == nullptr) return std::string("<null>");
    if(sym->type == SYM_INT)
    {
        return std::to_string(sym->value);
    }
    if(sym->name != nullptr)
    {
        return std::string(sym->name);
    }
    return std::string("<temp>");
}

bool assign_set(std::unordered_set<SYM*> &dst, const std::unordered_set<SYM*> &src)
{
    if(dst.size() == src.size())
    {
        bool identical = true;
        for(SYM *sym : src)
        {
            if(dst.find(sym) == dst.end())
            {
                identical = false;
                break;
            }
        }
        if(identical) return false;
    }
    dst = src;
    return true;
}

int label_index(SYM *label, const std::unordered_map<SYM*, int> &map)
{
    if(label == nullptr) return -1;
    auto it = map.find(label);
    return (it == map.end()) ? -1 : it->second;
}

int run_iteration()
{
    std::vector<TAC*> sequence;
    for(TAC *cur = tac_first; cur != nullptr; cur = cur->next)
    {
        sequence.push_back(cur);
    }
    if(sequence.empty()) return 0;

    std::vector<InstructionInfo> infos(sequence.size());
    std::unordered_map<SYM*, int> label_map;
    std::unordered_map<SYM*, int> real_def_count;
    std::unordered_map<SYM*, ConstDefCandidate> const_copy_defs;

    for(size_t i = 0; i < sequence.size(); ++i)
    {
        InstructionInfo &info = infos[i];
        info.tac = sequence[i];
        info.def = tac_def(info.tac);
        collect_uses(info.tac, info.uses);
        if(info.tac->op == TAC_LABEL && info.tac->a)
        {
            label_map[info.tac->a] = static_cast<int>(i);
        }

        SYM *def = info.def;
        if(def && is_tracked(def))
        {
            int op = info.tac->op;
            if(op != TAC_VAR && op != TAC_FORMAL)
            {
                real_def_count[def] += 1;
                if(op == TAC_COPY && info.tac->b && info.tac->b->type == SYM_INT)
                {
                    const_copy_defs[def] = ConstDefCandidate{info.tac, static_cast<int>(i), info.tac->b->value};
                }
                else
                {
                    const_copy_defs.erase(def);
                }
            }
        }
    }

    std::unordered_map<SYM*, ConstDefCandidate> unique_const_defs;
    for(const auto &entry : const_copy_defs)
    {
        SYM *sym = entry.first;
        auto it = real_def_count.find(sym);
        if(it != real_def_count.end() && it->second == 1)
        {
            unique_const_defs[sym] = entry.second;
        }
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
                int target = label_index(t->a, label_map);
                if(target >= 0) infos[i].succ.push_back(target);
                break;
            }
            case TAC_IFZ:
            {
                int target = label_index(t->a, label_map);
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

    std::vector<std::vector<int>> preds(infos.size());
    for(size_t i = 0; i < infos.size(); ++i)
    {
        for(int succ : infos[i].succ)
        {
            if(succ >= 0 && succ < static_cast<int>(infos.size()))
            {
                preds[succ].push_back(static_cast<int>(i));
            }
        }
    }

    std::vector<ConstEnv> const_in(infos.size());
    std::vector<ConstEnv> const_out(infos.size());
    bool const_changed;
    do
    {
        const_changed = false;
        for(size_t i = 0; i < infos.size(); ++i)
        {
            ConstEnv merged;
            const auto &pred_list = preds[i];
            if(!pred_list.empty())
            {
                merged = const_out[pred_list[0]];
                for(size_t j = 1; j < pred_list.size(); ++j)
                {
                    merged = merge_envs(merged, const_out[pred_list[j]]);
                }
            }

            if(assign_env(const_in[i], merged))
            {
                const_changed = true;
            }

            ConstEnv updated = const_in[i];
            SYM *def = infos[i].def;
            if(def && is_tracked(def))
            {
                int value;
                if(evaluate_constant(infos[i].tac, const_in[i], value))
                {
                    updated[def] = value;
                }
                else
                {
                    updated.erase(def);
                }
            }

            if(assign_env(const_out[i], updated))
            {
                const_changed = true;
            }
        }
    } while(const_changed);

    int removed_constant_ifz = 0;
    bool changed_constant_ifz = false;
    for(size_t i = 0; i < infos.size(); ++i)
    {
        TAC *t = infos[i].tac;
        if(t->op != TAC_IFZ) continue;

        int cond_value;
        bool has_const = operand_constant(t->b, const_in[i], cond_value);
        if(!has_const)
        {
            auto it = unique_const_defs.find(t->b);
            if(it == unique_const_defs.end() || it->second.index >= static_cast<int>(i))
            {
                continue;
            }
            cond_value = it->second.value;
            has_const = true;
        }

        if(!has_const)
        {
            continue;
        }

        if(cond_value == 0)
        {
            t->op = TAC_GOTO;
            t->b = nullptr;
            t->c = nullptr;
            changed_constant_ifz = true;
            std::ostringstream msg;
            msg << "folded constant ifz -> " << sym_repr(t->a);
            log_append(msg.str());
        }
        else
        {
            TAC *prev = t->prev;
            TAC *next = t->next;
            if(prev) prev->next = next; else tac_first = next;
            if(next) next->prev = prev; else tac_last = prev;
            t->prev = nullptr;
            t->next = nullptr;

            std::ostringstream msg;
            msg << "removed constant ifz -> " << sym_repr(t->a)
                << " (condition " << cond_value << ")";
            log_append(msg.str());
            ++removed_constant_ifz;
            changed_constant_ifz = true;
        }
    }

    if(changed_constant_ifz)
    {
        g_removed_total += removed_constant_ifz;
        return (removed_constant_ifz > 0) ? removed_constant_ifz : 1;
    }

    std::vector<char> reachable(infos.size(), 0);
    std::vector<int> worklist;
    auto enqueue = [&](int idx) {
        if(idx < 0 || idx >= static_cast<int>(infos.size())) return;
        if(reachable[idx]) return;
        reachable[idx] = 1;
        worklist.push_back(idx);
    };

    if(!infos.empty())
    {
        enqueue(0);
    }

    for(size_t i = 0; i < infos.size(); ++i)
    {
        if(infos[i].tac->op == TAC_BEGINFUNC)
        {
            enqueue(static_cast<int>(i));
            if(i > 0 && infos[i - 1].tac->op == TAC_LABEL)
            {
                enqueue(static_cast<int>(i - 1));
            }
        }
    }

    while(!worklist.empty())
    {
        int idx = worklist.back();
        worklist.pop_back();
        for(int succ : infos[idx].succ)
        {
            enqueue(succ);
        }
    }

    bool changed;
    do
    {
        changed = false;
        for(int i = static_cast<int>(infos.size()) - 1; i >= 0; --i)
        {
            InstructionInfo &info = infos[i];

            std::unordered_set<SYM*> new_out;
            for(int succ : info.succ)
            {
                const auto &succ_in = infos[succ].live_in;
                new_out.insert(succ_in.begin(), succ_in.end());
            }
            if(assign_set(info.live_out, new_out))
            {
                changed = true;
            }

            std::unordered_set<SYM*> new_in = info.live_out;
            if(info.def)
            {
                new_in.erase(info.def);
            }
            for(SYM *sym : info.uses)
            {
                if(is_tracked(sym))
                {
                    new_in.insert(sym);
                }
            }
            if(assign_set(info.live_in, new_in))
            {
                changed = true;
            }
        }
    } while(changed);

    for(size_t i = 0; i < infos.size(); ++i)
    {
        InstructionInfo &info = infos[i];
        if(reachable[i]) continue;
        int op = info.tac->op;
        if(op == TAC_BEGINFUNC || op == TAC_ENDFUNC)
        {
            continue;
        }
        info.removable = true;
        info.reason = RemovalReason::Unreachable;
    }

    for(InstructionInfo &info : infos)
    {
        if(info.reason != RemovalReason::None) continue;
        if(!is_side_effect_free(info.tac->op)) continue;
        if(info.def == nullptr) continue;
        if(info.live_out.find(info.def) != info.live_out.end()) continue;

        info.removable = true;
        info.reason = RemovalReason::DeadDefinition;
    }

    int removed_this_round = 0;
    for(InstructionInfo &info : infos)
    {
        if(!info.removable) continue;
        ++removed_this_round;

        switch(info.reason)
        {
            case RemovalReason::DeadDefinition:
            {
                std::ostringstream msg;
                msg << "removed dead " << tac_op_name(info.tac->op) << " targeting "
                    << sym_repr(info.def);
                log_append(msg.str());
                break;
            }
            case RemovalReason::Unreachable:
            {
                std::ostringstream msg;
                msg << "removed unreachable " << tac_op_name(info.tac->op);
                if(info.tac->op == TAC_LABEL)
                {
                    msg << " " << sym_repr(info.tac->a);
                }
                else if(info.tac->op == TAC_GOTO || info.tac->op == TAC_IFZ)
                {
                    msg << " -> " << sym_repr(info.tac->a);
                }
                else if(info.def)
                {
                    msg << " targeting " << sym_repr(info.def);
                }
                log_append(msg.str());
                break;
            }
            case RemovalReason::None:
                break;
        }
    }

    for(InstructionInfo &info : infos)
    {
        if(!info.removable) continue;
        TAC *t = info.tac;
        TAC *prev = t->prev;
        TAC *next = t->next;
        if(prev) prev->next = next; else tac_first = next;
        if(next) next->prev = prev; else tac_last = prev;
        t->prev = nullptr;
        t->next = nullptr;
    }

    g_removed_total += removed_this_round;
    return removed_this_round;
}

} // namespace

extern "C" int deadcode_run(void)
{
    log_clear();
    while(run_iteration() > 0) { /* iterate to fixpoint */ }
    return g_removed_total;
}

extern "C" void deadcode_emit_report(FILE *out)
{
    if(out == nullptr) return;

    out_str(out, "\n\t# dead assignment elimination pass\n");
    if(g_removed_total == 0)
    {
        out_str(out, "\t#   no changes\n\n");
    }
    else
    {
        for(const std::string &line : g_log)
        {
            out_str(out, "\t#   %s\n", line.c_str());
        }
        out_str(out, "\t#   total removed: %d\n\n", g_removed_total);
    }

    log_clear();
}
