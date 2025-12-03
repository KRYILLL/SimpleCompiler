#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include "tac.h"
#include "obj.h"
#include "constfold.h"
#include "copyprop.h"
#include "optlog.h"
#include "deadcode.h"

/* global var */
int tos; /* top of static */
int tof; /* top of frame */
int oof; /* offset of formal */
int oon; /* offset of next frame */
struct rdesc rdesc[R_NUM];

typedef struct sym_reg_info
{
	SYM *sym;
	int *uses;
	int use_count;
	int use_cap;
	int use_cursor;
	struct sym_reg_info *next;
} SymRegInfo;

static SymRegInfo *sym_info_list = NULL;
static int current_instr_index = -1;

static SymRegInfo *syminfo_get(SYM *sym, int create)
{
	if(sym == NULL) return NULL;
	if(sym->type != SYM_VAR) return NULL;
	SymRegInfo *info = (SymRegInfo *)sym->etc;
	if(info == NULL && create)
	{
		info = (SymRegInfo *)malloc(sizeof(SymRegInfo));
		if(info == NULL)
		{
			error("out of memory in register allocator\n");
		}
		info->sym = sym;
		info->uses = NULL;
		info->use_count = 0;
		info->use_cap = 0;
		info->use_cursor = 0;
		info->next = sym_info_list;
		sym_info_list = info;
		sym->etc = info;
	}
	return info;
}

static void syminfo_add_use(SYM *sym, int index)
{
	SymRegInfo *info = syminfo_get(sym, 1);
	if(info == NULL) return;
	if(info->use_count == info->use_cap)
	{
		int new_cap = (info->use_cap == 0) ? 4 : info->use_cap * 2;
		int *new_buf = (int *)realloc(info->uses, new_cap * sizeof(int));
		if(new_buf == NULL)
		{
			error("out of memory in register allocator\n");
		}
		info->uses = new_buf;
		info->use_cap = new_cap;
	}
	info->uses[info->use_count++] = index;
}

static int syminfo_peek_next_use(SYM *sym)
{
	SymRegInfo *info = syminfo_get(sym, 0);
	if(info == NULL) return INT_MAX;
	if(info->use_cursor >= info->use_count) return INT_MAX;
	return info->uses[info->use_cursor];
}

static void syminfo_consume_use(SYM *sym, int index)
{
	SymRegInfo *info = syminfo_get(sym, 0);
	if(info == NULL) return;
	if(info->use_cursor < info->use_count && info->uses[info->use_cursor] == index)
	{
		info->use_cursor++;
	}
}

static void syminfo_reset_cursors(void)
{
	for(SymRegInfo *info = sym_info_list; info != NULL; info = info->next)
	{
		info->use_cursor = 0;
	}
}

static void syminfo_cleanup(void)
{
	SymRegInfo *cur = sym_info_list;
	while(cur != NULL)
	{
		SymRegInfo *next = cur->next;
		if(cur->uses) free(cur->uses);
		if(cur->sym) cur->sym->etc = NULL;
		free(cur);
		cur = next;
	}
	sym_info_list = NULL;
}

static int syminfo_has_future_use(SYM *sym, int index)
{
	SymRegInfo *info = syminfo_get(sym, 0);
	if(info == NULL) return 0;
	for(int i = info->use_cursor; i < info->use_count; ++i)
	{
		if(info->uses[i] > index)
		{
			return 1;
		}
	}
	return 0;
}

static void regalloc_record_uses(TAC *t, int index)
{
	if(t == NULL) return;
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
			if(t->b) syminfo_add_use(t->b, index);
			if(t->c) syminfo_add_use(t->c, index);
			break;

		case TAC_NEG:
		case TAC_COPY:
			if(t->b) syminfo_add_use(t->b, index);
			break;

		case TAC_IFZ:
		case TAC_OUTPUT:
		case TAC_ACTUAL:
		case TAC_RETURN:
			if(t->b) syminfo_add_use(t->b, index);
			else if(t->a) syminfo_add_use(t->a, index);
			break;

		case TAC_CALL:
			/* no direct operand uses except through TAC_ACTUAL */
			break;

		default:
			break;
	}
}

