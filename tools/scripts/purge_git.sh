#!/bin/bash

# 获取脚本所在目录的绝对路径
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 初始化变量
SKIP_VERIFY=false
TARGET_DIR=""

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --skip-verify)
            SKIP_VERIFY=true
            shift
            ;;
        *)
            if [ -z "$TARGET_DIR" ]; then
                TARGET_DIR="$1"
            else
                echo -e "${RED}错误：无效的参数 '$1'${NC}"
                echo "用法：$0 [--skip-verify] <目标目录>"
                exit 1
            fi
            shift
            ;;
    esac
done

# 检查是否提供了目标目录
if [ -z "$TARGET_DIR" ]; then
    echo -e "${RED}错误：请提供要清理的目录路径${NC}"
    echo "用法：$0 [--skip-verify] <目标目录>"
    exit 1
fi

# 检查目标目录是否存在
if [ ! -d "$TARGET_DIR" ]; then
    echo -e "${RED}错误：目录 '$TARGET_DIR' 不存在${NC}"
    exit 1
fi

# 查找所有的 .git 目录
echo -e "${YELLOW}正在搜索 .git 目录...${NC}"
GIT_DIRS=$(find "$TARGET_DIR" -name ".git")

if [ -z "$GIT_DIRS" ]; then
    echo -e "${YELLOW}未找到任何 .git 目录${NC}"
    exit 0
fi

# 显示找到的 .git 目录
echo -e "${YELLOW}找到以下 .git 目录：${NC}"
echo "$GIT_DIRS"
echo

if [ "$SKIP_VERIFY" = false ]; then
    # 生成三个不同的随机字符
    CHARS="ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    CONFIRM_CHAR1=${CHARS:$((RANDOM % 26)):1}
    CONFIRM_CHAR2=${CHARS:$((RANDOM % 26)):1}
    CONFIRM_CHAR3=${CHARS:$((RANDOM % 26)):1}

    # 确保三个字符都不相同
    while [ "$CONFIRM_CHAR2" = "$CONFIRM_CHAR1" ]; do
        CONFIRM_CHAR2=${CHARS:$((RANDOM % 26)):1}
    done

    while [ "$CONFIRM_CHAR3" = "$CONFIRM_CHAR1" ] || [ "$CONFIRM_CHAR3" = "$CONFIRM_CHAR2" ]; do
        CONFIRM_CHAR3=${CHARS:$((RANDOM % 26)):1}
    done

    # 三次确认
    echo -e "${RED}警告：此操作将永久删除所有找到的 .git 目录！${NC}"
    echo -e "${RED}此操作不可撤销！${NC}"
    echo

    echo -e "第一次确认：请输入 '${CONFIRM_CHAR1}' 以继续："
    read -r input
    if [ "$input" != "$CONFIRM_CHAR1" ]; then
        echo -e "${RED}操作已取消${NC}"
        exit 1
    fi

    echo -e "\n第二次确认：请输入 '${CONFIRM_CHAR2}' 以继续："
    read -r input
    if [ "$input" != "$CONFIRM_CHAR2" ]; then
        echo -e "${RED}操作已取消${NC}"
        exit 1
    fi

    echo -e "\n最后确认：请输入 '${CONFIRM_CHAR3}' 以继续："
    read -r input
    if [ "$input" != "$CONFIRM_CHAR3" ]; then
        echo -e "${RED}操作已取消${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}跳过验证步骤...${NC}"
fi

# 执行删除操作
echo -e "\n${YELLOW}开始删除 .git 目录...${NC}"
while IFS= read -r dir; do
    echo -e "正在删除: ${dir}"
    rm -rf "$dir"
done <<< "$GIT_DIRS"

echo -e "\n${GREEN}操作完成！所有 .git 目录已被删除${NC}" 