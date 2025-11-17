#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include "tac.h"
#include "type.h"

/* global var */
int scope, next_tmp, next_label;
SYM *sym_tab_global, *sym_tab_local;
TAC *tac_first, *tac_last;

void tac_init()
{
	scope=0;
	sym_tab_global=NULL;
	sym_tab_local=NULL;	
	next_tmp=0;
	next_label=1;
}

void tac_complete()
{
	TAC *cur=NULL; 		/* Current TAC */
	TAC *prev=tac_last; 	/* Previous TAC */

	while(prev !=NULL)
	{
		prev->next=cur;
		cur=prev;
		prev=prev->prev;
	}

	tac_first = cur;
}

SYM *lookup_sym(SYM *symtab, char *name)
{
	SYM *t=symtab;

	while(t !=NULL)
	{
		if(strcmp(t->name, name)==0) break; 
		else t=t->next;
	}
	
	return t; /* NULL if not found */
}

void insert_sym(SYM **symtab, SYM *sym)
{
	sym->next=*symtab; /* Insert at head */
	*symtab=sym;
}

SYM *mk_sym(void)
{
	SYM *t;
	t=(SYM *)malloc(sizeof(SYM));
	return t;
}

SYM *mk_var_with_type(int dtype, char *name)
{
	SYM *sym=NULL;

	if(scope)  
		sym=lookup_sym(sym_tab_local,name);
	else
		sym=lookup_sym(sym_tab_global,name);

	/* var already declared */
	if(sym!=NULL)
	{
		error("variable already declared");
		return NULL;
	}

	/* var unseen before, set up a new symbol table node, insert_sym it into the symbol table. */
	sym=mk_sym();
	sym->type=SYM_VAR;
	sym->name=name;
	sym->offset=-1; /* Unset address */
	/* 统一类型：仅保存 Type* */
	sym->ty = (dtype == DTYPE_CHAR) ? type_char() : type_int();

	if(scope)  
		insert_sym(&sym_tab_local,sym);
	else
		insert_sym(&sym_tab_global,sym);

	return sym;
}

TAC *join_tac(TAC *c1, TAC *c2)
{
	TAC *t;

	if(c1==NULL) return c2;
	if(c2==NULL) return c1;

	/* Run down c2, until we get to the beginning and then add c1 */
	t=c2;
	while(t->prev !=NULL) 
		t=t->prev;

	t->prev=c1;
	return c2;
}

TAC *declare_var_with_type(int dtype, char *name)
{
	SYM *s = mk_var_with_type(dtype,name);
	if (s) {
		s->ty = (dtype == DTYPE_CHAR) ? type_char() : type_int();
	}
	return mk_tac(TAC_VAR, s, NULL, NULL);
}

TAC *declare_ptr_var(int base_dtype, char *name)
{
	Type *base = (base_dtype == DTYPE_CHAR) ? type_char() : type_int();
	return declare_var_type(type_ptr(base), name);
}

SYM *mk_char_const(int c)
{
	SYM *sym = NULL;
	char name[16];
	sprintf(name, "c%d", c); /* key for char constants */

	sym = lookup_sym(sym_tab_global, name);
	if (sym != NULL) return sym;

	sym = mk_sym();
	sym->type = SYM_CHAR; /* keep as numeric literal category */
	sym->value = c;
	sym->name = strdup(name);
	sym->ty = type_char();
	insert_sym(&sym_tab_global, sym);

	return sym;
}

TAC *mk_tac(int op, SYM *a, SYM *b, SYM *c)
{
	TAC *t=(TAC *)malloc(sizeof(TAC));

	t->next=NULL; /* Set these for safety */
	t->prev=NULL;
	t->op=op;
	t->a=a;
	t->b=b;
	t->c=c;

	return t;
}  

SYM *mk_label(char *name)
{
	SYM *t=mk_sym();

	t->type=SYM_LABEL;
	t->name=strdup(name);

	return t;
}  

