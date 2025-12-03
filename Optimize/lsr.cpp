#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <sstream>
#include "lsr.h"
#include "optlog.h"
#include "tac.h"

namespace {

struct LoopInfo {
    TAC *header = nullptr;
    TAC *backedge = nullptr;
    int header_index = -1;
    int back_index = -1;
};

struct InductionInfo {
    TAC *update = nullptr;
    TAC *final_copy = nullptr;
    int step = 0;
    bool has_init = false;
    int init_value = 0;
};

struct DerivedUse {
    TAC *node = nullptr;
    SYM *result = nullptr;
    int offset = 0;
};

struct ReductionInfo {
    TAC *mul = nullptr;
    SYM *product = nullptr;
    SYM *induction_var = nullptr;
    int factor = 0;
    InductionInfo *induction = nullptr;
    std::vector<DerivedUse> derived_uses;
    bool needs_product_state = true;
};

std::vector<std::string> *g_log = nullptr;
int g_reduced = 0;

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

bool sym_is_int(SYM *sym, int &value)
{
    if(sym == nullptr || sym->type != SYM_INT) return false;
    value = sym->value;
    return true;
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

const char *sym_name_safe(SYM *sym)
{
    if(sym == nullptr) return "<null>";
    if(sym->name != nullptr) return sym->name;
    return "<temp>";
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

void insert_after(TAC *pos, TAC *node)
{
    if(node == nullptr) return;
    if(pos == nullptr)
    {
        node->prev = nullptr;
        node->next = tac_first;
        if(tac_first) tac_first->prev = node; else tac_last = node;
        tac_first = node;
        return;
    }

    TAC *next = pos->next;
    pos->next = node;
    node->prev = pos;
    node->next = next;
    if(next) next->prev = node; else tac_last = node;
}

typedef std::unordered_map<SYM*, InductionInfo> InductionMap;

bool process_loop(const LoopInfo &loop)
{
    TAC *header = loop.header;
    TAC *backedge = loop.backedge;
    if(header == nullptr || backedge == nullptr) return false;

    std::vector<TAC*> body;
    for(TAC *cur = header->next; cur != nullptr && cur != backedge; cur = cur->next)
    {
        body.push_back(cur);
    }
    if(body.empty()) return false;

    std::unordered_map<TAC*, size_t> body_index;
    body_index.reserve(body.size());
    for(size_t i = 0; i < body.size(); ++i)
    {
        body_index[body[i]] = i;
    }

    std::unordered_map<SYM*, int> def_count;
    for(TAC *cur : body)
    {
        if(cur->op == TAC_VAR || cur->op == TAC_FORMAL) continue;
        SYM *def = tac_def_symbol(cur);
        if(def && is_tracked_symbol(def))
        {
            def_count[def] += 1;
        }
    }

    if(def_count.empty()) return false;

    struct CopyLink
    {
        TAC *copy = nullptr;
        SYM *dest = nullptr;
    };
    std::unordered_map<SYM*, CopyLink> copy_map;
    copy_map.reserve(body.size());
    for(TAC *cur : body)
    {
        if(cur->op != TAC_COPY) continue;
        SYM *dest = tac_def_symbol(cur);
        if(dest == nullptr || !is_tracked_symbol(dest)) continue;
        auto dest_count_it = def_count.find(dest);
        if(dest_count_it == def_count.end() || dest_count_it->second != 1) continue;
        SYM *src = cur->b;
        if(src == nullptr || !is_tracked_symbol(src)) continue;
        if(copy_map.find(src) == copy_map.end())
        {
            copy_map.emplace(src, CopyLink{cur, dest});
        }
    }

    std::unordered_map<SYM*, std::vector<DerivedUse>> add_use_map;
    add_use_map.reserve(body.size());
    for(TAC *cur : body)
    {
        if(cur->op != TAC_ADD && cur->op != TAC_SUB) continue;
        SYM *def = tac_def_symbol(cur);
        if(def == nullptr || !is_temp_symbol(def)) continue;
        auto def_count_it = def_count.find(def);
        if(def_count_it == def_count.end() || def_count_it->second != 1) continue;

        SYM *lhs = cur->b;
        SYM *rhs = cur->c;
        int imm = 0;
        int offset = 0;
        SYM *base = nullptr;

        if(cur->op == TAC_ADD)
        {
            if(lhs == nullptr || rhs == nullptr) continue;
            if(sym_is_int(rhs, imm))
            {
                base = lhs;
                offset = imm;
            }
            else if(sym_is_int(lhs, imm))
            {
                base = rhs;
                offset = imm;
            }
        }
        else if(cur->op == TAC_SUB)
        {
            if(lhs == nullptr || rhs == nullptr) continue;
            if(sym_is_int(rhs, imm))
            {
                base = lhs;
                offset = -imm;
            }
        }

        if(base == nullptr || !is_tracked_symbol(base)) continue;
        add_use_map[base].push_back(DerivedUse{cur, def, offset});
    }

    InductionMap induction_map;
    for(TAC *cur : body)
    {
        if(cur->op != TAC_ADD && cur->op != TAC_SUB) continue;
        SYM *def = tac_def_symbol(cur);
        if(def == nullptr) continue;

        SYM *var = nullptr;
        TAC *var_copy = nullptr;

        auto copy_it = copy_map.find(def);
        if(copy_it != copy_map.end())
        {
            const CopyLink &link = copy_it->second;
            SYM *dest = link.dest;
            if(dest && is_tracked_symbol(dest))
            {
                auto dest_count_it = def_count.find(dest);
                if(dest_count_it != def_count.end() && dest_count_it->second == 1)
                {
                    TAC *copy_tac = link.copy;
                    auto idx_def = body_index.find(cur);
                    auto idx_copy = body_index.find(copy_tac);
                    if(idx_def != body_index.end() && idx_copy != body_index.end() && idx_def->second < idx_copy->second)
                    {
                        var = dest;
                        var_copy = copy_tac;
                    }
                }
            }
        }

        if(var == nullptr && is_tracked_symbol(def))
        {
            auto count_it = def_count.find(def);
            if(count_it != def_count.end() && count_it->second == 1 && !is_temp_symbol(def))
            {
                var = def;
                var_copy = cur;
            }
        }

        if(var == nullptr || !is_tracked_symbol(var)) continue;
        if(induction_map.find(var) != induction_map.end()) continue;

        int step = 0;
        int imm = 0;
        bool matched = false;
        if(cur->op == TAC_ADD)
        {
            if(cur->b == var && sym_is_int(cur->c, imm))
            {
                step = imm;
                matched = true;
            }
            else if(cur->c == var && sym_is_int(cur->b, imm))
            {
                step = imm;
                matched = true;
            }
        }
        else if(cur->op == TAC_SUB)
        {
            if(cur->b == var && sym_is_int(cur->c, imm))
            {
                step = -imm;
                matched = true;
            }
        }

        if(!matched || step == 0) continue;

        InductionInfo info;
        info.update = cur;
        info.final_copy = var_copy ? var_copy : cur;
        info.step = step;
        induction_map.emplace(var, info);
    }

    if(induction_map.empty()) return false;

    for(auto &entry : induction_map)
    {
        SYM *var = entry.first;
        InductionInfo &info = entry.second;
        for(TAC *scan = header->prev; scan != nullptr; scan = scan->prev)
        {
            if(scan->op == TAC_BEGINFUNC) break;
            SYM *def = tac_def_symbol(scan);
            if(def == var)
            {
                if(scan->op == TAC_COPY && scan->b != nullptr && scan->b->type == SYM_INT)
                {
                    info.has_init = true;
                    info.init_value = scan->b->value;
                }
                break;
            }
        }
    }

    std::vector<ReductionInfo> reductions;
    reductions.reserve(body.size());
    for(TAC *cur : body)
    {
        if(cur->op != TAC_MUL) continue;
        SYM *prod = tac_def_symbol(cur);
        if(prod == nullptr || !is_temp_symbol(prod)) continue;

        SYM *op_b = cur->b;
        SYM *op_c = cur->c;

        InductionInfo *ind = nullptr;
        SYM *ind_var = nullptr;
        SYM *other = nullptr;

        auto it_b = induction_map.find(op_b);
        if(it_b != induction_map.end())
        {
            ind = &it_b->second;
            ind_var = op_b;
            other = op_c;
        }
        else
        {
            auto it_c = induction_map.find(op_c);
            if(it_c != induction_map.end())
            {
                ind = &it_c->second;
                ind_var = op_c;
                other = op_b;
            }
        }

        if(ind == nullptr || !ind->has_init) continue;

        int factor = 0;
        if(!sym_is_int(other, factor)) continue;
        if(factor == 0) continue;

        ReductionInfo red;
        red.mul = cur;
        red.product = prod;
        red.induction_var = ind_var;
        red.factor = factor;
        red.induction = ind;
        reductions.push_back(red);
    }

    if(reductions.empty()) return false;

    auto uses_symbol = [&](TAC *node, SYM *sym) -> bool
    {
        if(node == nullptr || sym == nullptr) return false;
        switch(node->op)
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
                return node->b == sym || node->c == sym;
            case TAC_NEG:
            case TAC_COPY:
                return node->b == sym;
            case TAC_IFZ:
                return node->b == sym;
            case TAC_ACTUAL:
            case TAC_RETURN:
            case TAC_OUTPUT:
                return node->a == sym;
            default:
                return false;
        }
    };

    for(ReductionInfo &red : reductions)
    {
        auto add_it = add_use_map.find(red.product);
        if(add_it != add_use_map.end())
        {
            red.derived_uses = add_it->second;
        }

        std::unordered_set<TAC*> planned;
        for(const DerivedUse &du : red.derived_uses)
        {
            planned.insert(du.node);
        }

        bool needs_state = false;
        for(TAC *cur : body)
        {
            if(cur == red.mul) continue;
            if(planned.find(cur) != planned.end()) continue;
            if(uses_symbol(cur, red.product))
            {
                needs_state = true;
                break;
            }
        }
        red.needs_product_state = needs_state;
    }

    bool changed = false;
    for(ReductionInfo &red : reductions)
    {
        long long base_ll = static_cast<long long>(red.induction->init_value) * static_cast<long long>(red.factor);
        long long step_ll = static_cast<long long>(red.induction->step) * static_cast<long long>(red.factor);
        int base_val = static_cast<int>(base_ll);
        int step_val = static_cast<int>(step_ll);

        SYM *base_const = mk_const(base_val);
        SYM *step_const = mk_const(step_val);

        TAC *update = nullptr;
        SYM *running = nullptr;
        TAC *anchor = red.induction->final_copy ? red.induction->final_copy : red.induction->update;
        TAC *insertion_point = anchor;

        if(red.needs_product_state)
        {
            running = mk_tmp();
            TAC *decl = mk_tac(TAC_VAR, running, nullptr, nullptr);
            TAC *init = mk_tac(TAC_COPY, running, base_const, nullptr);
            insert_before(header, decl);
            insert_after(decl, init);

            update = mk_tac(TAC_ADD, running, running, step_const);
            insert_after(anchor, update);
            insertion_point = update;

            red.mul->op = TAC_COPY;
            red.mul->b = running;
            red.mul->c = nullptr;
        }
        else
        {
            red.mul->op = TAC_COPY;
            red.mul->b = base_const;
            red.mul->c = nullptr;
        }

        if(g_log)
        {
            std::ostringstream oss;
            oss << "strength-reduced " << sym_name_safe(red.product)
                << " using " << sym_name_safe(red.induction_var)
                << " * " << red.factor;
            g_log->push_back(oss.str());
        }

        ++g_reduced;
        changed = true;

        if(!red.derived_uses.empty())
        {
            for(const DerivedUse &use : red.derived_uses)
            {
                long long derived_base_ll = base_ll + static_cast<long long>(use.offset);
                int derived_base = static_cast<int>(derived_base_ll);
                SYM *derived = mk_tmp();
                TAC *decl2 = mk_tac(TAC_VAR, derived, nullptr, nullptr);
                TAC *init2 = mk_tac(TAC_COPY, derived, mk_const(derived_base), nullptr);
                insert_before(header, decl2);
                insert_after(decl2, init2);

                TAC *update2 = mk_tac(TAC_ADD, derived, derived, step_const);
                insert_after(insertion_point, update2);
                insertion_point = update2;

                use.node->op = TAC_COPY;
                use.node->b = derived;
                use.node->c = nullptr;

                if(g_log)
                {
                    std::ostringstream oss;
                    oss << "derived strength-reduced " << sym_name_safe(use.result)
                        << " from " << sym_name_safe(red.product)
                        << " + " << use.offset;
                    g_log->push_back(oss.str());
                }

                ++g_reduced;
            }
        }
    }

    return changed;
}

} // namespace

extern "C" void lsr_reset(void)
{
    g_log = nullptr;
    g_reduced = 0;
}

extern "C" int lsr_run(void)
{
    std::vector<std::string> run_log;
    g_log = &run_log;
    g_reduced = 0;

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
                current_func = -1;
            }

