#!/bin/sh
# 唤起本机 DevEco Studio 打开本工程。
# DevEco 在鸿蒙 PC 上以 npm 包形式安装（@deveco/deveco-code），
# 入口是 ~/npm/bin/deveco，传目录参数即可打开工程。
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NODE=/data/service/hnp/hmos-clt.org/hmos-clt_1.0.0/node/bin/node
DEVECO=/storage/Users/currentUser/npm/bin/deveco

if [ ! -f "$DEVECO" ]; then
    echo "未在本机找到 DevEco Studio 入口：$DEVECO"
    echo "请手动打开 DevEco Studio，选择 File > Open，目录：$ROOT"
    exit 1
fi

echo "用 DevEco Studio 打开：$ROOT"

if [ -x "$NODE" ]; then
    exec "$NODE" "$DEVECO" "$ROOT"
else
    exec "$DEVECO" "$ROOT"
fi
