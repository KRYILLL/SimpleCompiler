#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"
#include "cfg.h"

typedef struct tac_ptr_array {
    TAC **data;
    int size;
    int cap;
} TAC_PTR_ARRAY;

static void arr_init(TAC_PTR_ARRAY *a) { a->data=NULL; a->size=0; a->cap=0; }
static void arr_push(TAC_PTR_ARRAY *a, TAC *t) {
    if(a->size==a->cap){ 
        a->cap = a->cap? a->cap*2:16; 
        a->data = (TAC**)realloc(a->data, a->cap*sizeof(TAC*)); }
    a->data[a->size++] = t;
}//加入TAC
static int arr_contains(TAC_PTR_ARRAY *a, TAC *t){
    for(int i=0;i<a->size;i++) if(a->data[i]==t) return 1; return 0;
}//确定TAC是否存在

typedef struct label_map_entry {
    char *name; /* label name */
    TAC *tac;   /* TAC_LABEL instruction */
} LABEL_MAP_ENTRY;

typedef struct label_map {
    LABEL_MAP_ENTRY *data;
    int size;
    int cap;
} LABEL_MAP;//Label表

static void lmap_init(LABEL_MAP *m){ m->data=NULL; m->size=0; m->cap=0; }
static void lmap_put(LABEL_MAP *m, const char *name, TAC *t){
    if(m->size==m->cap){ m->cap=m->cap? m->cap*2:16; m->data=(LABEL_MAP_ENTRY*)realloc(m->data, m->cap*sizeof(LABEL_MAP_ENTRY)); }
    m->data[m->size].name = strdup(name);
    m->data[m->size].tac = t;
    m->size++;
}
static TAC* lmap_get(LABEL_MAP *m, const char *name){
    for(int i=0;i<m->size;i++) if(strcmp(m->data[i].name, name)==0) return m->data[i].tac; return NULL;
}

static BASIC_BLOCK *bb_new(int id, TAC *first){
    BASIC_BLOCK *bb=(BASIC_BLOCK*)malloc(sizeof(BASIC_BLOCK));
    bb->id=id;
    bb->label=NULL;
    bb->first=first;
    bb->last=NULL;
    bb->succ=NULL;
    bb->pred=NULL;
    bb->next=NULL;
    return bb;
}

static void bb_add_edge(BASIC_BLOCK *from, BASIC_BLOCK *to){
    if(from==NULL || to==NULL) return;
    BB_LIST *s=(BB_LIST*)malloc(sizeof(BB_LIST)); s->bb=to; s->next=from->succ; from->succ=s;
    BB_LIST *p=(BB_LIST*)malloc(sizeof(BB_LIST)); p->bb=from; p->next=to->pred; to->pred=p;
}//添加边

static int is_terminator(TAC *t){
    if(!t) return 0;
    switch(t->op){
        case TAC_GOTO:
        case TAC_IFZ:
        case TAC_RETURN:
        case TAC_ENDFUNC:
            return 1;
        default:
            return 0;
    }
}//寻找终结符

static BASIC_BLOCK* find_block_by_start(TAC *s, BASIC_BLOCK *h){
    for(BASIC_BLOCK *b=h; b; b=b->next){ if(b->first==s) return b; }
    return NULL;
}//通过TAC查找BLOCK

static BASIC_BLOCK* find_block_by_label(const char *lname, BASIC_BLOCK *h, LABEL_MAP *lm){
    TAC *lt = lmap_get(lm, lname);
    if(!lt) return NULL;
    return find_block_by_start(lt, h);
}//通过LABEL查找BLOCK

