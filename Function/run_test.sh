#!/bin/bash
set -e  # 任何命令失败时立即退出脚本

# ============= 配置区域 =============
DEFAULT_INPUT="test.m"  # 默认测试文件
CLEANUP_OLD=true       # 是否自动清理旧的中间文件
VERBOSE=true           # 显示详细执行信息
# ===================================

# 检查输入文件
INPUT_FILE="${1:-$DEFAULT_INPUT}"  # 使用第一个参数或默认值
BASE_NAME="${INPUT_FILE%.*}"       # 去掉扩展名的基础文件名

if [ ! -f "$INPUT_FILE" ]; then
    echo "错误: 未找到输入文件 '$INPUT_FILE'!"
    echo "用法: $0 [<input_file.m>]"
    echo "示例: $0 my_program.m"
    exit 1
fi

# 检查必需的可执行文件
REQUIRED_EXES=("mini" "asm" "machine")
for exe in "${REQUIRED_EXES[@]}"; do
    if [ ! -x "./$exe" ]; then
        echo "错误: 缺少可执行文件 '$exe' 或无执行权限!"
        echo "请确保已编译程序并赋予执行权限: chmod +x $exe"
        exit 1
    fi
done

# 清理旧文件（如果启用）
if [ "$CLEANUP_OLD" = true ]; then
    [ -f "${BASE_NAME}.s" ] && rm -vf "${BASE_NAME}.s"
    [ -f "${BASE_NAME}.o" ] && rm -vf "${BASE_NAME}.o"
fi

# 步骤 1: 运行 mini 编译器
log_step() {
    if [ "$VERBOSE" = true ]; then
        echo -e "\n===== 步骤 $1: $2 ====="
    fi
}

log_step 1 "运行 ./mini $INPUT_FILE"
./mini "$INPUT_FILE"
if [ ! -f "${BASE_NAME}.s" ]; then
    echo "错误: mini 未生成 ${BASE_NAME}.s 文件!"
    exit 1
fi
[ "$VERBOSE" = true ] && echo "✓ 成功生成 ${BASE_NAME}.s"

# 步骤 2: 运行汇编器
log_step 2 "运行 ./asm ${BASE_NAME}.s"
./asm "${BASE_NAME}.s"
if [ ! -f "${BASE_NAME}.o" ]; then
    echo "错误: asm 未生成 ${BASE_NAME}.o 文件!"
    exit 1
fi
[ "$VERBOSE" = true ] && echo "✓ 成功生成 ${BASE_NAME}.o"

# 步骤 3: 运行虚拟机
log_step 3 "运行 ./machine ${BASE_NAME}.o"
./machine "${BASE_NAME}.o"

[ "$VERBOSE" = true ] && echo -e "\n===== 测试 '$INPUT_FILE' 成功完成! ====="