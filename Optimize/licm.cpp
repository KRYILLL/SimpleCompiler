#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include "licm.h"
#include "optlog.h"

namespace {

struct LoopInfo {
    TAC *header = nullptr;
    TAC *backedge = nullptr;
    int header_index = -1;
    int back_index = -1;
};

std::vector<std::string> *g_log = nullptr;
int g_hoisted = 0;

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

bool is_temp_symbol(SYM *sym)
{
    if(sym == nullptr) return false;
    if(sym->name == nullptr) return false;
    return sym->name[0] == 't';
}

bool is_candidate_op(int op)
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

SYM *tac_def_symbol(TAC *t)
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

void collect_uses(TAC *t, std::vector<SYM*> &out)
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
            if(is_tracked_symbol(t->b)) out.push_back(t->b);
            if(is_tracked_symbol(t->c)) out.push_back(t->c);
            break;
        case TAC_NEG:
        case TAC_COPY:
            if(is_tracked_symbol(t->b)) out.push_back(t->b);
            break;
        case TAC_IFZ:
            if(is_tracked_symbol(t->b)) out.push_back(t->b);
            break;
        case TAC_ACTUAL:
        case TAC_RETURN:
        case TAC_OUTPUT:
            if(is_tracked_symbol(t->a)) out.push_back(t->a);
            break;
        default:
            break;
    }
}

const char *sym_name(SYM *sym)
{
    if(sym == nullptr) return "<null>";
    if(sym->name != nullptr) return sym->name;
    return "<temp>";
}

std::string label_name(SYM *sym)
{
    if(sym == nullptr) return std::string("<loop>");
    if(sym->name != nullptr) return std::string(sym->name);
    return std::string("<loop>");
}

void log_hoist(SYM *def, SYM *label)
{
    if(g_log == nullptr) return;
    std::ostringstream oss;
    oss << "hoisted " << sym_name(def) << " before " << label_name(label);
    g_log->push_back(oss.str());
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

void insert_before(TAC *pos, TAC *node)
{
    if(node == nullptr) return;
    if(pos == nullptr)
    {
        node->prev = tac_last;
        node->next = nullptr;
        if(tac_last) tac_last->next = node; else tac_first = node;
        tac_last = node;
        return;
    }

    TAC *prev = pos->prev;
    node->next = pos;
    node->prev = prev;
    pos->prev = node;
    if(prev) prev->next = node; else tac_first = node;
}

bool process_loop(TAC *header, TAC *backedge)
{
    if(header == nullptr || backedge == nullptr) return false;

    std::vector<TAC*> body;
    body.reserve(64);
    for(TAC *cur = header->next; cur && cur != backedge; cur = cur->next)
    {
        body.push_back(cur);
    }
    if(body.empty()) return false;

    std::unordered_map<SYM*, int> def_count;
    std::unordered_map<SYM*, TAC*> var_decl;
    for(TAC *cur : body)
    {
        if(cur->op == TAC_VAR && cur->a)
        {
            var_decl[cur->a] = cur;
        }
        SYM *def = tac_def_symbol(cur);
        if(def && is_tracked_symbol(def))
        {
            if(cur->op == TAC_VAR || cur->op == TAC_FORMAL)
            {
                continue;
            }
            def_count[def] += 1;
        }
    }

    std::unordered_set<TAC*> hoist_set;
    std::vector<TAC*> hoist_order;
    std::unordered_set<SYM*> invariant_defs;

    bool changed;
    do
    {
        changed = false;
        for(TAC *cur : body)
        {
            if(hoist_set.count(cur)) continue;
            if(!is_candidate_op(cur->op)) continue;
            SYM *def = tac_def_symbol(cur);
            if(def == nullptr || !is_tracked_symbol(def)) continue;
            if(!is_temp_symbol(def)) continue;
            auto itc = def_count.find(def);
            if(itc == def_count.end() || itc->second != 1) continue;

            std::vector<SYM*> uses;
            collect_uses(cur, uses);
            bool ok = true;
            for(SYM *use : uses)
            {
                if(!is_tracked_symbol(use)) continue;
                if(use == def) continue;
                auto dit = def_count.find(use);
                if(dit != def_count.end())
                {
                    if(invariant_defs.find(use) == invariant_defs.end())
                    {
                        ok = false;
                        break;
                    }
                }
            }
            if(!ok) continue;

            hoist_set.insert(cur);
            hoist_order.push_back(cur);
            invariant_defs.insert(def);
            changed = true;
        }
    } while(changed);

    if(hoist_order.empty()) return false;

    SYM *loop_label = (header->op == TAC_LABEL) ? header->a : nullptr;

    for(TAC *node : hoist_order)
    {
        detach_tac(node);
    }
    for(TAC *node : hoist_order)
    {
        SYM *def = tac_def_symbol(node);
        if(def)
        {
            auto declIt = var_decl.find(def);
            if(declIt != var_decl.end())
            {
                TAC *decl = declIt->second;
                detach_tac(decl);
                insert_before(header, decl);
                var_decl.erase(declIt);
            }
        }
        insert_before(header, node);
        ++g_hoisted;
        log_hoist(def, loop_label);
    }

    return true;
}

} // namespace

