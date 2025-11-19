#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "deadcode.h"

#define MAX_SUCC 3

typedef struct sym_node {
    SYM *sym;
    struct sym_node *next;
} SYM_NODE;

typedef struct sym_set {
    SYM_NODE *head;
} SYM_SET;

typedef struct label_map_entry {
    SYM *label;
    int index;
} LABEL_MAP_ENTRY;

typedef struct label_map {
    LABEL_MAP_ENTRY *data;
    int size;
    int cap;
} LABEL_MAP;

typedef struct dc_log_entry {
    char *text;
    struct dc_log_entry *next;
} DC_LOG_ENTRY;

typedef struct dc_info {
    TAC *tac;
    SYM *def;
    SYM *uses[3];
    int use_count;
    int succ[MAX_SUCC];
    int succ_count;
    SYM_SET live_in;
    SYM_SET live_out;
    int removable;
} DC_INFO;

static DC_LOG_ENTRY *log_head = NULL;
static DC_LOG_ENTRY *log_tail = NULL;
static int removed_total = 0;

/* -------- small utilities -------- */

static void symset_init(SYM_SET *set)
{
    set->head = NULL;
}

static void symset_clear(SYM_SET *set)
{
    SYM_NODE *node = set->head;
    while(node != NULL)
    {
        SYM_NODE *next = node->next;
        free(node);
        node = next;
    }
    set->head = NULL;
}

static int symset_contains(const SYM_SET *set, SYM *sym)
{
    if(sym == NULL) return 0;
    for(SYM_NODE *node = set->head; node != NULL; node = node->next)
    {
        if(node->sym == sym) return 1;
    }
    return 0;
}//判断是否包含符号

static int symset_add(SYM_SET *set, SYM *sym)
{
    if(sym == NULL) return 0;
    if(symset_contains(set, sym)) return 0;
    SYM_NODE *node = (SYM_NODE *)malloc(sizeof(SYM_NODE));
    if(node == NULL) return 0;
    node->sym = sym;
    node->next = set->head;
    set->head = node;
    return 1;
}//添加符号到集合

static void symset_remove(SYM_SET *set, SYM *sym)
{
    if(sym == NULL) return;
    SYM_NODE **link = &set->head;
    while(*link != NULL)
    {
        if((*link)->sym == sym)
        {
            SYM_NODE *victim = *link;
            *link = victim->next;
            free(victim);
            return;
        }
        link = &(*link)->next;
    }
}//从集合中移除符号

static void symset_union_into(SYM_SET *dst, const SYM_SET *src)
{
    for(SYM_NODE *node = src->head; node != NULL; node = node->next)
    {
        symset_add(dst, node->sym);
    }
}//将src集合的元素加入dst集合

static int symset_equal(const SYM_SET *a, const SYM_SET *b)
{
    for(SYM_NODE *node = a->head; node != NULL; node = node->next)
    {
        if(!symset_contains(b, node->sym)) return 0;
    }
    for(SYM_NODE *node = b->head; node != NULL; node = node->next)
    {
        if(!symset_contains(a, node->sym)) return 0;
    }
    return 1;
}//判断两个集合是否相等

static int symset_replace(SYM_SET *dst, const SYM_SET *src)
{
    if(symset_equal(dst, src)) return 0;
    symset_clear(dst);
    for(SYM_NODE *node = src->head; node != NULL; node = node->next)
    {
        symset_add(dst, node->sym);
    }
    return 1;
}//用src集合替换dst集合

/* -------- label map helpers -------- */

static void label_map_init(LABEL_MAP *m)
{
    m->data = NULL;
    m->size = 0;
    m->cap = 0;
}

static void label_map_free(LABEL_MAP *m)
{
    free(m->data);
    m->data = NULL;
    m->size = 0;
    m->cap = 0;
}

