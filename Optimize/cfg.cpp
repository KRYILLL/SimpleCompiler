#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include "cfg.h"

namespace {

bool is_terminator(TAC *t)
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

void append_edge(BASIC_BLOCK *from, BASIC_BLOCK *to)
{
    if(from == nullptr || to == nullptr) return;
    BB_LIST *succ_node = new BB_LIST{to, from->succ};
    from->succ = succ_node;
    BB_LIST *pred_node = new BB_LIST{from, to->pred};
    to->pred = pred_node;
}

BASIC_BLOCK *find_block_by_start(TAC *start, const std::unordered_map<TAC*, BASIC_BLOCK*> &map)
{
    auto it = map.find(start);
    return (it == map.end()) ? nullptr : it->second;
}

CFG_FUNCTION *build_cfg_for_func(TAC *begin)
{
    if(begin == nullptr) return nullptr;

    TAC *end = begin;
    while(end && end->op != TAC_ENDFUNC) end = end->next;
    if(end == nullptr) return nullptr;

    TAC *cur = begin->next;

    std::unordered_map<SYM*, TAC*> label_map;
    std::vector<TAC*> leaders;
    std::unordered_set<TAC*> leader_seen;

    if(cur)
    {
        leaders.push_back(cur);
        leader_seen.insert(cur);
    }

    for(TAC *p = cur; p && p != end->next; p = p->next)
    {
        if(p->op == TAC_LABEL && p->a)
        {
            label_map[p->a] = p;
            if(!leader_seen.count(p))
            {
                leaders.push_back(p);
                leader_seen.insert(p);
            }
        }
        if(is_terminator(p))
        {
            TAC *next_instr = p->next;
            if(next_instr && next_instr != end->next && !leader_seen.count(next_instr))
            {
                leaders.push_back(next_instr);
                leader_seen.insert(next_instr);
            }
        }
    }

    BASIC_BLOCK *head = nullptr;
    BASIC_BLOCK *tail = nullptr;
    std::unordered_map<TAC*, BASIC_BLOCK*> block_by_start;
    std::vector<BASIC_BLOCK*> block_list;
    block_list.reserve(leaders.size());

    for(size_t i = 0; i < leaders.size(); ++i)
    {
        TAC *start = leaders[i];
        TAC *stop = (i + 1 < leaders.size()) ? leaders[i + 1] : end->next;
        TAC *last = nullptr;
        for(TAC *p = start; p && p != stop; p = p->next)
        {
            last = p;
        }

        BASIC_BLOCK *bb = new BASIC_BLOCK;
        bb->id = static_cast<int>(i);
        bb->label = (start && start->op == TAC_LABEL) ? start->a : nullptr;
        bb->first = start;
        bb->last = last;
        bb->succ = nullptr;
        bb->pred = nullptr;
        bb->next = nullptr;

        if(head == nullptr) head = bb; else tail->next = bb;
        tail = bb;
        block_by_start[start] = bb;
        block_list.push_back(bb);
    }

    for(size_t i = 0; i < block_list.size(); ++i)
    {
        BASIC_BLOCK *bb = block_list[i];
        TAC *last = bb->last;
        if(last == nullptr) continue;

        switch(last->op)
        {
            case TAC_GOTO:
            {
                BASIC_BLOCK *target = nullptr;
                if(last->a)
                {
                    auto it = label_map.find(last->a);
                    if(it != label_map.end())
                    {
                        target = find_block_by_start(it->second, block_by_start);
                    }
                }
                append_edge(bb, target);
                break;
            }
            case TAC_IFZ:
            {
                BASIC_BLOCK *target = nullptr;
                if(last->a)
                {
                    auto it = label_map.find(last->a);
                    if(it != label_map.end())
                    {
                        target = find_block_by_start(it->second, block_by_start);
                    }
                }
                append_edge(bb, target);
                if(i + 1 < block_list.size())
                {
                    append_edge(bb, block_list[i + 1]);
                }
                break;
            }
            case TAC_RETURN:
            case TAC_ENDFUNC:
                break;
            default:
                if(i + 1 < block_list.size())
                {
                    append_edge(bb, block_list[i + 1]);
                }
                break;
        }
    }

    const char *base_name = "<anon>";
    if(begin->prev && begin->prev->op == TAC_LABEL && begin->prev->a && begin->prev->a->name)
    {
        base_name = begin->prev->a->name;
    }

    CFG_FUNCTION *cfg = new CFG_FUNCTION;
    cfg->name = strdup(base_name);
    cfg->blocks = head;
    cfg->block_count = static_cast<int>(block_list.size());
    cfg->next = nullptr;
    return cfg;
}

void bb_list_free(BB_LIST *list)
{
    while(list)
    {
        BB_LIST *next = list->next;
        delete list;
        list = next;
    }
}

} // namespace

extern "C" CFG_ALL *cfg_build_all(void)
{
    CFG_ALL *all = new CFG_ALL;
    all->funcs = nullptr;
    all->func_count = 0;
    CFG_FUNCTION *tail = nullptr;

    for(TAC *cur = tac_first; cur; cur = cur->next)
    {
        if(cur->op == TAC_BEGINFUNC)
        {
            CFG_FUNCTION *func = build_cfg_for_func(cur);
            if(func == nullptr) continue;
            if(all->funcs == nullptr)
            {
                all->funcs = func;
            }
            else
            {
                tail->next = func;
            }
            tail = func;
            all->func_count++;

            while(cur && cur->op != TAC_ENDFUNC) cur = cur->next;
            if(cur == nullptr) break;
        }
    }
    return all;
}

static void print_block(FILE *f, BASIC_BLOCK *b)
{
    out_str(f, "B%d", b->id);
    if(b->label && b->label->name)
    {
        out_str(f, " [%s]", b->label->name);
    }
}

extern "C" void cfg_print_all(CFG_ALL *all)
{
    if(all == nullptr) return;
    out_str(file_x, "\n# cfg\n\n");
    for(CFG_FUNCTION *cf = all->funcs; cf; cf = cf->next)
    {
        out_str(file_x, "## Function %s\n", cf->name ? cf->name : "<anon>");
        for(BASIC_BLOCK *b = cf->blocks; b; b = b->next)
        {
            print_block(file_x, b);
            out_str(file_x, ":\n");
            for(TAC *t = b->first; t; t = t->next)
            {
                out_str(file_x, "    ");
                out_tac(file_x, t);
                out_str(file_x, "\n");
                if(t == b->last) break;
            }
            out_str(file_x, "    succ: ");
            BB_LIST *succ = b->succ;
            bool first = true;
            while(succ)
            {
                if(!first) out_str(file_x, ", ");
                first = false;
                print_block(file_x, succ->bb);
                succ = succ->next;
            }
            out_str(file_x, "\n\n");
        }
    }
}

extern "C" void cfg_free_all(CFG_ALL *all)
{
    if(all == nullptr) return;
    CFG_FUNCTION *func = all->funcs;
    while(func)
    {
        CFG_FUNCTION *next_func = func->next;
        BASIC_BLOCK *block = func->blocks;
        while(block)
        {
            BASIC_BLOCK *next_block = block->next;
            bb_list_free(block->succ);
            bb_list_free(block->pred);
            delete block;
            block = next_block;
        }
        if(func->name) free(func->name);
        delete func;
        func = next_func;
    }
    delete all;
}
