#!/bin/bash
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
TEST_DIR="$ROOT_DIR/testcase"
REPORT_DIR="$ROOT_DIR/test_results"
TMP_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/mini-tests.XXXXXX")"
trap 'rm -rf "$TMP_ROOT"' EXIT

rm -rf "$REPORT_DIR"
mkdir -p "$REPORT_DIR"
shopt -s nullglob

TEST_FILES=("$TEST_DIR"/*.m)
if [ ${#TEST_FILES[@]} -eq 0 ]; then
    echo "未在 $TEST_DIR 找到任何 .m 测试文件"
    exit 1
fi

pass_count=0
fail_count=0
failed_cases=()

for m_file in "${TEST_FILES[@]}"; do
    name="$(basename "$m_file" .m)"
    work_dir="$TMP_ROOT/$name"
    mkdir -p "$work_dir"
    cp "$m_file" "$work_dir/$name.m"

    status=0
    detail=""

    if ! "$ROOT_DIR/mini" "$work_dir/$name.m" >"$work_dir/mini.log" 2>&1; then
        status=1
        detail="mini 编译失败"
    elif [ ! -f "$work_dir/$name.s" ]; then
        status=1
        detail="mini 未生成 ${name}.s"
    elif [ ! -s "$work_dir/$name.s" ]; then
        status=1
        detail="mini 生成的 ${name}.s 为空"
    fi

    if [ $status -eq 0 ] && [ -f "$TEST_DIR/$name.s" ]; then
        if [ ! -s "$TEST_DIR/$name.s" ]; then
            status=2
            detail="基准 ${name}.s 为空"
        elif ! diff -u "$TEST_DIR/$name.s" "$work_dir/$name.s" >"$work_dir/$name.s.diff" 2>&1; then
            status=2
            detail="${name}.s 与基准不一致"
        fi
    fi

    if [ $status -eq 0 ]; then
        if ! "$ROOT_DIR/asm" "$work_dir/$name.s" >"$work_dir/asm.log" 2>&1; then
            status=3
            detail="asm 汇编失败"
        elif [ ! -f "$work_dir/$name.o" ]; then
            status=3
            detail="asm 未生成 ${name}.o"
        elif [ ! -s "$work_dir/$name.o" ]; then
            status=3
            detail="asm 生成的 ${name}.o 为空"
        fi
    fi

    if [ $status -eq 0 ]; then
        input_file="$TEST_DIR/$name.in"
        if [ -f "$input_file" ]; then
            if ! "$ROOT_DIR/machine" "$work_dir/$name.o" <"$input_file" >"$work_dir/machine.out" 2>&1; then
                status=4
                detail="machine 执行失败"
            fi
        else
            if ! "$ROOT_DIR/machine" "$work_dir/$name.o" </dev/null >"$work_dir/machine.out" 2>&1; then
                status=4
                detail="machine 执行失败"
            fi
        fi
    fi

    if [ $status -eq 0 ] && [ -f "$TEST_DIR/$name.x" ]; then
        if [ ! -s "$TEST_DIR/$name.x" ]; then
            status=5
            detail="基准 ${name}.x 为空"
        elif [ ! -f "$work_dir/$name.x" ]; then
            status=5
            detail="machine 未生成 ${name}.x"
        else
            expected_norm="$work_dir/$name.expected.x.norm"
            actual_norm="$work_dir/$name.actual.x.norm"
            sed -E 's/0x[0-9A-Fa-f]+/0xADDR/g' "$TEST_DIR/$name.x" >"$expected_norm"
            sed -E 's/0x[0-9A-Fa-f]+/0xADDR/g' "$work_dir/$name.x" >"$actual_norm"
            if ! diff -u "$expected_norm" "$actual_norm" >"$work_dir/$name.x.diff" 2>&1; then
                status=5
                detail="${name}.x 与基准不一致"
            fi
        fi
    fi

    if [ $status -eq 0 ]; then
        echo "[PASS] $name"
        pass_count=$((pass_count + 1))
    else
        echo "[FAIL] $name -> $detail"
        fail_count=$((fail_count + 1))
        failed_cases+=("$name ($detail)")
        target_dir="$REPORT_DIR/$name"
        mkdir -p "$target_dir"
        cp "$work_dir"/* "$target_dir"/ 2>/dev/null || true
    fi

done

echo
echo "测试完成: ${pass_count} 通过, ${fail_count} 失败。"
if [ $fail_count -gt 0 ]; then
    echo "失败用例如下:"
    for item in "${failed_cases[@]}"; do
        echo "  - $item"
    done
    echo "详细日志已保存到 $REPORT_DIR"
fi
