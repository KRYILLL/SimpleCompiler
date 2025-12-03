#!/bin/bash
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR="$SCRIPT_DIR/testcase"
RUN_TEST="$SCRIPT_DIR/run_test.sh"

if [ ! -d "$TEST_DIR" ]; then
    echo "错误: 未找到测试目录 '$TEST_DIR'" >&2
    exit 1
fi

if [ ! -x "$RUN_TEST" ]; then
    echo "错误: 未找到可执行的 run_test.sh" >&2
    exit 1
fi

status=0
for testcase in "$TEST_DIR"/*.m; do
    if [ ! -e "$testcase" ]; then
        echo "警告: 目录中没有 .m 测试文件" >&2
        break
    fi

    name="$(basename "$testcase")"
    echo -e "\n===== 运行测试: $name ====="
    if "$RUN_TEST" "$testcase"; then
        echo "===== 测试 $name 成功 ====="
    else
        echo "===== 测试 $name 失败 =====" >&2
        status=1
    fi

done

exit $status
