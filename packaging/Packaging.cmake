# Packaging.cmake — 安装规则与 CPack 打包配置（Windows NSIS/ZIP、macOS DMG、Linux DEB）
#
# 用法（产物输出到 ${CMAKE_SOURCE_DIR}/dist/）：
#   cmake --build build && (cd build && cpack -G "NSIS;ZIP")        # Windows
#   cmake --build build && (cd build && cpack -G "DragNDrop")       # macOS
#   cmake --build build && (cd build && cpack -G "DEB")             # Linux
#
# Qt 运行时随包部署（windeployqt / macdeployqt）由 LENS_DEPLOY_QT 控制，
# 默认开启；Linux 上不打 Qt 进 deb，运行时依赖系统 Qt（见 cpack-lens-config.cmake）。
# AppImage 与 Arch 包走 packaging/build-appimage.sh 与 packaging/PKGBUILD。

option(LENS_DEPLOY_QT "Bundle Qt runtime into install tree (windeployqt/macdeployqt)" ON)

set(LENS_ICON_DIR "${CMAKE_SOURCE_DIR}/packaging/icons")

# ---- 安装规则 ------------------------------------------------------------

install(TARGETS lens
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)

if(UNIX AND NOT APPLE)
    install(FILES packaging/lens.desktop DESTINATION share/applications)
    install(FILES ${LENS_ICON_DIR}/lens.svg
        DESTINATION share/icons/hicolor/scalable/apps RENAME lens.svg)
    foreach(_size 16 32 48 64 128 256)
        install(FILES ${LENS_ICON_DIR}/lens-${_size}.png
            DESTINATION share/icons/hicolor/${_size}x${_size}/apps RENAME lens.png)
    endforeach()
    install(FILES ${LENS_ICON_DIR}/lens.png
        DESTINATION share/icons/hicolor/512x512/apps RENAME lens.png)
endif()

# ---- Qt 运行时部署（安装期执行，CPack 的暂存树同样生效）--------------------

get_filename_component(LENS_QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)

if(WIN32 AND LENS_DEPLOY_QT)
    find_program(LENS_WINDEPLOYQT windeployqt HINTS "${LENS_QT_BIN_DIR}")
    if(LENS_WINDEPLOYQT)
        install(CODE "
            message(STATUS \"windeployqt: \${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:lens>\")
            execute_process(COMMAND \"${LENS_WINDEPLOYQT}\"
                --no-compiler-runtime --no-system-d3d-compiler --no-opengl-sw
                --qmldir \"${CMAKE_SOURCE_DIR}/src/app\"
                \"\${CMAKE_INSTALL_PREFIX}/bin/$<TARGET_FILE_NAME:lens>\"
                COMMAND_ERROR_IS_FATAL ANY)
        ")
    else()
        message(WARNING "windeployqt 未找到，安装树将缺少 Qt 运行时；请确认 Qt bin 目录在 PATH 中")
    endif()
elseif(APPLE AND LENS_DEPLOY_QT)
    find_program(LENS_MACDEPLOYQT macdeployqt HINTS "${LENS_QT_BIN_DIR}")
    if(LENS_MACDEPLOYQT)
        install(CODE "
            message(STATUS \"macdeployqt: \${CMAKE_INSTALL_PREFIX}/$<TARGET_BUNDLE_DIR_NAME:lens>\")
            execute_process(COMMAND \"${LENS_MACDEPLOYQT}\"
                \"\${CMAKE_INSTALL_PREFIX}/$<TARGET_BUNDLE_DIR_NAME:lens>\"
                -qmldir=\"${CMAKE_SOURCE_DIR}/src/app\" -verbose=1
                COMMAND_ERROR_IS_FATAL ANY)
        ")
    else()
        message(WARNING "macdeployqt 未找到，.app 将缺少 Qt 框架")
    endif()
endif()

# ---- CPack 总配置（各生成器差异在 cpack-lens-config.cmake 里按 CPACK_GENERATOR 分支）---

set(CPACK_PACKAGE_NAME lens)
set(CPACK_PACKAGE_VENDOR "Lens Project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lens — a lightweight GUI AI Agent")
set(CPACK_PACKAGE_DESCRIPTION
    "Lens is a lightweight GUI AI Agent (C++20 + Qt Quick) with streaming "
    "chat, tool calling, MCP and multi-provider support.")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Lens")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_SOURCE_DIR}/dist")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/packaging/cpack-lens-config.cmake")
set(CPACK_VERBATIM_VARIABLES ON)
# strip 只对 DEB 开启（在 cpack-lens-config.cmake 里设置）：
# MSVC 工具链没有 strip，Windows 产物开着会出错；macOS 交由 macdeployqt 处理

# NSIS（exe 安装包）
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_EXECUTABLES_DIRECTORY "bin")
set(CPACK_NSIS_DISPLAY_NAME "Lens")
set(CPACK_NSIS_PACKAGE_NAME "Lens")
set(CPACK_PACKAGE_EXECUTABLES "lens" "Lens")
set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\\\lens.exe")
set(CPACK_NSIS_MUI_ICON "${LENS_ICON_DIR}/lens.ico")
set(CPACK_NSIS_MUI_UNIICON "${LENS_ICON_DIR}/lens.ico")
set(CPACK_NSIS_MUI_FINISHPAGE_RUN "lens.exe")
set(CPACK_NSIS_URL_INFO_ABOUT "https://github.com/CrKcel/Lens")

# DEB
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Lens Project <dev@lens.invalid>")
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_PRIORITY optional)
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/CrKcel/Lens")
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_SOURCE_DIR}/packaging/debian/postinst")

# DragNDrop（DMG）：默认即可（.app + 拷入 Applications 的符号链接）
set(CPACK_DMG_VOLUME_NAME "Lens")

include(CPack)
