#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 获取项目根目录（假设脚本在 tools/scripts 目录下）
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${PROJECT_ROOT}"

rm -rf resources

mkdir -p resources/original
mkdir -p resources/static

wget https://cloud-images.ubuntu.com/releases/server/20.04/release/ubuntu-20.04-server-cloudimg-amd64.img -O resources/static/ubuntu-20.04-server-cloudimg-amd64.img

wget https://www.kernel.org/pub/linux/kernel/v5.x/linux-5.4.290.tar.xz -O resources/static/linux-5.4.290.tar.xz
tar -xf resources/static/linux-5.4.290.tar.xz -C resources/original

cd resources/original
git clone -b stable/202408 https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init
cd "${PROJECT_ROOT}"
tools/scripts/purge_git.sh --skip-verify resources/original/edk2

cd tools/config
docker build -t strangelinux-builder .