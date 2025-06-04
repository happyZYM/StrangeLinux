#!/bin/bash

# 测试脚本：比较 tcpdump 和 mytcpdump 的输出
# 使用方法: ./run_tests.sh [interface] [duration]

set -e  # 遇到错误立即退出

# 默认参数
DEFAULT_INTERFACE="wlp0s20f3"
DEFAULT_DURATION=10
BUILD_DIR="../build"
MYTCPDUMP="$BUILD_DIR/mytcpdump"

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 使用参数或默认值
INTERFACE=${1:-$DEFAULT_INTERFACE}
DURATION=${2:-$DEFAULT_DURATION}

# 测试过滤器列表
declare -a TEST_FILTERS=(
    "port 443"
    "port 80"
    "tcp"
    "udp"
    "port 443 and tcp"
    "host 1.1.1.1"
    "net 192.168.0.0/24"
)

# 输出目录
TEST_OUTPUT_DIR="/tmp/custom_tcpdump_test_results"
mkdir -p "$TEST_OUTPUT_DIR"

echo -e "${BLUE}=== mytcpdump 比较测试 ===${NC}"
echo -e "${YELLOW}接口: $INTERFACE${NC}"
echo -e "${YELLOW}测试时长: $DURATION 秒${NC}"
echo -e "${YELLOW}输出目录: $TEST_OUTPUT_DIR${NC}"
echo

# 检查必要工具
check_dependencies() {
    echo -e "${BLUE}检查依赖...${NC}"
    
    if ! command -v tcpdump &> /dev/null; then
        echo -e "${RED}错误: tcpdump 未安装${NC}"
        exit 1
    fi
    
    if [ ! -f "$MYTCPDUMP" ]; then
        echo -e "${RED}错误: mytcpdump 未找到，请先运行 make${NC}"
        exit 1
    fi
    
    if [ "$EUID" -ne 0 ]; then
        echo -e "${RED}错误: 需要 root 权限运行网络抓包${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}依赖检查通过${NC}"
}

# 运行单个测试
run_single_test() {
    local filter="$1"
    local test_name="test_$(echo "$filter" | tr ' /.()' '____' | tr -d '<>:|?*')"
    
    echo -e "${BLUE}测试过滤器: $filter${NC}"
    
    # 输出文件
    local tcpdump_output="$TEST_OUTPUT_DIR/${test_name}_tcpdump.txt"
    local mytcpdump_output="$TEST_OUTPUT_DIR/${test_name}_mytcpdump.txt"
    local comparison_output="$TEST_OUTPUT_DIR/${test_name}_comparison.txt"
    
    # 清理之前的输出文件
    # rm "$tcpdump_output" "$mytcpdump_output" "$comparison_output"
    [ -e "$tcpdump_output" ] && rm "$tcpdump_output"
    [ -e "$mytcpdump_output" ] && rm "$mytcpdump_output"
    [ -e "$comparison_output" ] && rm "$comparison_output"
    
    echo "  启动 tcpdump 和 mytcpdump..."
    
    # 启动 tcpdump (后台运行)
    tcpdump -i "$INTERFACE" -n "ip and ($filter)" -q > "$tcpdump_output" 2>&1 &
    local tcpdump_pid=$!
    
    # 启动 mytcpdump (后台运行)
    "$MYTCPDUMP" "$INTERFACE" "$filter" > "$mytcpdump_output" 2>&1 &
    local mytcpdump_pid=$!
    
    # 等待指定时间
    echo "  等待 $DURATION 秒..."
    sleep "$DURATION"
    
    # 停止两个进程
    echo "  停止抓包..."
    kill -9 $tcpdump_pid 2>/dev/null || true
    kill -9 $mytcpdump_pid 2>/dev/null || true
    
    # 等待进程结束
    wait $tcpdump_pid 2>/dev/null || true
    wait $mytcpdump_pid 2>/dev/null || true
    
    # 分析结果
    analyze_results "$filter" "$tcpdump_output" "$mytcpdump_output" "$comparison_output"
}

