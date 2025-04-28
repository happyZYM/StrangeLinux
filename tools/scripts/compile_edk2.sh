#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 解析命令行参数
COMPILE_ZYMPKG_ONLY=false
for arg in "$@"; do
  case $arg in
    --compile-zympkg-only)
      COMPILE_ZYMPKG_ONLY=true
      shift
      ;;
    *)
      # 未知参数
      ;;
  esac
done

cd ${PROJECT_ROOT}/edk2

export WORKSPACE=${PROJECT_ROOT}/edk2
export EDK_TOOLS_PATH=${PROJECT_ROOT}/edk2/BaseTools
export CONF_PATH=${PROJECT_ROOT}/tools/config/edk2

source edksetup.sh

if [ "$COMPILE_ZYMPKG_ONLY" = false ]; then
  make -C BaseTools -j$(nproc)
  build
  build -p ShellPkg/ShellPkg.dsc
fi

# 编译我们的ZYMPkg
build -p ZYMPkg/ZYMPkg.dsc