TAC *do_func(SYM *func, TAC *args, TAC *code)
{
	TAC *tlist; /* The backpatch list */

	TAC *tlab; /* Label at start of function */
	TAC *tbegin; /* BEGINFUNC marker */
	TAC *tend; /* ENDFUNC marker */

	tlab=mk_tac(TAC_LABEL, mk_label(func->name), NULL, NULL);
	tbegin=mk_tac(TAC_BEGINFUNC, NULL, NULL, NULL);
	tend=mk_tac(TAC_ENDFUNC,   NULL, NULL, NULL);

	tbegin->prev=tlab;
	code=join_tac(args, code);
	tend->prev=join_tac(tbegin, code);

	return tend;
}

SYM *mk_tmp(void)
{
	SYM *sym;
	char *name;

	name=malloc(12);
	sprintf(name, "t%d", next_tmp++); /* Set up text */
	return mk_var_with_type(DTYPE_INT,name);
}

SYM *mk_tmp_of_type(Type *t)
{
	SYM *sym;
	char *name;
	name = (char*)malloc(12);
	sprintf(name, "t%d", next_tmp++);
	int dtype = (t && t->kind == TY_CHAR) ? DTYPE_CHAR : DTYPE_INT;
	sym = mk_var_with_type(dtype, name);
	if (sym) {
		sym->ty = t ? t : type_int();
	}
	return sym;
}

/* mk_tmp_with 已弃用：统一类型系统直接使用 mk_tmp_of_type */

static EXP *reverse_exp_list(EXP *head)
{
	EXP *prev=NULL, *cur=head;
	while(cur){ EXP *n=cur->next; cur->next=prev; prev=cur; cur=n; }
	return prev;
}

TAC *declare_var_type(Type *ty, char *name)
{
	SYM *s = mk_var_with_type(DTYPE_INT, name); /* dtype 占位 */
	if (s) s->ty = ty ? ty : type_int();
	return mk_tac(TAC_VAR, s, NULL, NULL);
}

TAC *declare_para_type(Type *ty, char *name)
{
	SYM *s = mk_var_with_type(DTYPE_INT, name);
	if (s) s->ty = ty ? ty : type_int();
	return mk_tac(TAC_FORMAL, s, NULL, NULL);
}

TAC *declare_array_var_dims(int base_dtype, char *name, int *dims, int ndims)
{
	Type *t = (base_dtype == DTYPE_CHAR) ? type_char() : type_int();
	/* 从内层到外层构造：arr[a][b] => array(len=a) of array(len=b) of base? 实际 C 是外到内
	   这里按外->内构造：t = array(t, dims[0]); t = array(t, dims[1]); ... */
	for (int i = 0; i < ndims; ++i) {
		t = type_array(t, dims[i]);
	}
	return declare_var_type(t, name);
}

SYM *declare_func(char *name)
{
	SYM *sym=NULL;

	sym=lookup_sym(sym_tab_global,name);

	/* name used before declared */
	if(sym!=NULL)
	{
		if(sym->type==SYM_FUNC)
		{
			error("func already declared");
			return NULL;
		}

		if(sym->type !=SYM_UNDEF)
		{
			error("func name already used");
			return NULL;
		}

		return sym;
	}
	
	
	sym=mk_sym();
	sym->type=SYM_FUNC;
	sym->name=name;
	sym->address=NULL;

	insert_sym(&sym_tab_global,sym);
	return sym;
}

TAC *do_assign(SYM *var, EXP *exp)
{
	TAC *code;

	if(var->type !=SYM_VAR) error("assignment to non-variable");

	code=mk_tac(TAC_COPY, var, exp->ret, NULL);
	code->prev=exp->tac;

	return code;
}

TAC *do_input(SYM *var)
{
	TAC *code;

	if(var->type !=SYM_VAR) error("input to non-variable");

	code=mk_tac(TAC_INPUT, var, NULL, NULL);

	return code;
}

TAC *do_output(SYM *s)
{
	TAC *code;

	code=mk_tac(TAC_OUTPUT, s, NULL, NULL);

	return code;
}