void rdesc_clear(int r)    
{
	rdesc[r].var = NULL;
	rdesc[r].mod = 0;
}

void rdesc_fill(int r, SYM *s, int mod)
{
	int old;
	for(old=R_GEN; old < R_NUM; old++)
	{
		if(rdesc[old].var==s)
		{
			rdesc_clear(old);
		}
	}

	rdesc[r].var=s;
	rdesc[r].mod=mod;
}     

void asm_write_back(int r)
{
	if((rdesc[r].var!=NULL) && rdesc[r].mod)
	{
		if(rdesc[r].var->scope==1) /* local var */
		{
			out_str(file_s, "	STO (R%u+%u),R%u\n", R_BP, rdesc[r].var->offset, r);
		}
		else /* global var */
		{
			out_str(file_s, "	LOD R%u,STATIC\n", R_TP);
			out_str(file_s, "	STO (R%u+%u),R%u\n", R_TP, rdesc[r].var->offset, r);
		}
		rdesc[r].mod=UNMODIFIED;
	}
}

void asm_load(int r, SYM *s) 
{
	/* already in a reg */
	for(int i=R_GEN; i < R_NUM; i++)  
	{
		if(rdesc[i].var==s)
		{
			/* load from the reg */
			out_str(file_s, "	LOD R%u,R%u\n", r, i);

			/* update rdesc */
			// rdesc_fill(r, s, rdesc[i].mod);
			return;
		}
	}
	
	/* not in a reg */
	switch(s->type)
	{
		case SYM_INT:
		out_str(file_s, "	LOD R%u,%u\n", r, s->value);
		break;

		case SYM_VAR:
		if(s->scope==1) /* local var */
		{
			if((s->offset)>=0) out_str(file_s, "	LOD R%u,(R%u+%d)\n", r, R_BP, s->offset);
			else out_str(file_s, "	LOD R%u,(R%u-%d)\n", r, R_BP, -(s->offset));
		}
		else /* global var */
		{
			out_str(file_s, "	LOD R%u,STATIC\n", R_TP);
			out_str(file_s, "	LOD R%u,(R%u+%d)\n", r, R_TP, s->offset);
		}
		break;

		case SYM_TEXT:
		out_str(file_s, "	LOD R%u,L%u\n", r, s->label);
		break;
	}

	// rdesc_fill(r, s, UNMODIFIED);
}

int reg_alloc(SYM *s, int need_value)
{
	if(s == NULL) return R_UNDEF;

	for(int r = R_GEN; r < R_NUM; r++)
	{
		if(rdesc[r].var == s)
		{
			return r;
		}
	}

	int target = -1;
	for(int r = R_GEN; r < R_NUM; r++)
	{
		if(rdesc[r].var == NULL)
		{
			target = r;
			break;
		}
	}

	if(target == -1)
	{
		int best_reg = -1;
		long long best_score = -1;
		for(int r = R_GEN; r < R_NUM; r++)
		{
			SYM *held = rdesc[r].var;
			if(held == NULL)
			{
				best_reg = r;
				break;
			}
			int next_use = syminfo_peek_next_use(held);
			if(next_use == current_instr_index && held != s)
			{
				continue;
			}
			long long score = (next_use == INT_MAX) ? ((long long)INT_MAX + 1LL) : (long long)next_use;
			if(score > best_score)
			{
				best_score = score;
				best_reg = r;
			}
		}
		if(best_reg == -1)
		{
			best_reg = R_GEN;
		}
		target = best_reg;
		asm_write_back(target);
		rdesc_clear(target);
	}

	if(need_value)
	{
		asm_load(target, s);
	}
	rdesc_fill(target, s, UNMODIFIED);
	return target;
}

void asm_bin(const char *op, SYM *a, SYM *b, SYM *c)
{
	int reg_b = reg_alloc(b, 1);
	int reg_c = reg_alloc(c, 1);
	if(b && b != a && syminfo_has_future_use(b, current_instr_index))
	{
		asm_write_back(reg_b);
	}
	out_str(file_s, "	%s R%u,R%u\n", op, reg_b, reg_c);
	syminfo_consume_use(b, current_instr_index);
	syminfo_consume_use(c, current_instr_index);
	rdesc_fill(reg_b, a, MODIFIED);
}   

