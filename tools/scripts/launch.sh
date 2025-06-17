#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 定义资源文件路径
OVMF_CODE="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_CODE.fd"
OVMF_VARS="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_VARS.fd"
# SHELL_EFI="${PROJECT_ROOT}/edk2/Build/Shell/DEBUG_GCC5/X64/ShellPkg/Application/Shell/EA4BB293-2D7F-4456-A681-1F22F42CD0BC/DEBUG/Shell.efi"
RAW_ACPIVIEW_EFI="${PROJECT_ROOT}/edk2/Build/Shell/DEBUG_GCC5/X64/ShellPkg/Application/AcpiViewApp/AcpiViewApp/DEBUG/AcpiViewApp.efi"
HELLO_WORLD_EFI="${PROJECT_ROOT}/edk2/Build/ZYMPkg/DEBUG_GCC5/X64/ZYMPkg/Application/HelloWorld/HelloWorld/DEBUG/HelloWorld.efi"
HALLO_WORD_EFI="${PROJECT_ROOT}/edk2/Build/ZYMPkg/DEBUG_GCC5/X64/ZYMPkg/Application/HalloWord/HalloWord/DEBUG/HalloWord.efi"
MY_ACPIVIEW_EFI="${PROJECT_ROOT}/edk2/Build/ZYMPkg/DEBUG_GCC5/X64/ZYMPkg/Application/AcpiView/AcpiView/DEBUG/AcpiView.efi"
KERNEL_IMAGE="${PROJECT_ROOT}/build/kernel/arch/x86/boot/bzImage"
PROG_DIR="${PROJECT_ROOT}/build/prog"

ACPI_DUMP_PATH="/home/zym/.local/opt/acpica/generate/unix/bin/acpidump"

# 检查上述文件和目录是否存在
RESOURCE_LIST=("$OVMF_CODE" "$OVMF_VARS" "$RAW_ACPIVIEW_EFI" "$HELLO_WORLD_EFI" "$HALLO_WORD_EFI" "$MY_ACPIVIEW_EFI" "$KERNEL_IMAGE")
DIR_LIST=("$PROG_DIR")

for RESOURCE in "${RESOURCE_LIST[@]}"; do
    echo "检查文件: $RESOURCE"
    if [ ! -f "$RESOURCE" ]; then
        echo "错误：$RESOURCE 不存在，请确认编译路径是否正确"
        exit 1
    fi
done

for DIR in "${DIR_LIST[@]}"; do
    echo "检查目录: $DIR"
    if [ ! -d "$DIR" ]; then
        echo "错误：$DIR 不存在，请确认目录路径是否正确"
        exit 1
    fi
done
# 创建运行目录（如果不存在）
PLAYGROUND_DIR="${PROJECT_ROOT}/playground"
rm -rf "$PLAYGROUND_DIR"
mkdir -p "$PLAYGROUND_DIR"

# 复制OVMF变量文件（避免修改原始文件）
cp "$OVMF_VARS" "${PLAYGROUND_DIR}/OVMF_VARS.fd"

INITRAMFS_DIR="${PLAYGROUND_DIR}/initramfs"
rm -rf "${INITRAMFS_DIR}"
mkdir -p "${INITRAMFS_DIR}"/{bin,dev,proc,sys,mnt,tmp}

# 检查是否安装了busybox
if ! command -v busybox &> /dev/null; then
    echo "错误：未找到busybox，请先安装busybox"
    exit 1
fi

# 复制busybox到initramfs
cp $(which busybox) "${INITRAMFS_DIR}/bin/busybox"

cp "$ACPI_DUMP_PATH" "${INITRAMFS_DIR}/bin/acpidump"

cp -r "$PROG_DIR" "${INITRAMFS_DIR}/"

for file in "${PROG_DIR}"/*; do
    if [ -f "$file" ]; then
        INITRAM_DIR=${INITRAMFS_DIR} ${SCRIPT_DIR}/add_depend.sh "$file"
    fi
done

# 创建必要的lib目录
mkdir -p "${INITRAMFS_DIR}/lib"
mkdir -p "${INITRAMFS_DIR}/lib64"
mkdir -p "${INITRAMFS_DIR}/usr/lib"

# 复制关键的系统库
# 查找并复制libgcc_s.so.1
LIBGCC_PATH=$(find /usr/lib* /lib* -name "libgcc_s.so.1" 2>/dev/null | head -1)
if [ -n "$LIBGCC_PATH" ]; then
    cp "$LIBGCC_PATH" "${INITRAMFS_DIR}/lib/libgcc_s.so.1"
    echo "复制 libgcc_s.so.1 从 $LIBGCC_PATH"
else
    echo "警告：未找到 libgcc_s.so.1"
fi

# 复制pthread库
LIBPTHREAD_PATH=$(find /usr/lib* /lib* -name "libpthread.so.0" 2>/dev/null | head -1)
if [ -n "$LIBPTHREAD_PATH" ]; then
    cp "$LIBPTHREAD_PATH" "${INITRAMFS_DIR}/lib/libpthread.so.0"
    echo "复制 libpthread.so.0 从 $LIBPTHREAD_PATH"
fi

# 创建init脚本
cat > "${INITRAMFS_DIR}/init" << 'EOF'
#!/bin/busybox sh

# 创建busybox符号链接
/bin/busybox --install -s /bin

# 挂载必要的文件系统
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# 等待文件系统挂载完成
sleep 1

# 创建基本的设备节点
# mknod /dev/null c 1 3
# mknod /dev/tty c 5 0
# mknod /dev/console c 5 1
# mknod /dev/ttyS0 c 4 64

# 设置基本的环境变量
export PATH=/bin:/usr/bin
export HOME=/root
export TERM=vt100
export PS1='[\\w]\\$ '
export LD_LIBRARY_PATH=/lib:/lib64:/usr/lib:/usr/lib64

echo "文件系统挂载完成"
echo "当前挂载点："
mount

echo "Hello, World!"

# 启动shell，启用作业控制
setsid /bin/sh -c 'exec /bin/sh </dev/ttyS0 >/dev/ttyS0 2>&1'
poweroff -f
EOF

# 设置init脚本权限
chmod +x "${INITRAMFS_DIR}/init"

mkdir -p "$PLAYGROUND_DIR/uefi"

# 创建initramfs.cpio.gz
( cd "${INITRAMFS_DIR}" && find . | cpio -H newc -o | gzip > "${PLAYGROUND_DIR}/uefi/initramfs.cpio.gz" )

# cp "$SHELL_EFI" "${PLAYGROUND_DIR}/uefi/Origin_Shell.efi"
cp "$RAW_ACPIVIEW_EFI" "${PLAYGROUND_DIR}/uefi/O_AcpiViewApp.efi"
cp "$MY_ACPIVIEW_EFI" "${PLAYGROUND_DIR}/uefi/My_AcpiView.efi"
cp "$HELLO_WORLD_EFI" "${PLAYGROUND_DIR}/uefi/HelloWorld.efi"
cp "$HALLO_WORD_EFI" "${PLAYGROUND_DIR}/uefi/HalloWord.efi"
cp "$KERNEL_IMAGE" "${PLAYGROUND_DIR}/uefi/bzImage"
chmod +x "${PLAYGROUND_DIR}/uefi/bzImage"

#  创建 `startup.nsh`
cat > "${PLAYGROUND_DIR}/uefi/startup.nsh" << 'EOF'
My_AcpiView.efi hack_acpi_mode
bzImage initrd=initramfs.cpio.gz root=/dev/ram0 console=ttyS0
EOF

# 暂停
read -p "按任意键继续..."

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
