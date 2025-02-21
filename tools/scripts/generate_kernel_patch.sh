#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 进入项目根目录
cd "${PROJECT_ROOT}"

# 创建临时目录
TEMP_DIR=$(mktemp -d)
trap 'rm -rf "${TEMP_DIR}"' EXIT

# 复制原始代码到临时目录
echo "正在复制原始代码到临时目录..."
cp -r resources/original/linux-5.4.290 "${TEMP_DIR}/linux-5.4.290-original"

echo "正在复制修改后的代码到临时目录..."
cp -r linux-5.4.290 "${TEMP_DIR}/linux-5.4.290-modified"

# 生成patch
echo "正在生成patch文件..."
cd "${TEMP_DIR}"
diff -Naur linux-5.4.290-original linux-5.4.290-modified > "${PROJECT_ROOT}/build/kernel.patch"

# 检查是否成功生成patch
if [ $? -eq 0 ]; then
    echo "没有发现任何改动"
elif [ $? -eq 1 ]; then
    echo "成功生成patch文件：${PROJECT_ROOT}/build/kernel.patch"
else
    echo "生成patch时发生错误"
    exit 1
fi 