void asm_cmp(int op, SYM *a, SYM *b, SYM *c)
{
	int reg_b = reg_alloc(b, 1);
	int reg_c = reg_alloc(c, 1);
	if(b && b != a && syminfo_has_future_use(b, current_instr_index))
	{
		asm_write_back(reg_b);
	}
	out_str(file_s, "	SUB R%u,R%u\n", reg_b, reg_c);
	out_str(file_s, "	TST R%u\n", reg_b);

	switch(op)
	{		
		case TAC_EQ:
		out_str(file_s, "	LOD R3,R1+40\n");
		out_str(file_s, "	JEZ R3\n");
		out_str(file_s, "	LOD R%u,0\n", reg_b);
		out_str(file_s, "	LOD R3,R1+24\n");
		out_str(file_s, "	JMP R3\n");
		out_str(file_s, "	LOD R%u,1\n", reg_b);
		break;
		
		case TAC_NE:
		out_str(file_s, "	LOD R3,R1+40\n");
		out_str(file_s, "	JEZ R3\n");
		out_str(file_s, "	LOD R%u,1\n", reg_b);
		out_str(file_s, "	LOD R3,R1+24\n");
		out_str(file_s, "	JMP R3\n");
		out_str(file_s, "	LOD R%u,0\n", reg_b);
		break;
		
		case TAC_LT:
		out_str(file_s, "	LOD R3,R1+40\n");
		out_str(file_s, "	JLZ R3\n");
		out_str(file_s, "	LOD R%u,0\n", reg_b);
		out_str(file_s, "	LOD R3,R1+24\n");
		out_str(file_s, "	JMP R3\n");
		out_str(file_s, "	LOD R%u,1\n", reg_b);
		break;
		
		case TAC_LE:
		out_str(file_s, "	LOD R3,R1+40\n");
		out_str(file_s, "	JGZ R3\n");
		out_str(file_s, "	LOD R%u,1\n", reg_b);
		out_str(file_s, "	LOD R3,R1+24\n");
		out_str(file_s, "	JMP R3\n");
		out_str(file_s, "	LOD R%u,0\n", reg_b);
		break;
		
		case TAC_GT:
		out_str(file_s, "	LOD R3,R1+40\n");
		out_str(file_s, "	JGZ R3\n");
		out_str(file_s, "	LOD R%u,0\n", reg_b);
		out_str(file_s, "	LOD R3,R1+24\n");
		out_str(file_s, "	JMP R3\n");
		out_str(file_s, "	LOD R%u,1\n", reg_b);
		break;
		
		case TAC_GE:
		out_str(file_s, "	LOD R3,R1+40\n");
		out_str(file_s, "	JLZ R3\n");
		out_str(file_s, "	LOD R%u,1\n", reg_b);
		out_str(file_s, "	LOD R3,R1+24\n");
		out_str(file_s, "	JMP R3\n");
		out_str(file_s, "	LOD R%u,0\n", reg_b);
		break;
	}

	/* Delete c from the descriptors and insert a */
	rdesc_clear(reg_b);
	rdesc_fill(reg_b, a, MODIFIED);
	syminfo_consume_use(b, current_instr_index);
	syminfo_consume_use(c, current_instr_index);
}   

void asm_cond(char *op, SYM *a,  char *l)
{
	for(int r=R_GEN; r < R_NUM; r++) asm_write_back(r);

	if(a !=NULL)
	{
		int r = R_UNDEF;
		for(int i=R_GEN; i < R_NUM; i++)
		{
			if(rdesc[i].var == a)
			{
				r = i;
				break;
			}
		}
		if(r == R_UNDEF)
		{
			r = reg_alloc(a, 1);
		}
		out_str(file_s, "	TST R%u\n", r);
		syminfo_consume_use(a, current_instr_index);
	}

	out_str(file_s, "	%s %s\n", op, l); 
} 

