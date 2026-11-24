#!/usr/bin/env bash
# 开/关 实时聚光灯（需 spotlight 已在 PATH 中）
BIN="${SPOTLIGHT_BIN:-spotlight}"
if pgrep -x spotlight >/dev/null 2>&1; then
  pkill -x spotlight
else
  setsid "$BIN" --dim 0.60 --radius 320 --soft 0.55 >/dev/null 2>&1 < /dev/null &
fi
