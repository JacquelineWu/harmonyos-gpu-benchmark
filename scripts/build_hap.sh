#!/bin/sh
# 命令行构建 HAP —— 用的是本机 DevEco Studio 同款工具链
# （/data/service/hnp/hmos-clt.org 的 HarmonyOS SDK 26 + hvigor），
# 因此这里的构建结果与 IDE 里点 Build 等价。
set -e

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CLT=/data/service/hnp/hmos-clt.org/hmos-clt_1.0.0

if [ ! -d "$CLT" ]; then
    echo "未找到 DevEco 命令行工具链：$CLT"
    exit 1
fi

cd "$ROOT" || exit 1
echo "工程目录：$ROOT"
echo "SDK：$CLT/sdk"

DEVECO_SDK_HOME="$CLT/sdk" \
    "$CLT/node/bin/node" "$CLT/hvigor/bin/hvigorw.js" assembleHap --no-daemon

OUT="$ROOT/entry/build/default/outputs/default"
echo "产物目录：$OUT"
ls -l "$OUT" 2>/dev/null