void asm_call(SYM *a, SYM *b)
{
	int r;
	for(int r=R_GEN; r < R_NUM; r++) asm_write_back(r);
	for(int r=R_GEN; r < R_NUM; r++) rdesc_clear(r);
	out_str(file_s, "	STO (R2+%d),R2\n", tof+oon);	/* store old bp */
	oon += 4;
	out_str(file_s, "	LOD R4,R1+32\n"); 				/* return addr: 4*8=32 */
	out_str(file_s, "	STO (R2+%d),R4\n", tof+oon);	/* store return addr */
	oon += 4;
	out_str(file_s, "	LOD R2,R2+%d\n", tof+oon-8);	/* load new bp */
	out_str(file_s, "	JMP %s\n", (char *)b);			/* jump to new func */
	if(a != NULL)
	{
		r = reg_alloc(a, 0);
		out_str(file_s, "	LOD R%u,R%u\n", r, R_TP);	
		rdesc[r].mod = MODIFIED;
	}
	oon=0;
}

void asm_return(SYM *a)
{
	int ret_reg = R_UNDEF;
	if(a!=NULL)	 /* return value */
	{
		ret_reg = reg_alloc(a, 1);
		out_str(file_s, "\tLOD R%u,R%u\n", R_TP, ret_reg);
		syminfo_consume_use(a, current_instr_index);
	}
	for(int r=R_GEN; r < R_NUM; r++) asm_write_back(r);
	for(int r=R_GEN; r < R_NUM; r++) rdesc_clear(r);
	for(int r=R_GEN; r < R_NUM; r++) asm_write_back(r);
	for(int r=R_GEN; r < R_NUM; r++) rdesc_clear(r);

	if(a!=NULL)	 /* return value */
	{
		asm_load(R_TP, a);
	}

	out_str(file_s, "	LOD R3,(R2+4)\n");	/* return address */
	out_str(file_s, "	LOD R2,(R2)\n");	/* restore bp */
	out_str(file_s, "	JMP R3\n");			/* return */
}   

void asm_head()
{
	char head[]=
	"	# head\n"
	"	LOD R2,STACK\n"
	"	STO (R2),0\n"
	"	LOD R4,EXIT\n"
	"	STO (R2+4),R4\n";

	out_str(file_s, "%s", head);
}

void asm_tail()
{
	char tail[]=
	"\n	# tail\n"
	"EXIT:\n"
	"	END\n";

	out_str(file_s, "%s", tail);
}

void asm_str(SYM *s)
{
	char *t=s->name; /* The text */
	int i;

	out_str(file_s, "L%u:\n", s->label); /* Label for the string */
	out_str(file_s, "	DBS "); /* Label for the string */

	for(i=1; t[i + 1] !=0; i++)
	{
		if(t[i]=='\\')
		{
			switch(t[++i])
			{
				case 'n':
				out_str(file_s, "%u,", '\n');
				break;

				case '\"':
				out_str(file_s, "%u,", '\"');
				break;
			}
		}
		else out_str(file_s, "%u,", t[i]);
	}

	out_str(file_s, "0\n"); /* End of string */
}

void asm_static(void)
{
	int i;

	SYM *sl;

	for(sl=sym_tab_global; sl !=NULL; sl=sl->next)
	{
		if(sl->type==SYM_TEXT) asm_str(sl);
	}

	out_str(file_s, "STATIC:\n");
	out_str(file_s, "	DBN 0,%u\n", tos);				
	out_str(file_s, "STACK:\n");
}