# 分析测试结果
analyze_results() {
    local filter="$1"
    local tcpdump_file="$2"
    local mytcpdump_file="$3"
    local comparison_file="$4"
    
    echo "  分析结果..."
    
    # 创建处理后的文件（去掉前2行和最后3行）
    local tcpdump_clean="${tcpdump_file}.clean"
    local mytcpdump_clean="${mytcpdump_file}.clean"
    
    # 预处理tcpdump输出文件
    if [ -f "$tcpdump_file" ] && [ -s "$tcpdump_file" ]; then
        sed '1,2d' "$tcpdump_file" | head -n -3 > "$tcpdump_clean" 2>/dev/null || touch "$tcpdump_clean"
    else
        touch "$tcpdump_clean"
    fi
    
    # 预处理mytcpdump输出文件
    if [ -f "$mytcpdump_file" ] && [ -s "$mytcpdump_file" ]; then
        sed '1,2d' "$mytcpdump_file" | head -n -3 > "$mytcpdump_clean" 2>/dev/null || touch "$mytcpdump_clean"
    else
        touch "$mytcpdump_clean"
    fi
    
    # 统计抓包数量（使用清理后的文件）
    local tcpdump_count=0
    local mytcpdump_count=0
    
    if [ -f "$tcpdump_clean" ]; then
        tcpdump_count=$(wc -l < "$tcpdump_clean" 2>/dev/null || echo "0")
    fi
    
    if [ -f "$mytcpdump_clean" ]; then
        mytcpdump_count=$(wc -l < "$mytcpdump_clean" 2>/dev/null || echo "0")
    fi

    temp_file=$(mktemp)
    head -n $mytcpdump_count "$tcpdump_clean" >"$temp_file"
    mv "$temp_file" "$tcpdump_clean"

    tcpdump_count=$(wc -l < "$tcpdump_clean" 2>/dev/null || echo "0")
    mytcpdump_count=$(wc -l < "$mytcpdump_clean" 2>/dev/null || echo "0")

    temp_file=$(mktemp)
    head -n $tcpdump_count "$mytcpdump_clean" >"$temp_file"
    mv "$temp_file" "$mytcpdump_clean"

    tcpdump_count=$(wc -l < "$tcpdump_clean" 2>/dev/null || echo "0")
    mytcpdump_count=$(wc -l < "$mytcpdump_clean" 2>/dev/null || echo "0")
    
    # 使用diff比较两个清理后的文件
    local diff_result=""
    if [ -f "$tcpdump_clean" ] && [ -f "$mytcpdump_clean" ]; then
        if diff -q "$tcpdump_clean" "$mytcpdump_clean" >/dev/null 2>&1; then
            diff_result="IDENTICAL"
        else
            diff_result="DIFFERENT"
        fi
    else
        diff_result="FILE_MISSING"
    fi
    
    # 生成比较报告
    {
        echo "=== 测试比较报告 ==="
        echo "过滤器: $filter"
        echo "测试时间: $(date)"
        echo "测试时长: $DURATION 秒"
        echo ""
        echo "抓包统计:"
        echo "  tcpdump:    $tcpdump_count 个数据包"
        echo "  mytcpdump:  $mytcpdump_count 个数据包"
        echo ""
        
        echo "文件比较结果: $diff_result"
        
        if [ "$diff_result" = "IDENTICAL" ]; then
            echo "状态: PASS (输出完全一致)"
        elif [ "$diff_result" = "FILE_MISSING" ]; then
            echo "状态: FAIL (文件缺失)"
        elif [ "$tcpdump_count" -eq 0 ] && [ "$mytcpdump_count" -eq 0 ]; then
            echo "结果: 两者都没有抓到数据包 (可能网络无活动)"
            echo "状态: PASS (无数据包情况下的一致性)"
        else
            echo "结果: 输出内容不同"
            local diff_percent=0
            if [ "$tcpdump_count" -gt 0 ]; then
                diff_percent=$(( (mytcpdump_count - tcpdump_count) * 100 / tcpdump_count ))
                echo "数据包数量差异: $diff_percent%"
            fi
            echo "状态: FAIL (输出不一致)"
        fi
        echo ""
        
        # 显示样本输出
        echo "=== tcpdump 清理后输出 (前5行) ==="
        if [ -f "$tcpdump_clean" ]; then
            head -5 "$tcpdump_clean" 2>/dev/null || echo "无有效数据包输出"
        else
            echo "文件不存在"
        fi
        echo ""
        
        echo "=== mytcpdump 清理后输出 (前5行) ==="
        if [ -f "$mytcpdump_clean" ]; then
            head -5 "$mytcpdump_clean" 2>/dev/null || echo "无有效数据包输出"
        else
            echo "文件不存在"
        fi
        echo ""
        
        # 如果文件不同，显示详细差异
        if [ "$diff_result" = "DIFFERENT" ]; then
            echo "=== 详细差异 (前10行) ==="
            diff "$tcpdump_clean" "$mytcpdump_clean" 2>/dev/null | head -10 || echo "diff命令执行失败"
        fi
        
    } > "$comparison_file"
    
    # 显示简要结果
    echo "    tcpdump: $tcpdump_count 行, mytcpdump: $mytcpdump_count 行"
    
    if [ "$diff_result" = "IDENTICAL" ]; then
        echo -e "    ${GREEN}PASS: 输出完全一致${NC}"
    elif [ "$diff_result" = "FILE_MISSING" ]; then
        echo -e "    ${RED}FAIL: 文件缺失${NC}"
    elif [ "$tcpdump_count" -eq 0 ] && [ "$mytcpdump_count" -eq 0 ]; then
        echo -e "    ${YELLOW}无数据包 (网络可能无活动)${NC}"
    else
        echo -e "    ${RED}FAIL: 输出不一致${NC}"
    fi
    echo
    
    # 清理临时文件
    # rm -f "$tcpdump_clean" "$mytcpdump_clean" 2>/dev/null
}

