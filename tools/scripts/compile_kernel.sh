#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 确保构建目录存在
mkdir -p "${PROJECT_ROOT}/build/kernel"

# 使用Docker容器进行编译，使用当前用户的UID和GID
docker run --rm \
    -v "${PROJECT_ROOT}:/workspace" \
    -u $(id -u):$(id -g) \
    strangelinux-builder \
    bash -c "cd /workspace/linux-5.4.290 && \
    make KCONFIG_CONFIG=../tools/config/kernel/.config O=../build/kernel -j$(nproc)"