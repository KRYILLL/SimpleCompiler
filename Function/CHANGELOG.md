# 项目构建问题与待办记录

## v0.1.0

- 初始版本
- 完成基本的词法分析和语法分析功能
- 仅支持 `int` 类型

## v0.2.0

- 新增对 `char` 类型的支持

1. mini.l修改
    1. 增加对 `char` 类型和字符常量的识别
2. mini.y修改
    1. 增加当前定义的变量类型变量定义
    2. 修改语法为支持类型定义和函数参数类型
    3. 增加对字符常量的处理
3. obj.c修改
    1. asm_write_back中写回时，asm_load中加载入内存时增加类型判断
    2. TAC_ACTUAL — 传递实际参数 TAC_FORMAL — 函数形参的分配/标记 TAC_VAR — 变量（局部或全局）内存分配 修改，使之支持单个字节
4. tac.h tac.c修改
    1. 在sym中增加dtype和size
    2. 增加mk_char_const函數和将mk_const函数名改为mk_int_const
    3. 将declare_var函数修改为declare_var_with_type函数
    4. 将mk_var修改为mk_var_with_type
    5. 在declare_para中增加参数，使之支持类型
    6. 增加SYM_CHAR,DTYPE_INT,DTYPE_CHAR,SIZE_INT,SIZE_CHAR
    7. 修改asm_load增加char的加载
    8. 修改TAC_OUTPUT，增加对char的支持

## v0.2.1

- 实现字符的输入

1. obj.c修改
    1.修改TAC_OUTPUT使之支持char

## v0.3.0

- 实现指针

1. mini.y修改
    1. 增加对指针的变量定义
    2. 增加对指针的参数定义
    3. 在赋值中增加指针
    4. 在表达式中支持指针
    5. 支持取指
2. obj.c修改
    1. TAC_ADDR 实现获得地址
    2. TAC_LOAD 实现获得指针数据
    3. TAC_STORE 实现指针赋值
3. tac.c修改
    1.支持以上功能

## v0.3.1

- 重构类型，使之支持扩展，便于管理

1. type.c新增
    1. 定义枚类型枚举
    2. 定义结构字段表（未使用）
    3. 定义type
    4. 定义辅助函数
2. obj.c修改
    1. 重构使原有判断按照新的类型判断
3. tac.c修改
    1. 重构使原有判断按照新的类型判断
    2. 增加mk_tmp_of_type并将部分mk_tmp修改

## v0.4.0

- 支持数组

1. type.c修改
    1. 新增数组相关的辅助函数
2. mini.y修改
    1. 修改语法使之支持数组1定义及对数组元素的使用和修改
3. tac.c修改
    1. 增加相关函数

## v0.5.0

- 支持结构体,重构语法分析中的类型

1. mini.l修改
    1. 将struct加入到关键字中
2. mini.y修改
    1. 增加struct相关支持
    2. 修改为统一类型系统
    3. 添加左值处理以便支持更为复杂的结构
3. type.c修改
    1. 增加struct相关函数
4. tac.c修改
    1. 增加结构/数组访问符
5. obj.c修改
    1. TAC_INPUT增加写回
    2. TAC_STORE增加寄存器状态处理

## v0.6.0

1. mini.y修改
    1. 添加带返回值的函数定义
2. tac.c修改
    1. 修改函数为携带类型返回值的函数
3. obj.c修改
    1. 修改存储分配指令，使之能分配符合类型长度的空间

## v1.0.0

- 支持函数，循环等

1. mini.l
    1. 增加switch case 相关
2. mini.y
    1. 增加switch case 相关
3. tac.c


## 待定事务
