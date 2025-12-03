#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <sstream>
#include "loopunroll.h"
#include "optlog.h"
#include "tac.h"

namespace {

struct LoopInfo {
    TAC *header = nullptr;
    TAC *backedge = nullptr;
    int header_index = -1;
    int back_index = -1;
};

std::vector<std::string> *g_log = nullptr;
int g_unrolls = 0;

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

bool compute_trip_count(int cmp_op, int init, int limit, int step, int &trip_count)
{
    if(step == 0) return false;

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
    if(trips > 32) return false; // Limit unrolling to small loops

    trip_count = static_cast<int>(trips);
    return true;
}

TAC *clone_tac(TAC *orig)
{
    if(!orig) return nullptr;
    TAC *copy = mk_tac(orig->op, orig->a, orig->b, orig->c);
    // Note: etc field is not deep copied, but usually not needed for these ops
    return copy;
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

void remove_tac(TAC *node)
{
    if(node == nullptr) return;
    TAC *prev = node->prev;
    TAC *next = node->next;
    if(prev) prev->next = next; else tac_first = next;
    if(next) next->prev = prev; else tac_last = prev;
    node->prev = nullptr;
    node->next = nullptr;
}

bool process_loop(const LoopInfo &loop)
{
    TAC *header = loop.header;
    TAC *backedge = loop.backedge;
    if(header == nullptr || backedge == nullptr) return false;

    const char *loop_label = (header->a && header->a->name) ? header->a->name : "<loop>";

    std::vector<TAC*> body;
    body.reserve(32);
    for(TAC *cur = header->next; cur != nullptr && cur != backedge; cur = cur->next)
    {
        body.push_back(cur);
    }
    if(body.empty()) return log_skip(loop_label, "empty body");

    TAC *ifz = nullptr;
    for(TAC *cur : body)
    {
        if(cur->op == TAC_IFZ)
        {
            if(ifz != nullptr) return log_skip(loop_label, "multiple exits");
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
        if(cmp_op == TAC_LT || cmp_op == TAC_LE)
        {
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
    int limit_value = limit_sym->value;

    TAC *update_op = nullptr;
    int step = 0;

    for(size_t i = 0; i < body.size(); ++i)
    {
        TAC *cur = body[i];
        if(cur->op != TAC_ADD && cur->op != TAC_SUB) continue;
        SYM *def = tac_def_symbol(cur);
        if(def == nullptr) continue;
        
        // Check if this instruction updates ivar
        // Pattern: ivar = ivar + const OR ivar = const + ivar
        // But wait, usually it's tX = ivar + 1; ivar = tX
        // Or tX = ivar + 1; ... ; ivar = tX
        
        // loopreduce looks for:
        // tX = ivar + const
        // ...
        // ivar = tX
        
        // Let's look for the assignment to ivar
        // It must be a COPY or an arithmetic op directly to ivar (if supported)
        // In this TAC, arithmetic ops have destination.
        
        // If cur->a == ivar, it's an update.
        // But usually ivar is updated via a temp.
        
        // Let's stick to loopreduce's logic: find the arithmetic op that feeds into ivar update.
        // But loopreduce logic was: find arithmetic op, then find copy to ivar.
        
        // Let's simplify: scan for ANY assignment to ivar.
        // If there is more than one, or it's not a simple increment, skip.
    }
    
    // Re-implement loopreduce's update detection
    TAC *update_copy = nullptr;
    SYM *update_temp = nullptr;
    
    for(TAC *cur : body)
    {
        if(cur->op == TAC_COPY && cur->a == ivar)
        {
            if(update_copy != nullptr) return log_skip(loop_label, "multiple updates to ivar");
            update_copy = cur;
            update_temp = cur->b;
        }
        else if(tac_def_symbol(cur) == ivar)
        {
             return log_skip(loop_label, "complex update to ivar");
        }
    }
    
    if(update_copy == nullptr) return log_skip(loop_label, "no update to ivar");
    
    // Find definition of update_temp
    for(TAC *cur : body)
    {
        if(cur == update_copy) continue;
        if(tac_def_symbol(cur) == update_temp)
        {
            if(update_op != nullptr) return log_skip(loop_label, "multiple defs of update temp");
            update_op = cur;
        }
    }
    
    if(update_op == nullptr) return log_skip(loop_label, "update op not found");
    
    int delta = 0;
    if(update_op->op == TAC_ADD || update_op->op == TAC_SUB)
    {
        if(update_op->b == ivar && update_op->c && update_op->c->type == SYM_INT)
        {
            delta = update_op->c->value;
            if(update_op->op == TAC_SUB) delta = -delta;
        }
        else if(update_op->op == TAC_ADD && update_op->c == ivar && update_op->b && update_op->b->type == SYM_INT)
        {
            delta = update_op->b->value;
        }
    }
    
    if(delta == 0) return log_skip(loop_label, "complex update op");
    step = delta;

    int init_value = 0;
    if(!get_constant_value_before(header, ivar, init_value))
    {
        return log_skip(loop_label, "non-constant induction init");
    }

    int trip_count = 0;
    if(!compute_trip_count(cmp_op, init_value, limit_value, step, trip_count))
    {
        return log_skip(loop_label, "trip count unresolved or too large");
    }

    // Verify body is safe to unroll
    for(TAC *cur : body)
    {
        if(cur == cmp || cur == ifz || cur == update_op || cur == update_copy) continue;
        
        if(cur->op == TAC_LABEL) return log_skip(loop_label, "internal label");
        if(cur->op == TAC_GOTO) return log_skip(loop_label, "internal goto");
        if(cur->op == TAC_IFZ) return log_skip(loop_label, "internal ifz");
        if(cur->op == TAC_CALL) return log_skip(loop_label, "function call");
        if(cur->op == TAC_RETURN) return log_skip(loop_label, "return");
    }

    // Perform Unrolling
    if(g_log)
    {
        std::ostringstream oss;
        oss << "unrolling loop " << loop_label << " (" << trip_count << " iterations)";
        g_log->push_back(oss.str());
    }

    // We will insert the unrolled body after the header
    TAC *insert_pos = header->next; // Insert before this (which is the first instruction of body)
    
    // Actually, we can just append to a list and then splice it in.
    // But we need to remove the old body first.
    
    // Remove old body instructions from the chain
    // header -> [body] -> backedge -> next
    // We want: header -> [unrolled body] -> next
    
    // Detach body + backedge
    TAC *body_start = header->next;
    TAC *body_end = backedge;
    
    header->next = backedge->next;
    if(backedge->next) backedge->next->prev = header; else tac_last = header;
    
    // Now insert unrolled copies after header
    TAC *current_pos = header; // We insert after this
    
    // Helper to append
    auto append = [&](TAC *node) {
        TAC *next = current_pos->next;
        current_pos->next = node;
        node->prev = current_pos;
        node->next = next;
        if(next) next->prev = node; else tac_last = node;
        current_pos = node;
    };

    for(int k = 0; k < trip_count; ++k)
    {
        for(TAC *cur : body)
        {
            if(cur == cmp || cur == ifz) continue; // Skip control flow
            
            TAC *cloned = clone_tac(cur);
            append(cloned);
        }
    }
    
    // Clean up the detached nodes (optional, but good practice to avoid leaks if we had a GC or pool)
    // Here we just leave them detached.
    
    g_unrolls++;
    return true;
}

} // namespace

extern "C" void loopunroll_reset(void)
{
    g_log = nullptr;
    g_unrolls = 0;
}

extern "C" int loopunroll_run(void)
{
    std::vector<std::string> run_log;
    g_log = &run_log;
    g_unrolls = 0;

    // We only do one pass of unrolling per call to avoid exploding code if we re-detect unrolled loops (though they shouldn't be loops anymore)
    
    std::vector<TAC*> sequence;
    std::vector<int> func_id;
    std::unordered_map<SYM*, int> label_index;
    
    // Rebuild sequence info
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

    if(!sequence.empty())
    {
        std::vector<LoopInfo> loops;
        std::unordered_set<unsigned long long> seen;

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

        // Sort loops to process inner loops first?
        // Actually, unrolling destroys the loop structure.
        // If we unroll an inner loop, the outer loop just sees a longer body.
        // If we unroll an outer loop, we duplicate the inner loop.
        // It's better to unroll inner loops first.
        std::sort(loops.begin(), loops.end(), [](const LoopInfo &a, const LoopInfo &b) {
            if(a.header_index != b.header_index) return a.header_index > b.header_index;
            return a.back_index > b.back_index;
        });

        for(const LoopInfo &loop : loops)
        {
            // Check if loop is still valid (nodes not removed)
            if(loop.header->next == nullptr && loop.header != tac_last) continue; // Detached?
            
            process_loop(loop);
        }
    }

    g_log = nullptr;

    std::vector<const char*> raw;
    raw.reserve(run_log.size());
    for(const std::string &line : run_log)
    {
        raw.push_back(line.c_str());
    }

    optlog_record(OPT_PASS_LOOPUNROLL,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  g_unrolls);

    return g_unrolls;
}
