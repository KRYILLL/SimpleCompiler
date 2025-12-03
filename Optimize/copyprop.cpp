#include <vector>
#include <unordered_map>
#include <string>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include "copyprop.h"
#include "optlog.h"

namespace {

struct UseSite {
    SYM **slot = nullptr;
};

struct InstructionInfo {
    TAC *tac = nullptr;
    SYM *def = nullptr;
    std::vector<UseSite> uses;
    std::vector<int> succ;
    std::vector<int> pred;
    std::vector<int> kill;
    std::vector<int> gen;
    std::vector<uint8_t> in;
    std::vector<uint8_t> out;
};

struct CopyInfo {
    int id = -1;
    TAC *tac = nullptr;
    SYM *dst = nullptr;
    SYM *src = nullptr;
};

std::vector<std::string> *g_current_log = nullptr;

void log_append(const std::string &line)
{
    if(g_current_log)
    {
        g_current_log->push_back(line);
    }
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
    }//仅处理变量
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

void add_use(std::vector<UseSite> &uses, SYM **slot)
{
    if(slot == nullptr) return;
    SYM *sym = *slot;
    if(!is_tracked(sym)) return;
    uses.push_back(UseSite{slot});
}

void collect_uses(TAC *t, InstructionInfo &info)
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
            add_use(info.uses, &t->b);
            add_use(info.uses, &t->c);
            break;
        case TAC_NEG:
        case TAC_COPY:
            add_use(info.uses, &t->b);
            break;
        case TAC_IFZ:
            add_use(info.uses, &t->b);
            break;
        case TAC_ACTUAL:
        case TAC_RETURN:
        case TAC_OUTPUT:
            add_use(info.uses, &t->a);
            break;
        default:
            break;
    }
}

std::string sym_name(SYM *sym)
{
    if(sym == nullptr) return std::string("<null>");
    return (sym->name != nullptr) ? std::string(sym->name) : std::string("<temp>");
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
        info.def = tac_def(info.tac);//记录该指令顶定义的符号
        collect_uses(info.tac, info);//记录该指令使用了哪些符号
        if(info.tac->op == TAC_LABEL && info.tac->a)
        {
            label_map[info.tac->a] = static_cast<int>(i);
        }//记录label
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
                    if(it != label_map.end()) target = it->second;
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
                    if(it != label_map.end()) target = it->second;
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
    }//记录后继

    for(size_t i = 0; i < infos.size(); ++i)
    {
        for(int succ : infos[i].succ)
        {
            if(succ >= 0 && static_cast<size_t>(succ) < infos.size())
            {
                infos[succ].pred.push_back(static_cast<int>(i));
            }
        }
    }//记录前驱

    std::vector<CopyInfo> copies;
    copies.reserve(infos.size());
    std::unordered_map<SYM*, std::vector<int>> copies_by_dest;
    std::unordered_map<SYM*, std::vector<int>> copies_by_src;

    for(size_t i = 0; i < infos.size(); ++i)
    {
        TAC *t = infos[i].tac;
        if(t->op != TAC_COPY) continue;
        SYM *dst = t->a;
        SYM *src = t->b;
        if(!is_tracked(dst)) continue;
        if(src == nullptr) continue;
        if(dst == src) continue;
        int id = static_cast<int>(copies.size());
        copies.push_back(CopyInfo{id, t, dst, src});
        infos[i].gen.push_back(id);
        copies_by_dest[dst].push_back(id);
        if(is_tracked(src))
        {
            copies_by_src[src].push_back(id);
        }
    }//收集所有COPY指令

    if(copies.empty()) return 0;

    const size_t copy_count = copies.size();
    for(InstructionInfo &info : infos)
    {
        info.in.assign(copy_count, 0);
        info.out.assign(copy_count, 0);
        if(info.def && is_tracked(info.def))
        {
            auto it_dest = copies_by_dest.find(info.def);
            if(it_dest != copies_by_dest.end())
            {
                info.kill.insert(info.kill.end(), it_dest->second.begin(), it_dest->second.end());
            }
            auto it_src = copies_by_src.find(info.def);
            if(it_src != copies_by_src.end())
            {
                info.kill.insert(info.kill.end(), it_src->second.begin(), it_src->second.end());
            }
        }
    }//初始化in/out/kill/gen集合

    bool changed;
    do
    {
        changed = false;
        for(size_t i = 0; i < infos.size(); ++i)
        {
            InstructionInfo &info = infos[i];
            std::vector<uint8_t> new_in(copy_count, 0);
            if(!info.pred.empty())
            {
                new_in = infos[info.pred[0]].out;
                for(size_t p = 1; p < info.pred.size(); ++p)
                {
                    const std::vector<uint8_t> &out_vec = infos[info.pred[p]].out;
                    for(size_t bit = 0; bit < copy_count; ++bit)
                    {
                        new_in[bit] = static_cast<uint8_t>(new_in[bit] && out_vec[bit]);
                    }
                }
            }//计算新的in集合

            if(new_in != info.in)
            {
                info.in.swap(new_in);
                changed = true;
            }//更新in集合

            std::vector<uint8_t> new_out = info.in;
            for(int kill_id : info.kill)
            {
                if(kill_id >= 0 && static_cast<size_t>(kill_id) < copy_count)
                {
                    new_out[kill_id] = 0;
                }
            }
            for(int gen_id : info.gen)
            {
                if(gen_id >= 0 && static_cast<size_t>(gen_id) < copy_count)
                {
                    new_out[gen_id] = 1;
                }
            }
            if(new_out != info.out)
            {
                info.out.swap(new_out);
                changed = true;
            }
        }
    } while(changed);

    int replacements = 0;
    for(size_t i = 0; i < infos.size(); ++i)
    {
        InstructionInfo &info = infos[i];
        const std::vector<uint8_t> &available = info.in;
        for(const UseSite &use : info.uses)
        {
            if(use.slot == nullptr) continue;
            SYM *current = *(use.slot);
            if(!is_tracked(current)) continue;
            auto it = copies_by_dest.find(current);
            if(it == copies_by_dest.end()) continue;

            int chosen = -1;
            for(int copy_id : it->second)
            {
                if(copy_id < 0 || static_cast<size_t>(copy_id) >= available.size()) continue;
                if(!available[copy_id]) continue;
                if(chosen == -1) 
                {
                    chosen = copy_id;
                }
                else
                {
                    chosen = -2;
                    break;
                }
            }
            if(chosen < 0) continue;

            const CopyInfo &cp = copies[chosen];
            SYM *replacement = cp.src;
            if(replacement == nullptr) continue;
            if(replacement == current) continue;

            *(use.slot) = replacement;
            replacements++;

            std::ostringstream msg;
            msg << "replaced use of " << sym_name(current)
                << " with " << sym_name(replacement);
            log_append(msg.str());
        }
    }

    return replacements;
}

} // namespace

extern "C" void copyprop_reset(void)
{
    g_current_log = nullptr;
}

extern "C" int copyprop_run(void)
{
    std::vector<std::string> run_log;
    g_current_log = &run_log;
    int total_replaced = 0;
    for(;;)
    {
        int replaced = run_iteration();
        if(replaced <= 0) break;
        total_replaced += replaced;
    }
    g_current_log = nullptr;

    std::vector<const char*> raw;
    raw.reserve(run_log.size());
    for(const std::string &line : run_log)
    {
        raw.push_back(line.c_str());
    }
    optlog_record(OPT_PASS_COPYPROP,
                  raw.empty() ? nullptr : raw.data(),
                  static_cast<int>(raw.size()),
                  total_replaced);

    return total_replaced;
}
