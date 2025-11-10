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

Type *type_base(Type *t)
{
    if(!t) return NULL;
    return t->base;
}
