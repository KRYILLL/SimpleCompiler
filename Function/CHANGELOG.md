# 项目构建问题与待办记录

## v0.1.0

- 初始版本
- 完成基本的词法分析和语法分析功能
- 仅支持 `int` 类型

## v0.2.0

- 新增对 `char` 类型的支持

### 修改点

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

## 待定事务
1. mk_tmp未支持type可能存在问题，暂时未改