EXP *do_bin( int binop, EXP *exp1, EXP *exp2)
{
	TAC *temp; /* TAC code for temp symbol */
	TAC *ret; /* TAC code for result */

	/*
	if((exp1->ret->type==SYM_INT) && (exp2->ret->type==SYM_INT))
	{
		int newval;

		switch(binop)
		{
			case TAC_ADD:
			newval=exp1->ret->value + exp2->ret->value;
			break;

			case TAC_SUB:
			newval=exp1->ret->value - exp2->ret->value;
			break;

			case TAC_MUL:
			newval=exp1->ret->value * exp2->ret->value;
			break;

			case TAC_DIV:
			newval=exp1->ret->value / exp2->ret->value;
			break;
		}

		exp1->ret=mk_int_const(newval);

		return exp1;
	}
	*/

	temp=mk_tac(TAC_VAR, mk_tmp(), NULL, NULL);
	temp->prev=join_tac(exp1->tac, exp2->tac);
	ret=mk_tac(binop, temp->a, exp1->ret, exp2->ret);
	ret->prev=temp;

	exp1->ret=temp->a;
	exp1->tac=ret;

	return exp1;  
}   

EXP *do_cmp( int binop, EXP *exp1, EXP *exp2)
{
	TAC *temp; /* TAC code for temp symbol */
	TAC *ret; /* TAC code for result */

	temp=mk_tac(TAC_VAR, mk_tmp(), NULL, NULL);
	temp->prev=join_tac(exp1->tac, exp2->tac);
	ret=mk_tac(binop, temp->a, exp1->ret, exp2->ret);
	ret->prev=temp;

	exp1->ret=temp->a;
	exp1->tac=ret;

	return exp1;  
}   

EXP *do_un( int unop, EXP *exp) 
{
	TAC *temp; /* TAC code for temp symbol */
	TAC *ret; /* TAC code for result */

	temp=mk_tac(TAC_VAR, mk_tmp(), NULL, NULL);
	temp->prev=exp->tac;
	ret=mk_tac(unop, temp->a, exp->ret, NULL);
	ret->prev=temp;

	exp->ret=temp->a;
	exp->tac=ret;

	return exp;   
}

/* a = &var */
EXP *do_addr(SYM *var)
{
	/* 结果类型是指向 var->ty 的指针 */
	SYM *ret = mk_tmp_of_type(type_ptr(var->ty));
	TAC *tvar = mk_tac(TAC_VAR, ret, NULL, NULL);
	TAC *taddr = mk_tac(TAC_ADDR, ret, var, NULL);
	taddr->prev = tvar;
	return mk_exp(NULL, ret, taddr);
}

/* a = *addr */
EXP *do_deref(EXP *addr)
{
	Type *elem = type_int();
	if (addr && addr->ret && addr->ret->ty && type_is_ptr(addr->ret->ty) && type_base(addr->ret->ty)) {
		elem = type_base(addr->ret->ty);
	}
	SYM *ret = mk_tmp_of_type(elem);
	TAC *tvar = mk_tac(TAC_VAR, ret, NULL, NULL);
	tvar->prev = addr->tac;
	TAC *tld = mk_tac(TAC_LOAD, ret, addr->ret, NULL);
	tld->prev = tvar;
	addr->ret = ret;
	addr->tac = tld;
	return addr;
}

/* *addr = rhs */
TAC *do_store(EXP *addr, EXP *rhs)
{
	TAC *code = join_tac(addr->tac, rhs->tac);
	TAC *ts = mk_tac(TAC_STORE, addr->ret, rhs->ret, NULL);
	ts->prev = code;
	return ts;
}

static int collect_dims(Type *t, int *buf, int maxn)
{
	int n=0;
	while (t && t->kind == TY_ARRAY && n < maxn) { buf[n++] = t->array_len; t = t->base; }
	return n;
}

/* 指针加法：结果类型为 resultTyPtr */
static EXP *do_ptr_add(EXP *ptr, EXP *off, Type *resultTyPtr)
{
	SYM *ret = mk_tmp_of_type(resultTyPtr);
	TAC *tvar = mk_tac(TAC_VAR, ret, NULL, NULL);
	TAC *code = join_tac(ptr ? ptr->tac : NULL, off ? off->tac : NULL);
	tvar->prev = code;
	TAC *tadd = mk_tac(TAC_ADD, ret, ptr->ret, off->ret);
	tadd->prev = tvar;
	return mk_exp(NULL, ret, tadd);
}

