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
    const char *name;           /* 预留：结构字段名 */
    struct Type *type;          /* 预留：字段类型 */
    int offset;                 /* 预留：字段偏移 */
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

/* 工具：获取大小（直接读 type->size）；判定是否“按字节”/“按整字”访存 */
int type_size(Type *t);
int type_is_char(Type *t);
int type_is_ptr(Type *t);
Type *type_base(Type *t);

#endif /* TYPE_H */
