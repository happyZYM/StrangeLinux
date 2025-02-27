#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 检查OVMF固件文件是否存在
OVMF_CODE="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_CODE.fd"
OVMF_VARS="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_VARS.fd"
SHELL_EFI="${PROJECT_ROOT}/edk2/Build/Shell/DEBUG_GCC5/X64/ShellPkg/Application/Shell/EA4BB293-2D7F-4456-A681-1F22F42CD0BC/DEBUG/Shell.efi"
HELLO_WORLD_EFI="${PROJECT_ROOT}/edk2/Build/ZYMPkg/DEBUG_GCC5/X64/ZYMPkg/Application/HelloWorld/HelloWorld/DEBUG/HelloWorld.efi"

if [ ! -f "$OVMF_CODE" ] || [ ! -f "$OVMF_VARS" ]; then
    echo "错误：OVMF固件不存在，请先编译EDK2"
    exit 1
fi

if [ ! -f "$SHELL_EFI" ]; then
    echo "警告：Shell.efi 不存在，请确认编译路径是否正确"
    exit 1
fi

if [ ! -f "$HELLO_WORLD_EFI" ]; then
    echo "警告：HelloWorld.efi 不存在，请确认编译路径是否正确"
    exit 1
fi

# 创建运行目录（如果不存在）
PLAYGROUND_DIR="${PROJECT_ROOT}/playground"
mkdir -p "$PLAYGROUND_DIR"

# 复制OVMF变量文件（避免修改原始文件）
cp "$OVMF_VARS" "${PLAYGROUND_DIR}/OVMF_VARS.fd"

mkdir -p "$PLAYGROUND_DIR/uefi"
cp "$SHELL_EFI" "${PLAYGROUND_DIR}/uefi/Shell.efi"
cp "$HELLO_WORLD_EFI" "${PLAYGROUND_DIR}/uefi/HelloWorld.efi"

# 启动QEMU进入UEFI shell
qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -m 8G \
    -smp 4 \
    -drive if=pflash,format=raw,unit=0,file="${OVMF_CODE}",readonly=on \
    -drive if=pflash,format=raw,unit=1,file="${PLAYGROUND_DIR}/OVMF_VARS.fd" \
    -drive file=fat:rw:"${PLAYGROUND_DIR}/uefi",format=raw,if=ide,index=0 \
    -nographic \
    -no-reboot \
    -serial mon:stdio