            if(cur->op == TAC_LABEL && cur->a)
            {
                label_index[cur->a] = static_cast<int>(sequence.size() - 1);
            }
        }

        if(sequence.empty()) break;

        std::vector<LoopInfo> loops;
        std::unordered_set<unsigned long long> seen;
        loops.reserve(sequence.size());

        for(size_t i = 0; i < sequence.size(); ++i)
        {
            TAC *t = sequence[i];
            if(t->op != TAC_GOTO) continue;
            SYM *target = t->a;
            if(target == nullptr) continue;
            auto it = label_index.find(target);
            if(it == label_index.end()) continue;
            int target_idx = it->second;
            if(target_idx >= static_cast<int>(i)) continue;
            if(func_id[i] < 0 || func_id[i] != func_id[target_idx]) continue;

            TAC *header = sequence[target_idx];
            if(header == nullptr || header->op != TAC_LABEL) continue;

            unsigned long long key = (static_cast<unsigned long long>(target_idx) << 32) | static_cast<unsigned long long>(i);
            if(seen.find(key) != seen.end()) continue;
            seen.insert(key);

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
            if(process_loop(loop))
            {
                iteration_changed = true;
            }
        }

        if(!iteration_changed)
        {
            break;
        }
    }

    g_log = nullptr;

    std::vector<const char*> raw;
    raw.reserve(run_log.size());
    for(const std::string &line : run_log)
    {
        raw.push_back(line.c_str());
    }

    optlog_record(OPT_PASS_LSR,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  g_reduced);

    return g_reduced;
}