typedef struct {
	EXP *addr;
	Type *ty;
} AccessEval;

static AccessEval access_path_eval_internal(AccessPath *path)
{
	AccessEval result = { NULL, NULL };
	if (!path || !path->base) {
		error("invalid lvalue");
		return result;
	}
	Type *curType = path->base->ty;
	if (!curType) curType = type_int();
	EXP *addr = do_addr(path->base);
	AccessPathStep *step = path->head;
	while (step) {
		if (!curType) {
			error("invalid type in access path");
			curType = type_int();
		}
		switch (step->kind) {
		case ACCESS_STEP_FIELD:
		{
			if (curType->kind != TY_STRUCT) {
				error("field access on non-struct");
			}
			Field *fld = type_struct_get_field(curType, step->field_name);
			if (!fld) {
				error("unknown struct field");
			}
			Type *fieldType = fld ? fld->type : type_int();
			int offset = fld ? fld->offset : 0;
			if (offset != 0) {
				EXP *offExp = mk_exp(NULL, mk_int_const(offset), NULL);
				addr = do_ptr_add(addr, offExp, type_ptr(fieldType));
			} else {
				if (addr && addr->ret) addr->ret->ty = type_ptr(fieldType);
			}
			curType = fieldType;
			break;
		}
		case ACCESS_STEP_INDEX:
		{
			Type *elemType = NULL;
			if (curType->kind == TY_ARRAY) elemType = curType->base;
			else if (curType->kind == TY_PTR) elemType = curType->base;
			else {
				error("indexing non-array");
				elemType = type_int();
			}
			if (!elemType) elemType = type_int();
			EXP *idxExp = step->index_exp;
			if (!idxExp) idxExp = mk_exp(NULL, mk_int_const(0), NULL);
			int stride = type_size(elemType);
			if (stride > 1) {
				EXP *strideExp = mk_exp(NULL, mk_int_const(stride), NULL);
				idxExp = do_bin(TAC_MUL, idxExp, strideExp);
			}
			addr = do_ptr_add(addr, idxExp, type_ptr(elemType));
			curType = elemType;
			break;
		}
		}
		step = step->next;
	}
	if (addr && addr->ret) addr->ret->ty = type_ptr(curType);
	result.addr = addr;
	result.ty = curType;
	return result;
}

AccessPath *access_path_new(SYM *base)
{
	if (!base) return NULL;
	AccessPath *path = (AccessPath *)malloc(sizeof(AccessPath));
	path->base = base;
	path->head = NULL;
	path->tail = NULL;
	return path;
}

static void access_path_append_step(AccessPath *path, AccessPathStep *step)
{
	if (!path || !step) return;
	step->next = NULL;
	if (path->tail) {
		path->tail->next = step;
		path->tail = step;
	} else {
		path->head = path->tail = step;
	}
}

AccessPath *access_path_append_field(AccessPath *path, char *field_name)
{
	AccessPathStep *step = (AccessPathStep *)malloc(sizeof(AccessPathStep));
	step->kind = ACCESS_STEP_FIELD;
	step->field_name = field_name;
	step->index_exp = NULL;
	step->next = NULL;
	access_path_append_step(path, step);
	return path;
}

AccessPath *access_path_append_index(AccessPath *path, EXP *index_exp)
{
	AccessPathStep *step = (AccessPathStep *)malloc(sizeof(AccessPathStep));
	step->kind = ACCESS_STEP_INDEX;
	step->field_name = NULL;
	step->index_exp = index_exp;
	step->next = NULL;
	access_path_append_step(path, step);
	return path;
}

EXP *access_path_address(AccessPath *path)
{
	AccessEval eval = access_path_eval_internal(path);
	return eval.addr;
}

