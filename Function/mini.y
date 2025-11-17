%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tac.h"

int current_decl_dtype;
Type *current_decl_type;
Type *current_struct_def;

int yylex();
void yyerror(char* msg);
%}

%union
{
	char character;
	char *string;
	SYM *sym;
	TAC *tac;
	EXP	*exp;
	struct Type *ty;
	AccessPath *path;
}

%token INT CHAR STRUCT EQ NE LT LE GT GE UMINUS IF ELSE WHILE FUNC INPUT OUTPUT RETURN
%token <string> INTEGER IDENTIFIER TEXT
%token <character> CHARCONST

%left EQ NE LT LE GT GE
%left '+' '-'
%left '*' '/'
%right UMINUS

%type <tac> program function_declaration_list function_declaration function parameter_list variable_list statement assignment_statement return_statement if_statement while_statement call_statement block declaration_list declaration statement_list input_statement output_statement struct_definition struct_field_declaration struct_field_list struct_field_declarator_list struct_field_declarator
%type <exp> argument_list expression_list expression call_expression
%type <sym> function_head
%type <ty> arr_decl_dims type
%type <path> lvalue

%%

program : function_declaration_list
{
	tac_last = $1;
	tac_complete();
}
;

function_declaration_list : function_declaration
| function_declaration_list function_declaration
{
	$$ = join_tac($1, $2);
}
;

function_declaration : function
| declaration
| struct_definition
;

type : INT
{
	current_decl_dtype = DTYPE_INT;
	current_decl_type = type_int();
	$$ = current_decl_type;
}
| CHAR
{
	current_decl_dtype = DTYPE_CHAR;
	current_decl_type = type_char();
	$$ = current_decl_type;
}
| STRUCT IDENTIFIER
{
	Type *st = type_struct_lookup($2);
	if (!st) {
		error("struct type not defined");
		st = type_int();
	}
	current_decl_dtype = DTYPE_INT;
	current_decl_type = st;
	$$ = st;
}//当前decl_type 设置为 struct 类型
;

declaration : type variable_list ';'
{
	$$ = $2;
}
;

struct_definition : STRUCT IDENTIFIER
{
	current_struct_def = type_struct_begin($2);
}
'{' struct_field_list '}' ';'
{
	type_struct_finalize(current_struct_def);
	current_struct_def = NULL;
	$$ = NULL;
}
;

struct_field_list :
{
	$$ = NULL;
}
| struct_field_list struct_field_declaration
{
	$$ = NULL;
}
;

struct_field_declaration : type struct_field_declarator_list ';'
{
	$$ = NULL;
}
;

struct_field_declarator_list : struct_field_declarator
{
	$$ = NULL;
}
| struct_field_declarator_list ',' struct_field_declarator
{
	$$ = NULL;
}
;

struct_field_declarator : IDENTIFIER
{
	type_struct_add_field(current_struct_def, $1, current_decl_type);
	$$ = NULL;
}
| '*' IDENTIFIER
{
	type_struct_add_field(current_struct_def, $2, type_ptr(current_decl_type));
	$$ = NULL;
}
| IDENTIFIER arr_decl_dims
{
	Type *ty = $2 ? $2 : current_decl_type;
	type_struct_add_field(current_struct_def, $1, ty);
	$$ = NULL;
}
;

variable_list : IDENTIFIER
{
	$$ = declare_var_type(current_decl_type, $1);
}
| '*' IDENTIFIER
{
	$$ = declare_var_type(type_ptr(current_decl_type), $2);
}
| IDENTIFIER arr_decl_dims
{
	Type *ty = $2 ? $2 : current_decl_type;
	$$ = declare_var_type(ty, $1);
}
| variable_list ',' IDENTIFIER
{
	$$ = join_tac($1, declare_var_type(current_decl_type, $3));
}
| variable_list ',' '*' IDENTIFIER
{
	$$ = join_tac($1, declare_var_type(type_ptr(current_decl_type), $4));
}
| variable_list ',' IDENTIFIER arr_decl_dims
{
	Type *ty = $4 ? $4 : current_decl_type;
	$$ = join_tac($1, declare_var_type(ty, $3));
}
;

function : function_head '(' parameter_list ')' block
{
	$$ = do_func($1, $3, $5);
	scope = 0;
	sym_tab_local = NULL;
}
| error
{
	error("Bad function syntax");
	$$ = NULL;
}
;

function_head : IDENTIFIER
{
	$$ = declare_func($1);
	scope = 1;
	sym_tab_local = NULL;
}
;

