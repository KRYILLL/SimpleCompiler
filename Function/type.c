#include <stdlib.h>
#include "type.h"

/* 基础类型采用单例 */
static Type TY_INT_SINGLETON = { TY_INT, 4, NULL, 0, NULL, 0, 4 };
static Type TY_CHAR_SINGLETON = { TY_CHAR, 1, NULL, 0, NULL, 0, 1 };

Type *type_int(void) { return &TY_INT_SINGLETON; }
Type *type_char(void) { return &TY_CHAR_SINGLETON; }

Type *type_ptr(Type *base)
{
    Type *t = (Type*)malloc(sizeof(Type));
    t->kind = TY_PTR;
    t->base = base;
    t->size = 4;        /* 本机指针大小：与 SIZE_INT 一致 */
    t->array_len = 0;
    t->fields = NULL;
    t->field_count = 0;
    t->align = 4;
    return t;
}

Type *type_array(Type *base, int len)
{
    if(len <= 0) len = 1; /* 防御：至少 1 */
    Type *t = (Type*)malloc(sizeof(Type));
    t->kind = TY_ARRAY;
    t->base = base;          /* base 可以是元素或下一层数组 */
    t->array_len = len;
    t->fields = NULL;
    t->field_count = 0;
    t->align = base ? base->align : 1;
    int elem_size = base ? base->size : 0;
    long long total = (long long)elem_size * (long long)len; /* 防止溢出 */
    if(total > 0x7fffffff) total = 0x7fffffff; /* 简单截断 */
    t->size = (int)total;
    return t;
}

int type_size(Type *t)
{
    if(!t) return 0;
    return t->size;
}

int type_is_char(Type *t)
{
    if(!t) return 0;
    return t->kind == TY_CHAR;
}

int type_is_ptr(Type *t)
{
    return t && t->kind == TY_PTR;
}

int type_is_array(Type *t)
{
    return t && t->kind == TY_ARRAY;
}

Type *type_base(Type *t)
{
    if(!t) return NULL;
    return t->base;
}

int type_array_len(Type *t)
{
    if(!t || t->kind != TY_ARRAY) return 0;
    return t->array_len;
}

int type_elem_size(Type *t)
{
    /* 递归追到底层非数组/指针的基础元素大小 */
    while(t && (t->kind == TY_ARRAY || t->kind == TY_PTR)) t = t->base;
    return t ? t->size : 0;
}
