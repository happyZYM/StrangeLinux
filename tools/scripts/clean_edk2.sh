#!/bin/bash

# 设置颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 检查是否提供了目标目录
if [ $# -ne 1 ]; then
    echo -e "${RED}错误：请提供要清理的目录路径${NC}"
    echo "用法：$0 <目标目录>"
    exit 1
fi

TARGET_DIR="$1"

# 检查目标目录是否存在
if [ ! -d "$TARGET_DIR" ]; then
    echo -e "${RED}错误：目录 '$TARGET_DIR' 不存在${NC}"
    exit 1
fi

PYCACHE_DIRS=$(find ${TARGET_DIR} -name "__pycache__" -type d)
for dir in ${PYCACHE_DIRS}; do
    rm -rf ${dir}
done

# delete all *.d *.o files
find ${TARGET_DIR} -name "*.d" -type f -delete
find ${TARGET_DIR} -name "*.o" -type f -delete

rm -r ${TARGET_DIR}/Build
rm -r ${TARGET_DIR}/BaseTools/Source/C/bin
rm -r ${TARGET_DIR}/BaseTools/Source/C/libs

rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/EfiVfrParser.cpp
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/EfiVfrParser.h
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/VfrLexer.cpp
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/VfrLexer.h
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/VfrParser.dlg
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/VfrSyntax.cpp
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/VfrTokens.h
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/Pccts/antlr/antlr
rm ${TARGET_DIR}/BaseTools/Source/C/VfrCompile/Pccts/dlg/dlg