parameter_list : type IDENTIFIER
{
	$$ = declare_para_type(current_decl_type, $2);
}
| type '*' IDENTIFIER
{
	$$ = declare_para_type(type_ptr(current_decl_type), $3);
}
| parameter_list ',' type IDENTIFIER
{
	$$ = join_tac($1, declare_para_type(current_decl_type, $4));
}
| parameter_list ',' type '*' IDENTIFIER
{
	$$ = join_tac($1, declare_para_type(type_ptr(current_decl_type), $5));
}
|
{
	$$ = NULL;
}
;

statement : assignment_statement ';'
| input_statement ';'
| output_statement ';'
| call_statement ';'
| return_statement ';'
| if_statement
| while_statement
| block
| error
{
	error("Bad statement syntax");
	$$=NULL;
}
;

block : '{' declaration_list statement_list '}'
{
	$$=join_tac($2, $3);
}               
;

declaration_list        :
{
	$$=NULL;
}
| declaration_list declaration
{
	$$=join_tac($1, $2);
}
;

statement_list : statement
| statement_list statement
{
	$$=join_tac($1, $2);
}               
;

assignment_statement : lvalue '=' expression
{
	$$ = access_path_store($1, $3);
}
| '*' expression '=' expression
{
	$$ = do_store($2, $4);
}
;

lvalue : IDENTIFIER
{
	$$ = access_path_new(get_var($1));
}
| lvalue '.' IDENTIFIER
{
	$$ = access_path_append_field($1, $3);
}
| lvalue '[' expression ']'
{
	$$ = access_path_append_index($1, $3);
}
;

expression : expression '+' expression
{
	$$=do_bin(TAC_ADD, $1, $3);
}
| expression '-' expression
{
	$$=do_bin(TAC_SUB, $1, $3);
}
| expression '*' expression
{
	$$=do_bin(TAC_MUL, $1, $3);
}
| expression '/' expression
{
	$$=do_bin(TAC_DIV, $1, $3);
}
| '-' expression  %prec UMINUS
{
	$$=do_un(TAC_NEG, $2);
}
| '*' expression %prec UMINUS
{
	$$=do_deref($2);
}
| expression EQ expression
{
	$$=do_cmp(TAC_EQ, $1, $3);
}
| expression NE expression
{
	$$=do_cmp(TAC_NE, $1, $3);
}
| expression LT expression
{
	$$=do_cmp(TAC_LT, $1, $3);
}
| expression LE expression
{
	$$=do_cmp(TAC_LE, $1, $3);
}
| expression GT expression
{
	$$=do_cmp(TAC_GT, $1, $3);
}
| expression GE expression
{
	$$=do_cmp(TAC_GE, $1, $3);
}
| '(' expression ')'
{
	$$=$2;
}               
| INTEGER
{
	$$=mk_exp(NULL, mk_int_const(atoi($1)), NULL);
}
| CHARCONST
{
	$$=mk_exp(NULL, mk_char_const($1), NULL);
}
| lvalue
{
	$$ = access_path_load($1);
}
| '&' lvalue
{
	$$ = access_path_address($2);
}
| call_expression
{
	$$=$1;
}               
| error
{
	error("Bad expression syntax");
	$$=mk_exp(NULL, NULL, NULL);
}
;

argument_list           :
{
	$$=NULL;
}
| expression_list
;

expression_list : expression
|  expression_list ',' expression
{
	$3->next=$1;
	$$=$3;
}
;

input_statement : INPUT IDENTIFIER
{
	$$=do_input(get_var($2));
}
;

output_statement : OUTPUT IDENTIFIER
{
	$$=do_output(get_var($2));
}
| OUTPUT TEXT
{
	$$=do_output(mk_text($2));
}
;

return_statement : RETURN expression
{
	TAC *t=mk_tac(TAC_RETURN, $2->ret, NULL, NULL);
	t->prev=$2->tac;
	$$=t;
}               
;

if_statement : IF '(' expression ')' block
{
	$$=do_if($3, $5);
}
| IF '(' expression ')' block ELSE block
{
	$$=do_test($3, $5, $7);
}
;

while_statement : WHILE '(' expression ')' block
{
	$$=do_while($3, $5);
}               
;

call_statement : IDENTIFIER '(' argument_list ')'
{
	$$=do_call($1, $3);
}
;

call_expression : IDENTIFIER '(' argument_list ')'
{
	$$=do_call_ret($1, $3);
}
;

/* 数组：下标列表 [expr][expr]... 构成一个链表（头结点是最后一个表达式），在 TAC 里会反转顺序 */
/* 数组声明维度：构造成 Type*，按外->内逐层包装 */
arr_decl_dims : '[' INTEGER ']'
{
	$$ = type_array(current_decl_type, atoi($2));
}
| arr_decl_dims '[' INTEGER ']'
{
	$$ = type_array($1, atoi($3));
}
;

%%

void yyerror(char* msg) 
{
	fprintf(stderr, "%s: line %d\n", msg, yylineno);
	exit(0);
}