EXP *access_path_load(AccessPath *path)
{
	AccessEval eval = access_path_eval_internal(path);
	Type *ty = eval.ty;
	if (!ty) return mk_exp(NULL, NULL, NULL);
	if (ty->kind == TY_ARRAY) {
		if (eval.addr && eval.addr->ret) {
			Type *elem = ty->base;
			eval.addr->ret->ty = type_ptr(elem);
		}
		return eval.addr;
	}
	if (ty->kind == TY_STRUCT) {
		error("cannot use struct value in expression");
		return mk_exp(NULL, NULL, NULL);
	}
	return do_deref(eval.addr);
}

TAC *access_path_store(AccessPath *path, EXP *rhs)
{
	AccessEval eval = access_path_eval_internal(path);
	Type *ty = eval.ty;
	if (ty && (ty->kind == TY_STRUCT || ty->kind == TY_ARRAY)) {
		error("invalid assignment target");
	}
	return do_store(eval.addr, rhs);
}

/* 生成从数组名与下标列表到元素地址的表达式（返回指向元素的指针） */
static EXP *do_array_address(SYM *arr, EXP *indices)
{
	if (!arr || !arr->ty || arr->ty->kind != TY_ARRAY) {
		error("indexing a non-array variable");
		return mk_exp(NULL, NULL, NULL);
	}
	/* 下标列表顺序修正为外->内 */
	indices = reverse_exp_list(indices);

	/* 收集维度 */
	int dims[16];
	int nd = collect_dims(arr->ty, dims, 16);

	/* 当前类型构造顺序使得最外层维度在链表尾部，所以 collect_dims 得到的顺序是内->外，需要反转 */
	for (int i=0; i<nd/2; ++i) {
		int tmp = dims[i];
		dims[i] = dims[nd-1-i];
		dims[nd-1-i] = tmp;
	}

	/* 收集索引表达式到数组，拼接其 TAC */
	EXP *idxs[16];
	int ni=0;
	TAC *code=NULL;
	for (EXP *p=indices; p; p=p->next) {
		if (ni>=16) error("too many indices");
		idxs[ni++] = p;
		code = join_tac(code, p->tac);
	}
	if (ni != nd) {
		error("array index dimension mismatch");
	}

	/* 线性化：off = i0; for k=1..n-1: off = off*dim[k] + ik */
	EXP *offExp = idxs[0];
	for (int k=1; k<ni; ++k) {
		EXP *dimk = mk_exp(NULL, mk_int_const(dims[k]), NULL);
		offExp = do_bin(TAC_MUL, offExp, dimk);
		offExp = do_bin(TAC_ADD, offExp, idxs[k]);
	}

	/* 字节偏移 */
	int esize = type_elem_size(arr->ty);
	if (esize > 1) {
		EXP *esz = mk_exp(NULL, mk_int_const(esize), NULL);
		offExp = do_bin(TAC_MUL, offExp, esz);
	}

	/* 基址：&arr ，目的类型应为指向最内层元素 */
	Type *elem = arr->ty;
	while (elem && elem->kind == TY_ARRAY) elem = elem->base;
	EXP *base = do_addr(arr);
	/* 调整 base 返回指针类型为指向元素，而不是指向数组 */
	if (base && base->ret) base->ret->ty = type_ptr(elem);

	/* 结果地址 */
	return do_ptr_add(base, offExp, type_ptr(elem));
}

EXP *do_array_access(char *name, EXP *indices, int is_lvalue)
{
	SYM *arr = get_var(name);
	EXP *addr = do_array_address(arr, indices);
	if (is_lvalue) return addr;
	return do_deref(addr);
}

EXP *link_index_exp(EXP *head, EXP *idx)
{
	if (!idx) return head;
	idx->next = head;
	return idx;
}

TAC *do_array_store(SYM *arr, EXP *indices, EXP *rhs)
{
	EXP *addr = do_array_address(arr, indices);
	return do_store(addr, rhs);
}

TAC *do_call(char *name, EXP *arglist)
{
	EXP  *alt; /* For counting args */
	TAC *code; /* Resulting code */
	TAC *temp; /* Temporary for building code */

	code=NULL;
	for(alt=arglist; alt !=NULL; alt=alt->next) code=join_tac(code, alt->tac);

	while(arglist !=NULL) /* Generate ARG instructions */
	{
		temp=mk_tac(TAC_ACTUAL, arglist->ret, NULL, NULL);
		temp->prev=code;
		code=temp;

		alt=arglist->next;
		arglist=alt;
	};

	temp=mk_tac(TAC_CALL, NULL, (SYM *)strdup(name), NULL);
	temp->prev=code;
	code=temp;

	return code;
}

