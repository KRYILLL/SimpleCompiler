#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include "deadcode.h"

namespace {

struct InstructionInfo {
    TAC *tac = nullptr;
    SYM *def = nullptr;
    std::vector<SYM*> uses;
    std::vector<int> succ;
    std::unordered_set<SYM*> live_in;
    std::unordered_set<SYM*> live_out;
    bool removable = false;
};

std::vector<std::string> g_log;
int g_removed_total = 0;

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
        default: return "op";
    }
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

    int removed_this_round = 0;
    for(InstructionInfo &info : infos)
    {
        if(!is_side_effect_free(info.tac->op)) continue;
        if(info.def == nullptr) continue;
        if(info.live_out.find(info.def) != info.live_out.end()) continue;

        info.removable = true;
        removed_this_round++;

        std::ostringstream msg;
        msg << "removed dead " << tac_op_name(info.tac->op) << " targeting "
            << (info.def && info.def->name ? info.def->name : "<temp>");
        log_append(msg.str());
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

extern "C" void deadcode_run(void)
{
    log_clear();
    while(run_iteration() > 0) { /* iterate to fixpoint */ }
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