static void label_map_put(LABEL_MAP *m, SYM *label, int index)
{
    if(label == NULL) return;
    if(m->size == m->cap)
    {
        int new_cap = (m->cap == 0) ? 32 : m->cap * 2;
        LABEL_MAP_ENTRY *new_data = (LABEL_MAP_ENTRY *)realloc(m->data, new_cap * sizeof(LABEL_MAP_ENTRY));
        if(new_data == NULL) return;
        m->data = new_data;
        m->cap = new_cap;
    }
    m->data[m->size].label = label;
    m->data[m->size].index = index;
    m->size++;
}

static int label_map_get(const LABEL_MAP *m, SYM *label)
{
    if(label == NULL) return -1;
    for(int i = 0; i < m->size; ++i)
    {
        if(m->data[i].label == label) return m->data[i].index;
    }
    return -1;
}

/* -------- logging helpers -------- */

static void log_clear(void)
{
    DC_LOG_ENTRY *node = log_head;
    while(node != NULL)
    {
        DC_LOG_ENTRY *next = node->next;
        free(node->text);
        free(node);
        node = next;
    }
    log_head = NULL;
    log_tail = NULL;
    removed_total = 0;
}

static void log_append(const char *msg)
{
    if(msg == NULL) return;
    DC_LOG_ENTRY *entry = (DC_LOG_ENTRY *)malloc(sizeof(DC_LOG_ENTRY));
    if(entry == NULL) return;
    entry->text = strdup(msg);
    if(entry->text == NULL)
    {
        free(entry);
        return;
    }
    entry->next = NULL;
    if(log_tail == NULL)
    {
        log_head = entry;
        log_tail = entry;
    }
    else
    {
        log_tail->next = entry;
        log_tail = entry;
    }
}

static const char *tac_op_name(int op)
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

static int tac_is_side_effect_free(int op)
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
            return 1;
        default:
            return 0;
    }
}

static SYM *tac_def(TAC *t)
{
    if(t == NULL) return NULL;
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
            return NULL;
    }
}

static void info_add_use(DC_INFO *info, SYM *sym)
{
    if(sym == NULL) return;
    if(sym->type == SYM_INT || sym->type == SYM_TEXT || sym->type == SYM_FUNC || sym->type == SYM_LABEL) return;
    if(info->use_count >= 3) return;
    info->uses[info->use_count++] = sym;
}

static void info_collect_uses(DC_INFO *info, TAC *t)
{
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
            info_add_use(info, t->b);
            info_add_use(info, t->c);
            break;
        case TAC_NEG:
        case TAC_COPY:
            info_add_use(info, t->b);
            break;
        case TAC_IFZ:
            info_add_use(info, t->b);
            break;
        case TAC_ACTUAL:
            info_add_use(info, t->a);
            break;
        case TAC_RETURN:
            info_add_use(info, t->a);
            break;
        case TAC_OUTPUT:
            info_add_use(info, t->a);
            break;
        default:
            break;
    }
}

static void add_succ(DC_INFO *info, int succ)
{
    if(succ < 0) return;
    for(int i = 0; i < info->succ_count; ++i)
    {
        if(info->succ[i] == succ) return;
    }
    if(info->succ_count < MAX_SUCC)
    {
        info->succ[info->succ_count++] = succ;
    }
}

