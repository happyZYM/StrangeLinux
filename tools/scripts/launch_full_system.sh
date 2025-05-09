#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

# 定义默认参数
MEM_SIZE="8G"
CPU_CORES=4
SSH_PORT=2222
VNC_ENABLED=false
DEBUG_MODE=false
USE_DIRECT_KERNEL=true

# 解析命令行参数
while [[ $# -gt 0 ]]; do
  case $1 in
    --mem=*)
      MEM_SIZE="${1#*=}"
      shift
      ;;
    --cpu=*)
      CPU_CORES="${1#*=}"
      shift
      ;;
    --ssh-port=*)
      SSH_PORT="${1#*=}"
      shift
      ;;
    --vnc)
      VNC_ENABLED=true
      shift
      ;;
    --debug)
      DEBUG_MODE=true
      shift
      ;;
    --use-kernel)
      USE_DIRECT_KERNEL=true
      shift
      ;;
    --no-direct-kernel)
      USE_DIRECT_KERNEL=false
      shift
      ;;
    --help)
      echo "用法: $0 [选项]"
      echo "选项:"
      echo "  --mem=SIZE      设置内存大小 (默认: 8G)"
      echo "  --cpu=CORES     设置CPU核心数 (默认: 4)"
      echo "  --ssh-port=PORT 设置SSH转发端口 (默认: 2222)"
      echo "  --vnc           启用VNC显示 (默认: 禁用)"
      echo "  --debug         启用调试模式"
      echo "  --use-kernel    使用自定义内核直接引导 (默认: 启用)"
      echo "  --no-direct-kernel 不使用直接内核引导 (使用镜像自带内核)"
      echo "  --help          显示此帮助信息"
      exit 0
      ;;
    *)
      echo "未知参数: $1"
      echo "使用 --help 查看帮助"
      exit 1
      ;;
  esac
done

# 定义资源文件路径
SEED_IMG="${PROJECT_ROOT}/build/seed.img"
UBUNTU_IMG="${PROJECT_ROOT}/resources/static/ubuntu-20.04-server-cloudimg-amd64.img"
KERNEL_IMAGE="${PROJECT_ROOT}/build/kernel/arch/x86/boot/bzImage"
INITRD_IMAGE="${PROJECT_ROOT}/playground/shared/initrd.img-5.4.0-205-generic"
OVMF_CODE="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_CODE.fd"
OVMF_VARS="${PROJECT_ROOT}/edk2/Build/Ovmf3264/DEBUG_GCC5/FV/OVMF_VARS.fd"

# 检查必要文件是否存在
RESOURCE_LIST=("$SEED_IMG" "$UBUNTU_IMG" "$OVMF_CODE" "$OVMF_VARS")
if [ "$USE_DIRECT_KERNEL" = true ]; then
    RESOURCE_LIST+=("$KERNEL_IMAGE")
fi

for RESOURCE in "${RESOURCE_LIST[@]}"; do
    echo "检查文件: $RESOURCE"
    if [ ! -f "$RESOURCE" ]; then
        echo "错误：$RESOURCE 不存在，请确认资源路径是否正确"
        exit 1
    fi
done

# 检查qemu-img命令是否存在
if ! command -v qemu-img &> /dev/null; then
    echo "错误：未找到qemu-img命令，请先安装qemu-utils"
    exit 1
fi

# 创建运行目录（如果不存在）
PLAYGROUND_DIR="${PROJECT_ROOT}/playground"
mkdir -p "$PLAYGROUND_DIR"

# 复制OVMF变量文件（避免修改原始文件）
cp "$OVMF_VARS" "${PLAYGROUND_DIR}/OVMF_VARS.fd"

# 准备Ubuntu镜像（如果不存在）
if [ ! -f "${PLAYGROUND_DIR}/ubuntu.qcow2" ]; then
    echo "准备Ubuntu镜像..."
    qemu-img convert -O qcow2 "$UBUNTU_IMG" "${PLAYGROUND_DIR}/ubuntu.qcow2"
    
    # 扩展镜像大小以确保足够空间
    qemu-img resize "${PLAYGROUND_DIR}/ubuntu.qcow2" +5G
else
    echo "使用现有Ubuntu镜像：${PLAYGROUND_DIR}/ubuntu.qcow2"
fi

# 可选：创建用于共享文件的目录
SHARED_DIR="${PLAYGROUND_DIR}/shared"
mkdir -p "$SHARED_DIR"
echo "创建共享目录: $SHARED_DIR"
echo "你可以在宿主机和虚拟机之间共享文件。"
echo "将文件放入此目录，然后在虚拟机中挂载。"

# 构建QEMU命令
QEMU_CMD="qemu-system-x86_64 \
    -machine q35,accel=kvm \
    -m $MEM_SIZE \
    -smp $CPU_CORES \
    -drive if=pflash,format=raw,unit=0,file=\"${OVMF_CODE}\",readonly=on \
    -drive if=pflash,format=raw,unit=1,file=\"${PLAYGROUND_DIR}/OVMF_VARS.fd\""

# 添加磁盘驱动器
QEMU_CMD+=" \
    -drive file=\"${PLAYGROUND_DIR}/ubuntu.qcow2\",format=qcow2,if=ide,index=0 \
    -drive file=\"${SEED_IMG}\",format=raw,if=ide,index=1 \
    -netdev user,id=net0,hostfwd=tcp::${SSH_PORT}-:22 \
    -device virtio-net-pci,netdev=net0 \
    -fsdev local,id=fsdev0,path=\"${SHARED_DIR}\",security_model=none \
    -device virtio-9p-pci,fsdev=fsdev0,mount_tag=hostshare"

# 如果启用直接内核引导，添加内核参数
if [ "$USE_DIRECT_KERNEL" = true ]; then
    QEMU_CMD+=" \
    -kernel \"${KERNEL_IMAGE}\" \
    -initrd \"${INITRD_IMAGE}\" \
    -append \"root=/dev/sda1 console=ttyS0 earlyprintk=serial net.ifnames=0 biosdevname=0 rootwait\""
fi

# 如果启用VNC，添加VNC参数
if [ "$VNC_ENABLED" = true ]; then
    QEMU_CMD+=" -vnc :0"
    echo "已启用VNC服务器，请使用VNC客户端连接到 localhost:5900"
else
    QEMU_CMD+=" -nographic"
fi

QEMU_CMD+=" \
    -no-reboot \
    -serial mon:stdio"

# 输出系统信息
echo "启动完整系统..."
echo "-------------------------------------"
echo "内存: $MEM_SIZE"
echo "CPU核心: $CPU_CORES"
echo "SSH端口: $SSH_PORT"
echo "VNC: $([ "$VNC_ENABLED" = true ] && echo "启用" || echo "禁用")"
echo "调试模式: $([ "$DEBUG_MODE" = true ] && echo "启用" || echo "禁用")"
echo "直接内核启动: $([ "$USE_DIRECT_KERNEL" = true ] && echo "启用" || echo "禁用")"
echo "-------------------------------------"
echo "按Ctrl+A, X可退出QEMU"
echo "-------------------------------------"

# 执行QEMU命令
eval $QEMU_CMD

echo "系统已关闭"

# 使用说明：
# 1. 系统启动后，可通过 SSH 连接到虚拟机：ssh -p $SSH_PORT ubuntu@localhost
# 2. 要访问共享目录，在虚拟机中运行：
#    sudo mkdir -p /mnt/shared
#    sudo mount -t 9p -o trans=virtio,version=9p2000.L hostshare /mnt/shared 