EXP *do_call_ret(char *name, EXP *arglist)
{
	EXP  *alt; /* For counting args */
	SYM *ret; /* Where function result will go */
	TAC *code; /* Resulting code */
	TAC *temp; /* Temporary for building code */

	ret=mk_tmp(); /* For the result */
	code=mk_tac(TAC_VAR, ret, NULL, NULL);

	for(alt=arglist; alt !=NULL; alt=alt->next) code=join_tac(code, alt->tac);

	while(arglist !=NULL) /* Generate ARG instructions */
	{
		temp=mk_tac(TAC_ACTUAL, arglist->ret, NULL, NULL);
		temp->prev=code;
		code=temp;

		alt=arglist->next;
		arglist=alt;
	};

	temp=mk_tac(TAC_CALL, ret, (SYM *)strdup(name), NULL);
	temp->prev=code;
	code=temp;

	return mk_exp(NULL, ret, code);
}

char *mk_lstr(int i)
{
	char lstr[10]="L";
	sprintf(lstr,"L%d",i);
	return(strdup(lstr));	
}

TAC *do_if(EXP *exp, TAC *stmt)
{
	TAC *label=mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
	TAC *code=mk_tac(TAC_IFZ, label->a, exp->ret, NULL);

	code->prev=exp->tac;
	code=join_tac(code, stmt);
	label->prev=code;

	return label;
}

TAC *do_test(EXP *exp, TAC *stmt1, TAC *stmt2)
{
	TAC *label1=mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
	TAC *label2=mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
	TAC *code1=mk_tac(TAC_IFZ, label1->a, exp->ret, NULL);
	TAC *code2=mk_tac(TAC_GOTO, label2->a, NULL, NULL);

	code1->prev=exp->tac; /* Join the code */
	code1=join_tac(code1, stmt1);
	code2->prev=code1;
	label1->prev=code2;
	label1=join_tac(label1, stmt2);
	label2->prev=label1;
	
	return label2;
}

TAC *do_while(EXP *exp, TAC *stmt) 
{
	TAC *label=mk_tac(TAC_LABEL, mk_label(mk_lstr(next_label++)), NULL, NULL);
	TAC *code=mk_tac(TAC_GOTO, label->a, NULL, NULL);

	code->prev=stmt; /* Bolt on the goto */

	return join_tac(label, do_if(exp, code));
}

SYM *get_var(char *name)
{
	SYM *sym=NULL; /* Pointer to looked up symbol */

	if(scope) sym=lookup_sym(sym_tab_local,name);

	if(sym==NULL) sym=lookup_sym(sym_tab_global,name);

	if(sym==NULL)
	{
		error("name not declared as local/global variable");
		return NULL;
	}

	if(sym->type!=SYM_VAR)
	{
		error("not a variable");
		return NULL;
	}

	return sym;
} 

EXP *mk_exp(EXP *next, SYM *ret, TAC *code)
{
	EXP *exp=(EXP *)malloc(sizeof(EXP));

	exp->next=next;
	exp->ret=ret;
	exp->tac=code;

	return exp;
}

SYM *mk_text(char *text)
{
	SYM *sym=NULL; /* Pointer to looked up symbol */

	sym=lookup_sym(sym_tab_global,text);

	/* text already used */
	if(sym!=NULL)
	{
		return sym;
	}

	/* text unseen before */
	sym=mk_sym();
	sym->type=SYM_TEXT;
	sym->name=text;
	sym->label=next_label++;

	insert_sym(&sym_tab_global,sym);
	return sym;
}

SYM *mk_int_const(int n)
{
	SYM *sym=NULL;

	char name[10];
	sprintf(name, "%d", n);

	sym=lookup_sym(sym_tab_global, name);
	if(sym!=NULL)
	{
		return sym;
	}

	sym=mk_sym();
	sym->type=SYM_INT;
	sym->value=n;
	sym->name=strdup(name);
	sym->ty = type_int();
	insert_sym(&sym_tab_global,sym);

	return sym;
}     

