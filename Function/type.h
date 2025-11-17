#ifndef TYPE_H
#define TYPE_H

/* 统一类型系统的最小实现：仅覆盖 INT/CHAR/POINTER，数组/结构可后续扩展 */

typedef enum {
    TY_INT = 0,
    TY_CHAR = 1,
    TY_PTR = 2,
    TY_ARRAY = 3,    /* 预留 */
    TY_STRUCT = 4    /* 预留 */
} TypeKind;

struct Type;

typedef struct Field {
    char *name;                 /* 结构字段名 */
    struct Type *type;          /* 字段类型 */
    int offset;                 /* 字段偏移（字节） */
} Field;

typedef struct Type {
    TypeKind kind;
    int size;                   /* 类型自身大小（字节） */
    struct Type *base;          /* 指针/数组的元素类型 */
    int array_len;              /* 预留：数组长度 */
    Field *fields;              /* 预留：结构字段表 */
    int field_count;            /* 预留：字段数量 */
    int align;                  /* 可选：对齐 */
} Type;

/* 构造与共享：这些返回的是共享的基础类型单例（线程不安全但足够本项目） */
Type *type_int(void);
Type *type_char(void);

/* 指针类型：返回一个以 base 为元素类型的指针 Type；可简单缓存或直接分配 */
Type *type_ptr(Type *base);

/* 数组类型：返回一个以 base 为元素类型、长度为 len 的数组类型 */
Type *type_array(Type *base, int len);

/* 结构体类型：声明、查找、字段管理 */
Type *type_struct_begin(const char *name);
Type *type_struct_lookup(const char *name);
void type_struct_add_field(Type *st, const char *field_name, Type *field_type);
Field *type_struct_get_field(Type *st, const char *field_name);
void type_struct_finalize(Type *st);

/* 工具：获取大小（直接读 type->size）；判定是否“按字节”/“按整字”访存 */
int type_size(Type *t);
int type_is_char(Type *t);
int type_is_ptr(Type *t);
int type_is_array(Type *t);
Type *type_base(Type *t);
int type_array_len(Type *t);

/* 计算从该类型衍生出的最底层元素大小（多维数组/指针链最终元素） */
int type_elem_size(Type *t);

#endif /* TYPE_H */
