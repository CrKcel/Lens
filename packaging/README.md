# Lens 打包与分发

所有产物输出到仓库根目录的 `dist/`。版本号取自根 `CMakeLists.txt` 的 `project(VERSION)`，
发版时改这一处即可（PKGBUILD 的 `pkgver` 需同步）。

## Windows：exe 安装包 + 绿色版 Zip

前置：MSVC + Qt 6.5+（`windeployqt` 所在的 Qt bin 目录需在 PATH），NSIS（CI 的
runner 预装；本机用 [choco install nsis](https://community.chocolatey.org/packages/nsis)）。

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cd build
cpack -G "NSIS;ZIP"
```

- `dist/lens-<版本>-win64.exe` — NSIS 安装包（安装时自动部署 Qt 运行时、开始菜单
  快捷方式、卸载支持）；
- `dist/lens-<版本>-win64-portable.zip` — 绿色版，解压即用（`bin/lens.exe` + Qt DLL）。

Qt 运行时随包部署由 `LENS_DEPLOY_QT` 控制（默认 `ON`），在 `cmake --install` 阶段调用
`windeployqt`（含 `--qmldir src/app` 以抓取 QML 模块），NSIS 与 ZIP 共用同一安装树。

## macOS

前置：Xcode command line tools + Qt 6.5+（`macdeployqt` 在 PATH）。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
(cd build && cpack -G "DragNDrop")
```

产物 `dist/lens-<版本>-macos.dmg`：内含 `lens.app`（`macdeployqt` 已打入 Qt 框架与
QML 模块）及指向 /Applications 的符号链接。

签名与公证（可选，未配置则产出未签名 DMG）：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"          # Universal 2（可选）
codesign --deep --force --sign "Developer ID Application: <name>" dist/lens-*-macos.dmg
xcrun notarytool submit dist/lens-*-macos.dmg --keychain-profile lens --wait
xcrun stapler staple dist/lens-*-macos.dmg
```

## Linux

### deb

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
(cd build && cpack -G "DEB")
```

产物 `dist/lens_<版本>_amd64.deb`，安装前缀 `/usr`，依赖系统 Qt 6.5+（清单见
`packaging/cpack-lens-config.cmake`；构建机 Qt 版本较新时请在依赖里同步抬高最低版本，
或改开 `CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON` 自动推导）。安装后自动刷新桌面数据库
（`packaging/debian/postinst`）。

### AppImage

```sh
packaging/build-appimage.sh
```

脚本完成 Release 构建 → 安装进 `AppDir/usr` → 下载 `linuxdeploy` → 用 Qt 插件打齐
运行时 → 生成 `dist/Lens-<版本>-<arch>.AppImage`（大写 L，取自 .desktop 的 Name）。
可用 `LINUXDEPLOY` 环境变量指定本地 linuxdeploy。这是给非 Debian 系 / 旧发行版用户的推荐产物。

### PKGBUILD

`packaging/PKGBUILD` 从 GitHub tag 源码包构建（`source` 指向
`https://github.com/CrKcel/Lens/archive/refs/tags/v<pkgver>.tar.gz`）：

```sh
cd packaging
# 本地测试：把 source 换成仓库根目录打包，或直接对 tag 打包
makepkg -f
sudo pacman -U lens-<版本>-1-x86_64.pkg.tar.zst
```

## GitHub Actions 发布

`.github/workflows/release.yml`：推 `v*` tag 触发，三个 job 并行产出
NSIS 安装包 + ZIP 绿色版（windows-latest）、DMG（macos-latest）、deb + AppImage
（ubuntu-latest），全部上传 artifact 并发布到 GitHub Release。
Qt 统一用 `jurplel/install-qt-action@v4` 安装（版本在 workflow 顶部 `QT_VERSION` 处统一调整）。

## 图标

`icons/lens.svg` 是源文件；`icons/gen_icon.py`生成 `lens.png`、多尺寸 PNG 与 `lens.ico`。