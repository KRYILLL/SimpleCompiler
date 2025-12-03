#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <sstream>
#include "loopreduce.h"
#include "optlog.h"
#include "tac.h"

namespace {

struct LoopInfo {
    TAC *header = nullptr;
    TAC *backedge = nullptr;
    int header_index = -1;
    int back_index = -1;
};

struct AccReduction {
    SYM *acc = nullptr;
    long long per_iter_increment = 0;
    long long total_increment = 0;
    TAC *copy_node = nullptr;
};

std::vector<std::string> *g_log = nullptr;
int g_collapses = 0;

bool log_skip(const char *loop_label, const char *reason)
{
    if(g_log)
    {
        std::ostringstream oss;
        oss << "skipped loop " << (loop_label ? loop_label : "<loop>")
            << " (" << reason << ")";
        g_log->push_back(oss.str());
    }
    return false;
}

bool is_tracked_symbol(const SYM *sym)
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

bool is_temp_symbol(const SYM *sym)
{
    if(sym == nullptr) return false;
    if(sym->name == nullptr) return false;
    return sym->name[0] == 't';
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

const char *sym_name_safe(const SYM *sym)
{
    if(sym == nullptr) return "<null>";
    if(sym->name != nullptr) return sym->name;
    return "<temp>";
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

bool get_constant_value_before(TAC *header, SYM *sym, int &value)
{
    if(sym == nullptr) return false;
    for(TAC *cur = header->prev; cur != nullptr; cur = cur->prev)
    {
        if(cur->op == TAC_BEGINFUNC)
        {
            break;
        }
        SYM *def = tac_def_symbol(cur);
        if(def != sym) continue;
        if(cur->op == TAC_COPY && cur->b != nullptr && cur->b->type == SYM_INT)
        {
            value = cur->b->value;
            return true;
        }
        return false;
    }
    return false;
}

struct ExprResult {
    int acc_coeff = 0;
    long long constant = 0;
    bool valid = false;
};

ExprResult eval_symbol(SYM *sym,
                       SYM *acc,
                       const std::unordered_map<SYM*, TAC*> &def_map,
                       std::unordered_set<SYM*> &temps_used,
                       std::unordered_set<TAC*> &nodes_used,
                       TAC *header,
                       std::unordered_map<SYM*, ExprResult> &cache,
                       std::unordered_set<SYM*> &visiting)
{
    if(sym == nullptr)
    {
        return {0, 0, false};
    }

    if(sym == acc)
    {
        return {1, 0, true};
    }

    if(sym->type == SYM_INT)
    {
        return {0, sym->value, true};
    }

    auto cache_it = cache.find(sym);
    if(cache_it != cache.end())
    {
        return cache_it->second;
    }

    if(!visiting.insert(sym).second)
    {
        return {0, 0, false};
    }

    ExprResult result{0, 0, false};

    auto def_it = def_map.find(sym);
    if(def_it != def_map.end())
    {
        TAC *def = def_it->second;
        if(def == nullptr)
        {
            visiting.erase(sym);
            return {0, 0, false};
        }

        switch(def->op)
        {
            case TAC_COPY:
            {
                ExprResult inner = eval_symbol(def->b, acc, def_map, temps_used, nodes_used, header, cache, visiting);
                if(inner.valid)
                {
                    result = inner;
                    nodes_used.insert(def);
                    if(is_temp_symbol(sym)) temps_used.insert(sym);
                }
                break;
            }
            case TAC_ADD:
            {
                ExprResult lhs = eval_symbol(def->b, acc, def_map, temps_used, nodes_used, header, cache, visiting);
                ExprResult rhs = eval_symbol(def->c, acc, def_map, temps_used, nodes_used, header, cache, visiting);
                if(lhs.valid && rhs.valid)
                {
                    result.acc_coeff = lhs.acc_coeff + rhs.acc_coeff;
                    result.constant = lhs.constant + rhs.constant;
                    result.valid = true;
                    nodes_used.insert(def);
                    if(is_temp_symbol(sym)) temps_used.insert(sym);
                }
                break;
            }
            case TAC_SUB:
            {
                ExprResult lhs = eval_symbol(def->b, acc, def_map, temps_used, nodes_used, header, cache, visiting);
                ExprResult rhs = eval_symbol(def->c, acc, def_map, temps_used, nodes_used, header, cache, visiting);
                if(lhs.valid && rhs.valid)
                {
                    result.acc_coeff = lhs.acc_coeff - rhs.acc_coeff;
                    result.constant = lhs.constant - rhs.constant;
                    result.valid = true;
                    nodes_used.insert(def);
                    if(is_temp_symbol(sym)) temps_used.insert(sym);
                }
                break;
            }
            default:
                break;
        }
    }
    else
    {
        int value = 0;
        if(get_constant_value_before(header, sym, value))
        {
            result.acc_coeff = 0;
            result.constant = value;
            result.valid = true;
        }
    }

    visiting.erase(sym);
    if(result.valid)
    {
        cache[sym] = result;
    }
    return result;
}

bool compute_trip_count(int cmp_op, int init, int limit, int step, int &trip_count, int &final_value)
{
    if(step == 0)
    {
        return false;
    }

    long long init_ll = init;
    long long limit_ll = limit;
    long long step_ll = step;
    long long trips = 0;

    switch(cmp_op)
    {
        case TAC_GT:
        {
            if(step_ll >= 0) return false;
            if(init_ll <= limit_ll) return false;
            long long diff = init_ll - limit_ll;
            long long step_abs = -step_ll;
            trips = (diff + step_abs - 1) / step_abs;
            break;
        }
        case TAC_GE:
        {
            if(step_ll >= 0) return false;
            if(init_ll < limit_ll) return false;
            long long diff = init_ll - limit_ll;
            long long step_abs = -step_ll;
            trips = diff / step_abs + 1;
            break;
        }
        case TAC_LT:
        {
            if(step_ll <= 0) return false;
            if(init_ll >= limit_ll) return false;
            long long diff = limit_ll - init_ll;
            long long step_abs = step_ll;
            trips = (diff + step_abs - 1) / step_abs;
            break;
        }
        case TAC_LE:
        {
            if(step_ll <= 0) return false;
            if(init_ll > limit_ll) return false;
            long long diff = limit_ll - init_ll;
            long long step_abs = step_ll;
            trips = diff / step_abs + 1;
            break;
        }
        default:
            return false;
    }

    if(trips <= 0) return false;

    long long final_ll = init_ll + trips * step_ll;
    if(final_ll < std::numeric_limits<int>::min() || final_ll > std::numeric_limits<int>::max()) return false;
    if(trips > std::numeric_limits<int>::max()) return false;

    trip_count = static_cast<int>(trips);
    final_value = static_cast<int>(final_ll);
    return true;
}

bool process_loop(const LoopInfo &loop)
{
    TAC *header = loop.header;
    TAC *backedge = loop.backedge;
    if(header == nullptr || backedge == nullptr)
    {
        return log_skip("<invalid>", "missing header/backedge");
    }
    const char *loop_label = (header->a && header->a->name) ? header->a->name : "<loop>";
    if(g_log)
    {
        std::ostringstream oss;
        oss << "considering loop " << loop_label;
        g_log->push_back(oss.str());
    }

    std::vector<TAC*> body;
    body.reserve(32);
    for(TAC *cur = header->next; cur != nullptr && cur != backedge; cur = cur->next)
    {
        body.push_back(cur);
    }
    if(body.empty()) return log_skip(loop_label, "empty body");

    std::unordered_map<SYM*, TAC*> def_map;
    std::unordered_map<SYM*, int> def_count;
    for(TAC *cur : body)
    {
        SYM *def = tac_def_symbol(cur);
        if(def != nullptr)
        {
            def_map[def] = cur;
            def_count[def] += 1;
        }
    }

    TAC *ifz = nullptr;
    for(TAC *cur : body)
    {
        if(cur->op == TAC_IFZ)
        {
            if(ifz != nullptr)
            {
                return log_skip(loop_label, "multiple exits");
            }
            ifz = cur;
        }
    }
    if(ifz == nullptr) return log_skip(loop_label, "missing guard");

    SYM *test_sym = ifz->b;
    TAC *cmp = nullptr;
    for(TAC *cur = ifz->prev; cur != nullptr && cur != header; cur = cur->prev)
    {
        if(tac_def_symbol(cur) == test_sym)
        {
            cmp = cur;
            break;
        }
    }
    if(cmp == nullptr) return log_skip(loop_label, "missing comparison");

    int cmp_op = cmp->op;
    if(cmp_op != TAC_GT && cmp_op != TAC_GE && cmp_op != TAC_LT && cmp_op != TAC_LE)
    {
        return log_skip(loop_label, "unsupported compare op");
    }

    SYM *ivar = nullptr;
    SYM *limit_sym = nullptr;
    if(is_tracked_symbol(cmp->b) && cmp->c != nullptr && cmp->c->type == SYM_INT)
    {
        ivar = cmp->b;
        limit_sym = cmp->c;
    }
    else if(is_tracked_symbol(cmp->c) && cmp->b != nullptr && cmp->b->type == SYM_INT)
    {
        // Mirror for forms like const < var
        if(cmp_op == TAC_LT || cmp_op == TAC_LE)
        {
            // (const < var) -> var > const
            ivar = cmp->c;
            limit_sym = cmp->b;
            cmp_op = (cmp_op == TAC_LT) ? TAC_GT : TAC_GE;
        }
        else
        {
            return log_skip(loop_label, "unsupported mirrored compare");
        }
    }
    else
    {
        return log_skip(loop_label, "compare operands not constant");
    }

    if(ivar == nullptr || limit_sym == nullptr) return log_skip(loop_label, "failed to identify induction variable");
    if(limit_sym->type != SYM_INT) return log_skip(loop_label, "non-integer loop bound");
    int limit_value = limit_sym->value;

    TAC *update_op = nullptr;
    TAC *update_copy = nullptr;
    SYM *update_temp = nullptr;
    int step = 0;

    for(size_t i = 0; i < body.size(); ++i)
    {
        TAC *cur = body[i];
        if(cur->op != TAC_ADD && cur->op != TAC_SUB) continue;
        SYM *def = tac_def_symbol(cur);
        if(def == nullptr || !is_temp_symbol(def)) continue;

        int delta = 0;
        bool matched = false;
        if(cur->b == ivar && cur->c != nullptr && cur->c->type == SYM_INT)
        {
            delta = cur->c->value;
            matched = true;
            if(cur->op == TAC_SUB) delta = -delta;
        }
        else if(cur->op == TAC_ADD && cur->c == ivar && cur->b != nullptr && cur->b->type == SYM_INT)
        {
            delta = cur->b->value;
            matched = true;
        }

        if(!matched || delta == 0) continue;

        // look for copy assigning temp back to ivar
        size_t j = i + 1;
        while(j < body.size() && body[j]->op == TAC_VAR)
        {
            ++j;
        }
        if(j >= body.size()) continue;
        TAC *next = body[j];
        if(next->op != TAC_COPY) continue;
        if(next->a != ivar || next->b != def) continue;

        update_op = cur;
        update_copy = next;
        update_temp = def;
        step = delta;
        break;
    }

    if(update_op == nullptr || update_copy == nullptr) return log_skip(loop_label, "induction update not found");
    if(step == 0) return log_skip(loop_label, "zero induction step");

    int init_value = 0;
    if(!get_constant_value_before(header, ivar, init_value))
    {
        return log_skip(loop_label, "non-constant induction init");
    }

    int trip_count = 0;
    int final_value = 0;
    if(!compute_trip_count(cmp_op, init_value, limit_value, step, trip_count, final_value))
    {
        return log_skip(loop_label, "trip count unresolved");
    }
    if(trip_count <= 0) return log_skip(loop_label, "non-positive trip count");

    std::vector<AccReduction> reductions;
    reductions.reserve(4);

    std::unordered_set<TAC*> nodes_to_remove;
    std::unordered_set<SYM*> temps_to_remove;

    for(TAC *cur : body)
    {
        if(cur->op == TAC_COPY)
        {
            SYM *dest = cur->a;
            if(dest == nullptr) continue;
            if(dest == ivar) continue;
            if(!is_tracked_symbol(dest)) continue;
            if(def_count[dest] != 1) continue;

            std::unordered_set<SYM*> temps_used;
            std::unordered_set<TAC*> expr_nodes;
            std::unordered_map<SYM*, ExprResult> cache;
            std::unordered_set<SYM*> visiting;
            ExprResult expr = eval_symbol(cur->b, dest, def_map, temps_used, expr_nodes, header, cache, visiting);
            if(!expr.valid) continue;
            if(expr.acc_coeff != 1) continue;
            long long per_iter = expr.constant;
            if(per_iter == 0) continue;

            AccReduction red;
            red.acc = dest;
            red.per_iter_increment = per_iter;
            red.copy_node = cur;
            reductions.push_back(red);

            nodes_to_remove.insert(cur);
            nodes_to_remove.insert(expr_nodes.begin(), expr_nodes.end());
            temps_to_remove.insert(temps_used.begin(), temps_used.end());
        }
        else if(cur->op == TAC_ADD)
        {
            SYM *dest = cur->a;
            if(dest == nullptr) continue;
            if(dest == ivar) continue;
            if(!is_tracked_symbol(dest)) continue;
            if(def_count[dest] != 1) continue;
            if(dest != cur->b && dest != cur->c) continue;

            std::unordered_set<SYM*> temps_used;
            std::unordered_set<TAC*> expr_nodes;
            std::unordered_map<SYM*, ExprResult> cache;
            std::unordered_set<SYM*> visiting;

            SYM *other = (cur->b == dest) ? cur->c : cur->b;
            ExprResult expr = eval_symbol(other, dest, def_map, temps_used, expr_nodes, header, cache, visiting);
            if(!expr.valid) continue;
            if(expr.acc_coeff != 0) continue;
            long long per_iter = expr.constant;
            if(per_iter == 0) continue;

            AccReduction red;
            red.acc = dest;
            red.per_iter_increment = per_iter;
            red.copy_node = cur;
            reductions.push_back(red);

            nodes_to_remove.insert(cur);
            nodes_to_remove.insert(expr_nodes.begin(), expr_nodes.end());
            temps_to_remove.insert(temps_used.begin(), temps_used.end());
        }
    }

    for(TAC *cur : body)
    {
        if(cur->op == TAC_IFZ && cur != ifz)
        {
            std::unordered_set<SYM*> temps_used;
            std::unordered_set<TAC*> expr_nodes_unused;
            std::unordered_map<SYM*, ExprResult> cache;
            std::unordered_set<SYM*> visiting;
            ExprResult expr = eval_symbol(cur->b, nullptr, def_map, temps_used, expr_nodes_unused, header, cache, visiting);
            if(expr.valid && expr.acc_coeff == 0 && expr.constant != 0)
            {
                nodes_to_remove.insert(cur);
            }
        }
    }

    if(reductions.empty())
    {
        if(g_log)
        {
            std::ostringstream oss;
            oss << "skipped loop " << loop_label << " (no reducible accumulators)";
            g_log->push_back(oss.str());
        }
        return false;
    }

    // include update nodes for removal
    nodes_to_remove.insert(update_op);
    nodes_to_remove.insert(update_copy);
    if(update_temp) temps_to_remove.insert(update_temp);

    // ensure body contains only expected instructions
    for(TAC *cur : body)
    {
        if(nodes_to_remove.count(cur)) continue;
        if(cur == cmp || cur == ifz) continue;
        if(cur->op == TAC_LABEL) continue;
        if(cur->op == TAC_VAR && cur->a == ifz->b) continue;
        if(cur->op == TAC_VAR && temps_to_remove.count(cur->a))
        {
            nodes_to_remove.insert(cur);
            continue;
        }
        if(cur->op == TAC_VAR)
        {
            // var for other temps not touched: abort
            return log_skip(loop_label, "var decl for unknown temp");
        }
        return log_skip(loop_label, "unsupported statement inside loop");
    }

    // add TAC_VAR declarations explicitly
    for(TAC *cur : body)
    {
        if(cur->op == TAC_VAR && temps_to_remove.count(cur->a))
        {
            nodes_to_remove.insert(cur);
        }
    }

    // remove backedge later
    nodes_to_remove.insert(backedge);

    // determine insertion point (first removable node after ifz)
    TAC *insert_pos = nullptr;
    for(TAC *cur : body)
    {
        if(nodes_to_remove.count(cur))
        {
            insert_pos = cur;
            break;
        }
    }
    if(insert_pos == nullptr)
    {
        insert_pos = backedge;
    }

    // build new operations
    for(AccReduction &red : reductions)
    {
        long long total = red.per_iter_increment * static_cast<long long>(trip_count);
        if(total < std::numeric_limits<int>::min() || total > std::numeric_limits<int>::max())
        {
            return log_skip(loop_label, "total increment overflow");
        }
        red.total_increment = total;

        SYM *const_sym = mk_const(static_cast<int>(total));
        TAC *add = mk_tac(TAC_ADD, red.acc, red.acc, const_sym);
        insert_before(insert_pos, add);

        if(g_log)
        {
            std::ostringstream oss;
            oss << "reduced loop for " << sym_name_safe(red.acc)
                << " : per-iter " << red.per_iter_increment
                << " * iterations " << trip_count
                << " -> +" << total;
            g_log->push_back(oss.str());
        }
        ++g_collapses;
    }

    SYM *final_sym = mk_const(final_value);
    TAC *set_final = mk_tac(TAC_COPY, ivar, final_sym, nullptr);
    insert_before(insert_pos, set_final);

    // remove marked nodes
    for(TAC *node : nodes_to_remove)
    {
        detach_tac(node);
    }

    return true;
}

} // namespace

extern "C" void loopreduce_reset(void)
{
    g_log = nullptr;
    g_collapses = 0;
}

extern "C" int loopreduce_run(void)
{
    std::vector<std::string> run_log;
    g_log = &run_log;
    g_collapses = 0;

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

    optlog_record(OPT_PASS_LOOPREDUCE,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  g_collapses);

    return g_collapses;
}
