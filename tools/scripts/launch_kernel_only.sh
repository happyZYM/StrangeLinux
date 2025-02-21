#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 检查必要文件是否存在
KERNEL_IMAGE="${PROJECT_ROOT}/build/kernel/arch/x86/boot/bzImage"
OVMF_CODE="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_CODE.fd"
OVMF_VARS="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_VARS.fd"

if [ ! -f "$KERNEL_IMAGE" ]; then
    echo "错误：内核镜像不存在，请先编译内核"
    exit 1
fi

if [ ! -f "$OVMF_CODE" ] || [ ! -f "$OVMF_VARS" ]; then
    echo "错误：OVMF固件不存在，请先编译EDK2"
    exit 1
fi

# 创建运行目录（如果不存在）
PLAYGROUND_DIR="${PROJECT_ROOT}/playground"
INITRAMFS_DIR="${PLAYGROUND_DIR}/initramfs"
mkdir -p "$PLAYGROUND_DIR"
mkdir -p "${INITRAMFS_DIR}"/{bin,dev,proc,sys}

# 检查是否安装了busybox
if ! command -v busybox &> /dev/null; then
    echo "错误：未找到busybox，请先安装busybox"
    exit 1
fi

# 复制busybox到initramfs
cp $(which busybox) "${INITRAMFS_DIR}/bin/busybox"

# 创建init脚本
cat > "${INITRAMFS_DIR}/init" << 'EOF'
#!/bin/busybox sh

# 挂载必要的文件系统
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev

# 等待文件系统挂载完成
sleep 1

# 创建基本的设备节点
mknod /dev/null c 1 3
mknod /dev/tty c 5 0
mknod /dev/console c 5 1
mknod /dev/ttyS0 c 4 64

# 创建busybox符号链接
/bin/busybox --install -s /bin

# 设置基本的环境变量
export PATH=/bin
export HOME=/root
export TERM=vt100
export PS1='[\\w]\\$ '

echo "文件系统挂载完成"
echo "当前挂载点："
mount

echo "Hello, World!"

# 启动shell，启用作业控制
setsid /bin/sh -c 'exec /bin/sh </dev/ttyS0 >/dev/ttyS0 2>&1'
EOF

# 设置init脚本权限
chmod +x "${INITRAMFS_DIR}/init"

# 创建initramfs.cpio.gz
( cd "${INITRAMFS_DIR}" && find . | cpio -H newc -o | gzip > "${PLAYGROUND_DIR}/initramfs.cpio.gz" )

# 复制OVMF变量文件（避免修改原始文件）
cp "$OVMF_VARS" "${PLAYGROUND_DIR}/OVMF_VARS.fd"

# 启动QEMU
qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -m 8G \
    -smp 4 \
    -drive if=pflash,format=raw,unit=0,file="${OVMF_CODE}",readonly=on \
    -drive if=pflash,format=raw,unit=1,file="${PLAYGROUND_DIR}/OVMF_VARS.fd" \
    -kernel "${KERNEL_IMAGE}" \
    -initrd "${PLAYGROUND_DIR}/initramfs.cpio.gz" \
    -append "console=ttyS0 earlyprintk=ttyS0 debug loglevel=7" \
    -nographic \
    -no-reboot \
    -serial mon:stdio 