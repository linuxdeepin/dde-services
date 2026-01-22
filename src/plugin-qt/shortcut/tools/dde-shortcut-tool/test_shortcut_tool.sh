#!/bin/bash
#
# dde-shortcut-tool 功能测试脚本
# 
# 用法: ./test_shortcut_tool.sh [--dry-run] [--skip-dangerous]
#
# 选项:
#   --dry-run         只显示命令，不实际执行
#   --skip-dangerous  跳过危险操作（如关机、休眠、关屏等）
#

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 工具路径
TOOL="dde-shortcut-tool"

# 参数解析
DRY_RUN=false
SKIP_DANGEROUS=false

for arg in "$@"; do
    case $arg in
        --dry-run)
            DRY_RUN=true
            ;;
        --skip-dangerous)
            SKIP_DANGEROUS=true
            ;;
        --help|-h)
            echo "用法: $0 [--dry-run] [--skip-dangerous]"
            echo ""
            echo "选项:"
            echo "  --dry-run         只显示命令，不实际执行"
            echo "  --skip-dangerous  跳过危险操作（如关机、休眠、关屏等）"
            exit 0
            ;;
    esac
done

# 统计
TOTAL=0
PASSED=0
FAILED=0
SKIPPED=0

# 执行测试
run_test() {
    local name="$1"
    local cmd="$2"
    local dangerous="${3:-false}"
    
    TOTAL=$((TOTAL + 1))
    
    if [ "$dangerous" = "true" ] && [ "$SKIP_DANGEROUS" = "true" ]; then
        echo -e "${YELLOW}[SKIP]${NC} $name"
        echo "       命令: $TOOL $cmd"
        SKIPPED=$((SKIPPED + 1))
        return
    fi
    
    echo -e "${BLUE}[TEST]${NC} $name"
    echo "       命令: $TOOL $cmd"
    
    if [ "$DRY_RUN" = "true" ]; then
        echo -e "${YELLOW}[DRY-RUN]${NC} 跳过执行"
        SKIPPED=$((SKIPPED + 1))
        return
    fi
    
    if $TOOL $cmd; then
        echo -e "${GREEN}[PASS]${NC} $name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $name (退出码: $?)"
        FAILED=$((FAILED + 1))
    fi
    echo ""
}

# 测试帮助信息
test_help() {
    local name="$1"
    local cmd="$2"
    
    TOTAL=$((TOTAL + 1))
    
    echo -e "${BLUE}[TEST]${NC} $name"
    echo "       命令: $TOOL $cmd"
    
    if [ "$DRY_RUN" = "true" ]; then
        echo -e "${YELLOW}[DRY-RUN]${NC} 跳过执行"
        SKIPPED=$((SKIPPED + 1))
        return
    fi
    
    # 帮助命令返回0表示成功
    if $TOOL $cmd > /dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} $name"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} $name"
        FAILED=$((FAILED + 1))
    fi
    echo ""
}

echo "========================================"
echo "  dde-shortcut-tool 功能测试"
echo "========================================"
echo ""
echo "会话类型: ${XDG_SESSION_TYPE:-unknown}"
echo "DRY_RUN: $DRY_RUN"
echo "SKIP_DANGEROUS: $SKIP_DANGEROUS"
echo ""

# 检查工具是否存在
if ! command -v $TOOL &> /dev/null; then
    # 尝试使用构建目录中的工具
    if [ -f "./build/tools/dde-shortcut-tool/dde-shortcut-tool" ]; then
        TOOL="./build/tools/dde-shortcut-tool/dde-shortcut-tool"
        echo "使用构建目录中的工具: $TOOL"
    else
        echo -e "${RED}错误: 找不到 dde-shortcut-tool${NC}"
        echo "请先编译项目或将工具添加到 PATH"
        exit 1
    fi
fi

echo ""
echo "========================================"
echo "  1. 帮助信息测试"
echo "========================================"
echo ""

test_help "全局帮助" "--help"
test_help "版本信息" "--version"
test_help "audio 帮助" "audio --help"
test_help "display 帮助" "display --help"
test_help "touchpad 帮助" "touchpad --help"
test_help "power 帮助" "power --help"
test_help "kbdlight 帮助" "kbdlight --help"
test_help "media 帮助" "media --help"
test_help "lockkey 帮助" "lockkey --help"
test_help "launch 帮助" "launch --help"
test_help "network 帮助" "network --help"

echo ""
echo "========================================"
echo "  2. Audio 控制器测试"
echo "========================================"
echo ""

