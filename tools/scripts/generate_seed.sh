#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 检查必要的工具是否安装
if ! command -v cloud-localds &> /dev/null; then
    echo "错误：未找到 cloud-localds 命令"
    echo "请先安装 cloud-utils 包："
    echo "  对于 Manjaro/Arch：sudo pacman -S cloud-utils"
    exit 1
fi

# 创建 build 目录（如果不存在）
mkdir -p "${PROJECT_ROOT}/build"

# 生成 seed.img
cloud-localds \
    "${PROJECT_ROOT}/build/seed.img" \
    "${PROJECT_ROOT}/tools/config/cloud-init-user-data" \
    "${PROJECT_ROOT}/tools/config/cloud-init-meta-data"

if [ $? -eq 0 ]; then
    echo "seed.img 已成功生成在 ${PROJECT_ROOT}/build/seed.img"
else
    echo "生成 seed.img 时发生错误"
    exit 1
fi