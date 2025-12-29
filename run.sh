#!/bin/sh
#
# Use this script to run your program LOCALLY (in WSL).
#
set -e

# 仓库根目录（脚本所在目录）
REPO_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

# 把构建目录放到 WSL 的 Linux 文件系统里，避免 /mnt/d 的权限问题
BUILD_DIR="${HOME}/.cache/codecrafters-shell-cpp/build"

mkdir -p "$BUILD_DIR"

# Configure & Build
cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug
cmake --build "$BUILD_DIR" -j "$(nproc)"

# Run
exec "$BUILD_DIR/shell" "$@"
