# cpack-lens-config.cmake — CPack 安装期项目配置文件。
# CPack 对每个生成器各 include 一次，CPACK_GENERATOR 指明当前生成器，
# 生成器差异（安装前缀、产物命名、deb 依赖）都在这里按分支设置。

set(_lens_ver "${CPACK_PACKAGE_VERSION}")

# 平台代号：Windows / Darwin(macos) / Linux
if(CPACK_SYSTEM_NAME MATCHES "Windows")
    set(_lens_platform "win64")
elseif(CPACK_SYSTEM_NAME MATCHES "Darwin")
    set(_lens_platform "macos")
else()
    set(_lens_platform "linux-${CMAKE_SYSTEM_PROCESSOR}")
endif()

# deb 架构代号（构建机缺 dpkg 时无法自动识别）
set(_lens_deb_arch "amd64")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(_lens_deb_arch "arm64")
endif()

if(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_PACKAGING_INSTALL_PREFIX "")
    set(CPACK_PACKAGE_FILE_NAME "lens-${_lens_ver}-win64")

elseif(CPACK_GENERATOR STREQUAL "ZIP")
    # 绿色版：解压即用，顶层是 bin/lens.exe 与 Qt 运行时
    set(CPACK_PACKAGING_INSTALL_PREFIX "")
    set(CPACK_PACKAGE_FILE_NAME "lens-${_lens_ver}-${_lens_platform}-portable")

elseif(CPACK_GENERATOR STREQUAL "DragNDrop")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_PACKAGE_FILE_NAME "lens-${_lens_ver}-macos")

elseif(CPACK_GENERATOR STREQUAL "DEB")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${_lens_deb_arch}")
    set(CPACK_STRIP_FILES ON)
    # 运行时依赖：系统 Qt 6.5+（QML 模块与 SQLite 驱动按需列出）。
    # 如需自动推导可改用 CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON（构建机需 dpkg-shlibdeps）。
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libqt6core6 (>= 6.5), libqt6gui6 (>= 6.5), libqt6network6 (>= 6.5), libqt6sql6 (>= 6.5), libqt6sql6-sqlite, libqt6qml6 (>= 6.5), libqt6quick6 (>= 6.5), libqt6quickcontrols2-6 (>= 6.5), qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtqml-workerscript, libstdc++6 (>= 12), libgcc-s1 (>= 12)")
endif()
