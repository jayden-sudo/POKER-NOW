#!/usr/bin/env bash
# run_tests.sh -- POKER-NOW host 端單元測試的唯一權威入口。
#
# 編譯並執行純 C 遊戲邏輯(手牌評估 hand_eval、側池結算 side_pot)的 host 測試,
# 不需要 ESP-IDF 或實體硬體。路徑一律相對本腳本解析,因此可從任何工作目錄執行:
#
#     tools/run_tests.sh        # 從 repo 根
#     ./run_tests.sh            # 從 tools/
#
# 以 -Werror 建置:任何編譯器警告都視為測試失敗,退出碼非零可直接用於 CI。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CORE="$REPO_ROOT/common/components/poker_core"

CC="${CC:-cc}"
BIN="$(mktemp -t poker_htest.XXXXXX)"
trap 'rm -f "$BIN"' EXIT

"$CC" -std=c11 -Wall -Wextra -Werror -DPK_HOST_TEST \
    -I "$CORE/include" \
    "$SCRIPT_DIR/test_hand_eval.c" \
    "$CORE/src/hand_eval.c" \
    "$CORE/src/side_pot.c" \
    -o "$BIN"

"$BIN"