char *to_str(SYM *s, char *str) 
{
	if(s==NULL)	return "NULL";

	switch(s->type)
	{
		case SYM_FUNC:
		case SYM_VAR:
		/* Just return the name */
		return s->name;

		case SYM_TEXT:
		/* Put the address of the text */
		sprintf(str, "L%d", s->label);
		return str;

		case SYM_INT:
		/* Convert the number to string */
			sprintf(str, "%d", s->value);
		return str;

		case SYM_CHAR:
		sprintf(str, "'%c'", (char)s->value);
		return str;

		default:
		/* Unknown arg type */
		error("unknown TAC arg type");
		return "?";
	}
} 

void out_str(FILE *f, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(f, format, args);
    va_end(args);
}

void out_sym(FILE *f, SYM *s)
{
	out_str(f, "%p\t%s", s, s->name);
}

void out_tac(FILE *f, TAC *i)
{
	char sa[12]; /* For text of TAC args */
	char sb[12];
	char sc[12];

	switch(i->op)
	{
		case TAC_UNDEF:
		fprintf(f, "undef");
		break;

		case TAC_ADD:
		fprintf(f, "%s = %s + %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_SUB:
		fprintf(f, "%s = %s - %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_MUL:
		fprintf(f, "%s = %s * %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_DIV:
		fprintf(f, "%s = %s / %s", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_EQ:
		fprintf(f, "%s = (%s == %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_NE:
		fprintf(f, "%s = (%s != %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_LT:
		fprintf(f, "%s = (%s < %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_LE:
		fprintf(f, "%s = (%s <= %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_GT:
		fprintf(f, "%s = (%s > %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_GE:
		fprintf(f, "%s = (%s >= %s)", to_str(i->a, sa), to_str(i->b, sb), to_str(i->c, sc));
		break;

		case TAC_NEG:
		fprintf(f, "%s = - %s", to_str(i->a, sa), to_str(i->b, sb));
		break;

		case TAC_COPY:
		fprintf(f, "%s = %s", to_str(i->a, sa), to_str(i->b, sb));
		break;

		case TAC_GOTO:
		fprintf(f, "goto %s", i->a->name);
		break;

		case TAC_IFZ:
		fprintf(f, "ifz %s goto %s", to_str(i->b, sb), i->a->name);
		break;

		case TAC_ACTUAL:
		fprintf(f, "actual %s", to_str(i->a, sa));
		break;

		case TAC_FORMAL:
		fprintf(f, "formal %s", to_str(i->a, sa));
		break;

		case TAC_CALL:
		if(i->a==NULL) fprintf(f, "call %s", (char *)i->b);
		else fprintf(f, "%s = call %s", to_str(i->a, sa), (char *)i->b);
		break;

		case TAC_INPUT:
		fprintf(f, "input %s", to_str(i->a, sa));
		break;

		case TAC_OUTPUT:
		fprintf(f, "output %s", to_str(i->a, sa));
		break;

		case TAC_ADDR:
		fprintf(f, "%s = &%s", to_str(i->a, sa), to_str(i->b, sb));
		break;

		case TAC_LOAD:
		fprintf(f, "%s = *%s", to_str(i->a, sa), to_str(i->b, sb));
		break;

		case TAC_STORE:
		fprintf(f, "*%s = %s", to_str(i->a, sa), to_str(i->b, sb));
		break;

		case TAC_RETURN:
		fprintf(f, "return %s", to_str(i->a, sa));
		break;

		case TAC_LABEL:
		fprintf(f, "label %s", i->a->name);
		break;

		case TAC_VAR:
		fprintf(f, "var %s", to_str(i->a, sa));
		break;

		case TAC_BEGINFUNC:
		fprintf(f, "begin");
		break;

		case TAC_ENDFUNC:
		fprintf(f, "end");
		break;

		default:
		error("unknown TAC opcode");
		break;
	}
}