static int run_iteration(void)
{
    int count = 0;
    for(TAC *cur = tac_first; cur != NULL; cur = cur->next) count++;
    if(count == 0) return 0;

    DC_INFO *infos = (DC_INFO *)calloc(count, sizeof(DC_INFO));
    if(infos == NULL) return 0;

    LABEL_MAP labels; label_map_init(&labels);

    int idx = 0;
    for(TAC *cur = tac_first; cur != NULL; cur = cur->next, ++idx)
    {
        DC_INFO *info = &infos[idx];
        info->tac = cur;
        info->def = tac_def(cur);
        info->use_count = 0;
        info->succ_count = 0;
        info->removable = 0;
        symset_init(&info->live_in);
        symset_init(&info->live_out);
        info_collect_uses(info, cur);
        if(cur->op == TAC_LABEL && cur->a != NULL)
        {
            label_map_put(&labels, cur->a, idx);
        }
    }//初始信息收集

    for(int i = 0; i < count; ++i)
    {
        TAC *t = infos[i].tac;
        switch(t->op)
        {
            case TAC_GOTO:
                add_succ(&infos[i], label_map_get(&labels, t->a));
                break;
            case TAC_IFZ:
                if(i + 1 < count) add_succ(&infos[i], i + 1);
                add_succ(&infos[i], label_map_get(&labels, t->a));
                break;
            case TAC_RETURN:
            case TAC_ENDFUNC:
                break;
            default:
                if(i + 1 < count) add_succ(&infos[i], i + 1);
                break;
        }
    }//控制流边收集

    int changed;
    do
    {
        changed = 0;
        for(int i = count - 1; i >= 0; --i)
        {
            SYM_SET new_out; symset_init(&new_out);
            for(int s = 0; s < infos[i].succ_count; ++s)
            {
                int succ = infos[i].succ[s];
                if(succ >= 0 && succ < count)
                {
                    symset_union_into(&new_out, &infos[succ].live_in);
                }
            }//合并后继的live_in到new_out
            if(symset_replace(&infos[i].live_out, &new_out)) changed = 1;
            symset_clear(&new_out);

            SYM_SET new_in; symset_init(&new_in);
            for(int u = 0; u < infos[i].use_count; ++u)
            {
                symset_add(&new_in, infos[i].uses[u]);
            }
            SYM_SET out_minus_def; symset_init(&out_minus_def);
            symset_union_into(&out_minus_def, &infos[i].live_out);
            symset_remove(&out_minus_def, infos[i].def);
            symset_union_into(&new_in, &out_minus_def);
            if(symset_replace(&infos[i].live_in, &new_in)) changed = 1;
            symset_clear(&out_minus_def);
            symset_clear(&new_in);
        }
    } while(changed);//数据流分析迭代

    int removed_this_iter = 0;
    for(int i = 0; i < count; ++i)
    {
        TAC *t = infos[i].tac;
        if(!tac_is_side_effect_free(t->op)) continue;
        if(infos[i].def == NULL) continue;
        if(symset_contains(&infos[i].live_out, infos[i].def)) continue;

        infos[i].removable = 1;
        removed_this_iter++;

        char line[128];
        snprintf(line, sizeof(line), "removed dead %s targeting %s", tac_op_name(t->op), infos[i].def->name ? infos[i].def->name : "<sym>");
        log_append(line);
    }//标记可删除的指令

    for(int i = 0; i < count; ++i)
    {
        if(!infos[i].removable) continue;
        TAC *t = infos[i].tac;
        TAC *prev = t->prev;
        TAC *next = t->next;
        if(prev != NULL) prev->next = next; else tac_first = next;
        if(next != NULL) next->prev = prev; else tac_last = prev;
        t->prev = NULL;
        t->next = NULL;
    }//删除指令

    for(int i = 0; i < count; ++i)
    {
        symset_clear(&infos[i].live_in);
        symset_clear(&infos[i].live_out);
    }//清理集合

    label_map_free(&labels);
    free(infos);

    removed_total += removed_this_iter;
    return removed_this_iter;
}

void deadcode_run(void)
{
    log_clear();
    while(run_iteration() > 0) {
        /* keep iterating until a fixpoint is reached */
    }
}

void deadcode_emit_report(FILE *out)
{
    if(out == NULL) return;
    out_str(out, "\n\t# dead assignment elimination pass\n");
    if(removed_total == 0)
    {
        out_str(out, "\t#   no changes\n\n");
    }
    else
    {
        for(DC_LOG_ENTRY *node = log_head; node != NULL; node = node->next)
        {
            out_str(out, "\t#   %s\n", node->text);
        }
        out_str(out, "\t#   total removed: %d\n\n", removed_total);
    }
    log_clear();
}