/* 为单个函数构建 CFG：从该函数的 TAC_BEGINFUNC 之后开始，直到匹配的 TAC_ENDFUNC（包含）为止 */
static CFG_FUNCTION *build_cfg_for_func(TAC *begin){
    /* 确定函数名：通常在 BEGINFUNC 前一条是函数名对应的 TAC_LABEL，这里回溯一步获取 */
    TAC *t = begin;
    char *fname = NULL;
    /* 若未按上述格式出现，则使用占位名 <anon> */
    if(t && t->prev && t->prev->op==TAC_LABEL && t->prev->a && t->prev->a->name){
        fname = t->prev->a->name;
    } else {
        fname = strdup("<anon>");
    }

    /* 在 begin（不含）到 end（含）范围内，收集所有 label 与 leader（基本块入口） */
    LABEL_MAP lm; lmap_init(&lm);
    TAC_PTR_ARRAY leaders; arr_init(&leaders);

    TAC *first = begin; /* BEGINFUNC 自身 */
    TAC *end = begin;
    while(end && end->op != TAC_ENDFUNC) end = end->next;
    TAC *cur = begin->next; /* BEGINFUNC 之后的第一条指令 */

    if(cur) arr_push(&leaders, cur); /* 第一条指令一定是一个 leader（第一个基本块头） */

    for(TAC *p=cur; p && p!=end->next; p=p->next){
        if(p->op==TAC_LABEL){
            if(p->a && p->a->name) lmap_put(&lm, p->a->name, p);
            if(!arr_contains(&leaders, p)) arr_push(&leaders, p);
        }
        /* 任何“终止”指令（goto/ifz/return/endfunc）之后的下一条也是 leader */
        if(is_terminator(p)){
            if(p->next && p->next!=end->next && !arr_contains(&leaders, p->next)) arr_push(&leaders, p->next);
        }
        /* 跳转目标自身就是 leader（已通过 label 标记） */
    }// 通过标签、终止指令与顺序规则补齐所有块头

    /* 按程序出现次序对 leaders 排序：通过线性扫描，将出现的 leaders 依序放入 ordered 中 */
    TAC_PTR_ARRAY ordered; arr_init(&ordered);
    for(TAC *p=cur; p && p!=end->next; p=p->next){ if(arr_contains(&leaders, p)) arr_push(&ordered, p); }

    /* 构建基本块 */
    BASIC_BLOCK *head=NULL, *tail=NULL; int bid=0;
    for(int i=0;i<ordered.size;i++){
        TAC *start = ordered.data[i];
        BASIC_BLOCK *bb = bb_new(bid++, start);
        if(start->op==TAC_LABEL) bb->label = start->a;
        /* 确定块的 last：直到下一个 leader 之前的最后一条，或到 end->next 为止（包含） */
        TAC *last = NULL;
        TAC *p = start;
        TAC *stop = (i+1<ordered.size) ? ordered.data[i+1] : end->next;
        while(p && p!=stop){ last = p; p = p->next; }
        bb->last = last;

        if(!head) head=bb; else tail->next=bb; tail=bb;
    }

    /* 建立控制流边（后继/前驱） */
    for(BASIC_BLOCK *b=head; b; b=b->next){
        TAC *last = b->last;
        if(!last) continue;
        switch(last->op){
            case TAC_GOTO:{
                const char *lname = last->a ? last->a->name : NULL;
                BASIC_BLOCK *tgt = lname? find_block_by_label(lname, head, &lm): NULL;
                if(tgt) bb_add_edge(b, tgt);
                break;
            }
            case TAC_IFZ:{
                const char *lname = last->a ? last->a->name : NULL;
                BASIC_BLOCK *tgt = lname? find_block_by_label(lname, head, &lm): NULL;
                if(tgt) bb_add_edge(b, tgt);
                /* 顺序落入（fall-through）后继 */
                if(b->next) bb_add_edge(b, b->next);
                break;
            }
            case TAC_RETURN:
            case TAC_ENDFUNC:
                /* 无后继 */
                break;
            default:
                /* 普通顺序落入 */
                if(b->next) bb_add_edge(b, b->next);
                break;
        }
    }

    CFG_FUNCTION *cfg=(CFG_FUNCTION*)malloc(sizeof(CFG_FUNCTION));
    cfg->name = fname ? strdup(fname) : strdup("<anon>");
    cfg->blocks = head;
    cfg->block_count = bid;
    cfg->next = NULL;
    /* 注意：LABEL_MAP 中分配的字符串由进程结束统一释放；如需严格释放可扩展清理逻辑 */
    return cfg;
}

CFG_ALL *cfg_build_all(void){
    CFG_ALL *all=(CFG_ALL*)malloc(sizeof(CFG_ALL));
    all->funcs=NULL; all->func_count=0;
    CFG_FUNCTION *tail=NULL;

    for(TAC *cur=tac_first; cur; cur=cur->next){
        if(cur->op==TAC_BEGINFUNC){
            CFG_FUNCTION *cf = build_cfg_for_func(cur);
            if(!all->funcs) all->funcs=cf; else tail->next=cf; tail=cf;
            all->func_count++;
            /* skip to endfunc for speed */
            while(cur && cur->op!=TAC_ENDFUNC) cur=cur->next;
            if(!cur) break;
        }
    }
    return all;
}

static void print_block(FILE *f, BASIC_BLOCK *b){
    out_str(f, "B%d", b->id);
    if(b->label) out_str(f, " [%s]", b->label->name);
}

void cfg_print_all(CFG_ALL *all){
    if(!all) return;
    out_str(file_x, "\n# cfg\n\n");
    for(CFG_FUNCTION *cf=all->funcs; cf; cf=cf->next){
        out_str(file_x, "## Function %s\n", cf->name);
        /* 列出基本块与其内部 TAC */
        for(BASIC_BLOCK *b=cf->blocks; b; b=b->next){
            print_block(file_x, b); out_str(file_x, ":\n");
            for(TAC *t=b->first; t; t=t->next){
                out_str(file_x, "    ");
                out_tac(file_x, t);
                out_str(file_x, "\n");
                if(t==b->last) break;
            }
            /* 后继列表 */
            out_str(file_x, "    succ: ");
            BB_LIST *s=b->succ; int first=1;
            while(s){ if(!first) out_str(file_x, ", "); first=0; print_block(file_x, s->bb); s=s->next; }
            out_str(file_x, "\n\n");
        }
    }
}

static void bb_list_free(BB_LIST *l){ while(l){ BB_LIST *n=l->next; free(l); l=n; } }
void cfg_free_all(CFG_ALL *all){
    if(!all) return;
    CFG_FUNCTION *cf=all->funcs;
    while(cf){
        CFG_FUNCTION *ncf=cf->next;
        BASIC_BLOCK *b=cf->blocks;
        while(b){ BASIC_BLOCK *nb=b->next; bb_list_free(b->succ); bb_list_free(b->pred); free(b); b=nb; }
        if(cf->name) free(cf->name);
        free(cf);
        cf=ncf;
    }
    free(all);
}