extern "C" void licm_reset(void)
{
    g_log = nullptr;
    g_hoisted = 0;
}

extern "C" int licm_run(void)
{
    std::vector<std::string> run_log;
    g_log = &run_log;
    g_hoisted = 0;

    bool changed_any = false;

    while(true)
    {
        std::vector<TAC*> sequence;
        std::vector<int> func_id;
        std::unordered_map<SYM*, int> label_index;
        sequence.reserve(256);
        func_id.reserve(256);

        int current_func = -1;
        int next_func_id = 0;
        for(TAC *cur = tac_first; cur != nullptr; cur = cur->next)
        {
            sequence.push_back(cur);
            func_id.push_back(current_func);

            if(cur->op == TAC_BEGINFUNC)
            {
                current_func = next_func_id++;
                func_id.back() = current_func;
            }
            else if(cur->op == TAC_ENDFUNC)
            {
                // ENDFUNC belongs to current function; keep id before resetting.
                current_func = -1;
            }

            if(cur->op == TAC_LABEL && cur->a)
            {
                label_index[cur->a] = static_cast<int>(sequence.size() - 1);
            }
        }

        if(sequence.empty()) break;

        std::vector<LoopInfo> loops;
        loops.reserve(sequence.size());
        std::unordered_set<unsigned long long> seen;

        for(size_t i = 0; i < sequence.size(); ++i)
        {
            TAC *t = sequence[i];
            if(t->op != TAC_GOTO) continue;
            SYM *target_sym = t->a;
            if(target_sym == nullptr) continue;
            auto it = label_index.find(target_sym);
            if(it == label_index.end()) continue;
            int target_idx = it->second;
            if(target_idx >= static_cast<int>(i)) continue;
            if(func_id[i] < 0 || func_id[i] != func_id[target_idx]) continue;

            TAC *header = sequence[target_idx];
            if(header == nullptr || header->op != TAC_LABEL) continue;

            unsigned long long key = (static_cast<unsigned long long>(target_idx) << 32) |
                                     static_cast<unsigned long long>(i);
            if(!seen.insert(key).second) continue;

            LoopInfo info;
            info.header = header;
            info.backedge = sequence[i];
            info.header_index = target_idx;
            info.back_index = static_cast<int>(i);
            loops.push_back(info);
        }

        if(loops.empty()) break;

        std::sort(loops.begin(), loops.end(), [](const LoopInfo &a, const LoopInfo &b) {
            if(a.header_index != b.header_index) return a.header_index > b.header_index;
            return a.back_index > b.back_index;
        });

        bool iteration_changed = false;
        for(const LoopInfo &loop : loops)
        {
            if(process_loop(loop.header, loop.backedge))
            {
                iteration_changed = true;
            }
        }

        if(!iteration_changed)
        {
            break;
        }
        changed_any = true;
    }

    g_log = nullptr;

    std::vector<const char*> raw;
    raw.reserve(run_log.size());
    for(const std::string &line : run_log)
    {
        raw.push_back(line.c_str());
    }

    optlog_record(OPT_PASS_LICM,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  g_hoisted);

    return g_hoisted;
}