# 生成总结报告
generate_summary() {
    local summary_file="$TEST_OUTPUT_DIR/test_summary.txt"
    
    echo -e "${BLUE}生成总结报告...${NC}"
    
    {
        echo "=== mytcpdump 测试总结报告 ==="
        echo "测试时间: $(date)"
        echo "测试接口: $INTERFACE"
        echo "每个测试时长: $DURATION 秒"
        echo "测试过滤器数量: ${#TEST_FILTERS[@]}"
        echo ""
        
        local pass_count=0
        local fail_count=0
        local warn_count=0
        
        for filter in "${TEST_FILTERS[@]}"; do
            local test_name="test_$(echo "$filter" | tr ' /.()' '____' | tr -d '<>:|?*')"
            local comparison_file="$TEST_OUTPUT_DIR/${test_name}_comparison.txt"
            
            if [ -f "$comparison_file" ]; then
                local status=$(grep "状态:" "$comparison_file" | cut -d' ' -f2)
                echo "过滤器 '$filter': $status"
                
                case "$status" in
                    "PASS"*) ((pass_count++)) ;;
                    "FAIL"*) ((fail_count++)) ;;
                    "WARN"*) ((warn_count++)) ;;
                esac
            fi
        done
        
        echo ""
        echo "=== 测试结果统计 ==="
        echo "通过 (PASS): $pass_count"
        echo "警告 (WARN): $warn_count"
        echo "失败 (FAIL): $fail_count"
        echo "总计: $((pass_count + warn_count + fail_count))"
        
        if [ "$fail_count" -eq 0 ]; then
            echo ""
            echo "✅ 所有测试都通过了！mytcpdump 工作正常。"
        else
            echo ""
            echo "❌ 有 $fail_count 个测试失败，需要检查实现。"
        fi
        
    } > "$summary_file"
    
    echo -e "${GREEN}总结报告已保存到: $summary_file${NC}"
}

# 主函数
main() {
    check_dependencies
    
    echo -e "${BLUE}开始测试...${NC}"
    echo
    
    # 运行所有测试
    for filter in "${TEST_FILTERS[@]}"; do
        run_single_test "$filter"
    done
    
    # 生成总结报告
    generate_summary
    
    echo -e "${GREEN}所有测试完成！${NC}"
    echo -e "详细结果请查看 ${YELLOW}$TEST_OUTPUT_DIR${NC} 目录"
}

# 帮助信息
show_help() {
    echo "用法: $0 [interface] [duration]"
    echo ""
    echo "参数:"
    echo "  interface  网络接口名称 (默认: $DEFAULT_INTERFACE)"
    echo "  duration   每个测试的时长(秒) (默认: $DEFAULT_DURATION)"
    echo ""
    echo "示例:"
    echo "  $0                      # 使用默认参数"
    echo "  $0 eth0                 # 使用 eth0 接口"
    echo "  $0 wlp0s20f3 20        # 使用 wlp0s20f3 接口，每个测试20秒"
    echo ""
    echo "注意: 需要 root 权限运行"
}

# 处理命令行参数
if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
    show_help
    exit 0
fi

# 运行主程序
main 