void asm_code(TAC *c)
{
	int r;

	switch(c->op)
	{
		case TAC_UNDEF:
		error("cannot translate TAC_UNDEF");
		return;

		case TAC_ADD:
		asm_bin("ADD", c->a, c->b, c->c);
		return;

		case TAC_SUB:
		asm_bin("SUB", c->a, c->b, c->c);
		return;

		case TAC_MUL:
		asm_bin("MUL", c->a, c->b, c->c);
		return;

		case TAC_DIV:
		asm_bin("DIV", c->a, c->b, c->c);
		return;

		case TAC_NEG:
		asm_bin("SUB", c->a, mk_const(0), c->b);
		return;

		case TAC_EQ:
		case TAC_NE:
		case TAC_LT:
		case TAC_LE:
		case TAC_GT:
		case TAC_GE:
		asm_cmp(c->op, c->a, c->b, c->c);
		return;

		case TAC_COPY:
		r = reg_alloc(c->b, 1);
		if(c->b && c->b != c->a && syminfo_has_future_use(c->b, current_instr_index))
		{
			asm_write_back(r);
		}
		rdesc_fill(r, c->a, MODIFIED);
		syminfo_consume_use(c->b, current_instr_index);
		return;

		case TAC_INPUT:
		r=reg_alloc(c->a, 0);
		out_str(file_s, "	ITI\n");
		out_str(file_s, "	LOD R%u,R15\n", r);
		rdesc[r].mod = MODIFIED;
		return;

		case TAC_OUTPUT:
		if(c->a->type == SYM_TEXT)
		{
			r=reg_alloc(c->a, 1);
			out_str(file_s, "\tLOD R15,R%u\n", r);
			out_str(file_s, "\tOTS\n");
		}
		else
		{
			r=reg_alloc(c->a, 1);
			out_str(file_s, "\tLOD R15,R%u\n", r);
			out_str(file_s, "\tOTI\n");
		}
		syminfo_consume_use(c->a, current_instr_index);
		return;

		case TAC_GOTO:
		asm_cond("JMP", NULL, c->a->name);
		return;

		case TAC_IFZ:
		asm_cond("JEZ", c->b, c->a->name);
		return;

		case TAC_LABEL:
		for(int r=R_GEN; r < R_NUM; r++) asm_write_back(r);
		for(int r=R_GEN; r < R_NUM; r++) rdesc_clear(r);
		out_str(file_s, "%s:\n", c->a->name);
		return;

		case TAC_ACTUAL:
		r=reg_alloc(c->a, 1);
		out_str(file_s, "	STO (R2+%d),R%u\n", tof+oon, r);
		oon += 4;
		syminfo_consume_use(c->a, current_instr_index);
		return;

		case TAC_CALL:
		asm_call(c->a, c->b);
		return;

		case TAC_BEGINFUNC:
		/* We reset the top of stack, since it is currently empty apart from the link information. */
		scope=1;
		tof=LOCAL_OFF;
		oof=FORMAL_OFF;
		oon=0;
		return;

		case TAC_FORMAL:
		c->a->scope=1; /* parameter is special local var */
		c->a->offset=oof;
		oof -=4;
		return;

		case TAC_VAR:
		if(scope)
		{
			c->a->scope=1; /* local var */
			c->a->offset=tof;
			tof +=4;
		}
		else
		{
			c->a->scope=0; /* global var */
			c->a->offset=tos;
			tos +=4;
		}
		return;

		case TAC_RETURN:
		asm_return(c->a);
		return;

		case TAC_ENDFUNC:
		asm_return(NULL);
		scope=0;
		return;

		default:
		/* Don't know what this one is */
		error("unknown TAC opcode to translate");
		return;
	}
}

void tac_obj()
{
	tof=LOCAL_OFF; /* TOS allows space for link info */
	oof=FORMAL_OFF;
	oon=0;

	for(int r=0; r < R_NUM; r++) rdesc[r].var=NULL;
	
	int instr_index = 0;
	for(TAC *scan = tac_first; scan != NULL; scan = scan->next)
	{
		scan->etc = (void *)(intptr_t)instr_index;
		regalloc_record_uses(scan, instr_index);
		instr_index++;
	}
	syminfo_reset_cursors();

	asm_head();
	optlog_emit(file_s);
	deadcode_emit_report(file_s);

	TAC * cur;
	for(cur=tac_first; cur!=NULL; cur=cur->next)
	{
		current_instr_index = (int)(intptr_t)cur->etc;
		cur->etc = NULL;
		out_str(file_s, "\n	# ");
		out_tac(file_s, cur);
		out_str(file_s, "\n");
		asm_code(cur);
	}
	current_instr_index = -1;
	asm_tail();
	asm_static();
	syminfo_cleanup();
} 

