#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

cd ${PROJECT_ROOT}/edk2

export WORKSPACE=${PROJECT_ROOT}/edk2
export EDK_TOOLS_PATH=${PROJECT_ROOT}/edk2/BaseTools
export CONF_PATH=${PROJECT_ROOT}/tools/config/edk2

source edksetup.sh

make -C BaseTools -j$(nproc)

build