run_test "音量增加" "audio volume-up"
run_test "音量减少" "audio volume-down"
run_test "静音切换" "audio mute-toggle"
run_test "麦克风静音切换" "audio mic-mute-toggle"

echo ""
echo "========================================"
echo "  3. Display 控制器测试"
echo "========================================"
echo ""

run_test "亮度增加" "display brightness-up"
run_test "亮度减少" "display brightness-down"
run_test "显示模式切换" "display switch-mode"
run_test "关闭屏幕" "display turn-off-screen" true

echo ""
echo "========================================"
echo "  4. Touchpad 控制器测试"
echo "========================================"
echo ""

run_test "触摸板切换" "touchpad toggle"
run_test "触摸板开启" "touchpad on"
run_test "触摸板关闭" "touchpad off"

echo ""
echo "========================================"
echo "  5. Power 控制器测试"
echo "========================================"
echo ""

run_test "电源模式切换" "power switch-mode"
run_test "系统锁定" "power system-away"
# 电源按钮行为取决于配置，可能触发关机/休眠等
run_test "电源按钮" "power button" true

echo ""
echo "========================================"
echo "  6. Keyboard Light 控制器测试"
echo "========================================"
echo ""

run_test "键盘背光切换" "kbdlight toggle"
run_test "键盘背光增加" "kbdlight brightness-up"
run_test "键盘背光减少" "kbdlight brightness-down"

echo ""
echo "========================================"
echo "  7. Media 控制器测试"
echo "========================================"
echo ""

run_test "媒体播放/暂停" "media play-pause"
run_test "媒体播放" "media play"
run_test "媒体暂停" "media pause"
run_test "媒体停止" "media stop"
run_test "上一曲" "media previous"
run_test "下一曲" "media next"
run_test "快退" "media rewind"
run_test "快进" "media forward"

echo ""
echo "========================================"
echo "  8. Lock Key 控制器测试"
echo "========================================"
echo ""

run_test "CapsLock OSD" "lockkey capslock"
run_test "NumLock OSD" "lockkey numlock"

echo ""
echo "========================================"
echo "  9. Launch 控制器测试"
echo "========================================"
echo ""

run_test "启动器搜索" "launch search"
run_test "系统工具" "launch tools"
run_test "消息应用" "launch messenger"
run_test "电池设置" "launch battery"
# MIME 类型启动需要具体的 MIME 类型
run_test "MIME 启动 (浏览器)" "launch mime x-scheme-handler/http"

echo ""
echo "========================================"
echo "  10. Network 控制器测试"
echo "========================================"
echo ""

run_test "WiFi 切换" "network toggle-wifi"
run_test "飞行模式切换" "network toggle-airplane"

echo ""
echo "========================================"
echo "  11. 错误处理测试"
echo "========================================"
echo ""

# 测试无效命令
TOTAL=$((TOTAL + 1))
echo -e "${BLUE}[TEST]${NC} 无效命令处理"
echo "       命令: $TOOL invalid-command"
if [ "$DRY_RUN" = "false" ]; then
    if ! $TOOL invalid-command > /dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} 正确返回错误"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} 应该返回错误"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "${YELLOW}[DRY-RUN]${NC} 跳过执行"
    SKIPPED=$((SKIPPED + 1))
fi
echo ""

# 测试无效动作
TOTAL=$((TOTAL + 1))
echo -e "${BLUE}[TEST]${NC} 无效动作处理"
echo "       命令: $TOOL audio invalid-action"
if [ "$DRY_RUN" = "false" ]; then
    if ! $TOOL audio invalid-action > /dev/null 2>&1; then
        echo -e "${GREEN}[PASS]${NC} 正确返回错误"
        PASSED=$((PASSED + 1))
    else
        echo -e "${RED}[FAIL]${NC} 应该返回错误"
        FAILED=$((FAILED + 1))
    fi
else
    echo -e "${YELLOW}[DRY-RUN]${NC} 跳过执行"
    SKIPPED=$((SKIPPED + 1))
fi
echo ""

echo "========================================"
echo "  测试结果汇总"
echo "========================================"
echo ""
echo -e "总计: $TOTAL"
echo -e "${GREEN}通过: $PASSED${NC}"
echo -e "${RED}失败: $FAILED${NC}"
echo -e "${YELLOW}跳过: $SKIPPED${NC}"
echo ""

if [ $FAILED -gt 0 ]; then
    echo -e "${RED}测试未全部通过${NC}"
    exit 1
else
    echo -e "${GREEN}测试完成${NC}"
    exit 0
fi
