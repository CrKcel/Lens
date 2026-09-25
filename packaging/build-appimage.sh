#!/usr/bin/env bash
# build-appimage.sh — 构建 Lens AppImage（自包含 Qt 运行时，适合任意 Linux 发行版）
#
# 依赖：cmake、ninja（可选）、linuxdeploy（缺省自动下载）。
# 用法：
#   packaging/build-appimage.sh
#   LINUXDEPLOY=/path/to/linuxdeploy-x86_64.AppImage packaging/build-appimage.sh
# 产物：dist/lens-<version>-<arch>.AppImage

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-appimage}"
APPDIR="$BUILD_DIR/AppDir"
ARCH="${ARCH:-$(uname -m)}"

# 1. Release 构建（安装前缀 /usr，AppDir 内保持标准 FHS 布局）
cmake -S "$ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$BUILD_DIR" -j"$(nproc)"

# 2. 安装到 AppDir（含 .desktop 与 hicolor 图标）
rm -rf "$APPDIR"
cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"

# 3. linuxdeploy 打包 Qt 运行时并生成 AppImage
if [[ -z "${LINUXDEPLOY:-}" ]]; then
    LINUXDEPLOY="$BUILD_DIR/linuxdeploy-$ARCH.AppImage"
    if [[ ! -x "$LINUXDEPLOY" ]]; then
        echo ">> 下载 linuxdeploy ($ARCH)"
        curl -fsSL -o "$LINUXDEPLOY" \
            "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
        chmod +x "$LINUXDEPLOY"
    fi
fi

mkdir -p "$ROOT/dist"
(cd "$ROOT/dist" && "$LINUXDEPLOY" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/lens" \
    --desktop-file "$APPDIR/usr/share/applications/lens.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/lens.png" \
    --plugin qt \
    --output appimage \
    --appimage-extract-and-run)

# 产物名取自 .desktop 的 Name（Lens-<版本>-<arch>.AppImage）
echo ">> 完成：$(ls "$ROOT/dist"/*.AppImage)"
