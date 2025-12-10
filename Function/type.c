#include <stdlib.h>
#include <string.h>
#include "type.h"

extern void error(const char *format, ...);

typedef struct StructRegistry {
    char *name;
    Type *type;
    struct StructRegistry *next;
} StructRegistry;

static StructRegistry *struct_registry = NULL;

static int align_to(int value, int align)
{
    if (align <= 0) return value;
    int rem = value % align;
    return rem ? (value + align - rem) : value;
}

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
    Type *int_type = type_int();
    t->size = type_size(int_type);
    t->array_len = 0;
    t->fields = NULL;
    t->field_count = 0;
    t->align = int_type->align ? int_type->align : t->size;
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

static StructRegistry *new_struct_registry(const char *name, Type *type)
{
    StructRegistry *node = (StructRegistry *)malloc(sizeof(StructRegistry));
    node->name = strdup(name);
    node->type = type;
    node->next = struct_registry;
    struct_registry = node;
    return node;
}

Type *type_struct_lookup(const char *name)
{
    for (StructRegistry *node = struct_registry; node; node = node->next) {
        if (strcmp(node->name, name) == 0) return node->type;
    }
    return NULL;
}

Type *type_struct_begin(const char *name)
{
    Type *existing = type_struct_lookup(name);
    if (existing) {
        if (existing->kind != TY_STRUCT) {
            error("type name used for non-struct");
        }
        if (existing->field_count > 0) {
            error("struct redefinition");
        }
        /* 允许在未完成定义的情况下继续补充字段 */
        existing->size = 0;
        existing->align = 1;
        return existing;
    }

    Type *t = (Type *)malloc(sizeof(Type));
    t->kind = TY_STRUCT;
    t->size = 0;
    t->base = NULL;
    t->array_len = 0;
    t->fields = NULL;
    t->field_count = 0;
    t->align = 1;
    new_struct_registry(name, t);
    return t;
}

void type_struct_add_field(Type *st, const char *field_name, Type *field_type)
{
    if (!st || st->kind != TY_STRUCT) {
        error("add field on non-struct type");
        return;
    }
    if (!field_type) {
        error("struct field has no type");
        return;
    }
    for (int i = 0; i < st->field_count; ++i) {
        if (strcmp(st->fields[i].name, field_name) == 0) {
            error("duplicate field name in struct");
        }
    }

    Field *fields = (Field *)realloc(st->fields, sizeof(Field) * (st->field_count + 1));
    if (!fields) {
        error("out of memory when adding struct field");
        return;
    }
    st->fields = fields;
    Field *fld = &st->fields[st->field_count];
    fld->name = strdup(field_name);
    fld->type = field_type;
    int align = field_type->align ? field_type->align : type_size(field_type);
    if (align <= 0) align = 1;
    int offset = align_to(st->size, align);
    fld->offset = offset;
    st->size = offset + type_size(field_type);
    if (st->align < align) st->align = align;
    st->field_count += 1;
}

Field *type_struct_get_field(Type *st, const char *field_name)
{
    if (!st || st->kind != TY_STRUCT) return NULL;
    for (int i = 0; i < st->field_count; ++i) {
        if (strcmp(st->fields[i].name, field_name) == 0) {
            return &st->fields[i];
        }
    }
    return NULL;
}

void type_struct_finalize(Type *st)
{
    if (!st || st->kind != TY_STRUCT) return;
    int align = st->align ? st->align : 1;
    st->size = align_to(st->size, align);
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
